#include "stdio.h"
#include "string.h"
#include "hardware.h"

#include "menu.h"
#include "tos.h"
#include "fat.h"
#include "fpga.h"
#include "cdc_control.h"
#include "debug.h"

#define CONFIG_FILENAME  "MIST    CFG"

typedef struct {
  unsigned long system_ctrl;  // system control word
  char tos_img[12];
  char cart_img[12];
  char acsi_img[2][12];
  char video_adjust[2];
  char cdc_control_redirect;
  char sd_direct;
} tos_config_t;

static tos_config_t config;

#define TOS_BASE_ADDRESS_192k    0xfc0000
#define TOS_BASE_ADDRESS_256k    0xe00000
#define CART_BASE_ADDRESS        0xfa0000
#define VIDEO_BASE_ADDRESS       0x010000

static unsigned char font[2048];  // buffer for 8x16 atari font

// two floppies
static struct {
  fileTYPE file;
  unsigned char sides;
  unsigned char spt;
} fdd_image[2];

// one harddisk
fileTYPE hdd_image[2];
unsigned long hdd_direct = 0;

static unsigned char dma_buffer[512];

static const char *acsi_cmd_name(int cmd) {
  static const char *cmdname[] = {
    "Test Drive Ready", "Restore to Zero", "Cmd $2", "Request Sense",
    "Format Drive", "Read Block limits", "Reassign Blocks", "Cmd $7", 
    "Read Sector", "Cmd $9", "Write Sector", "Seek Block", 
    "Cmd $C", "Cmd $D", "Cmd $E", "Cmd $F", 
    "Cmd $10", "Cmd $11", "Inquiry", "Verify", 
    "Cmd $14", "Mode Select", "Cmd $16", "Cmd $17", 
    "Cmd $18", "Cmd $19", "Mode Sense", "Start/Stop Unit", 
    "Cmd $1C", "Cmd $1D", "Cmd $1E", "Cmd $1F"
  };
  
  return cmdname[cmd];
}

char tos_get_cdc_control_redirect(void) {
  return config.cdc_control_redirect;
}

void tos_set_cdc_control_redirect(char mode) {
  if((mode >= CDC_REDIRECT_NONE) && (mode <= CDC_REDIRECT_MIDI)) {
    config.cdc_control_redirect = mode;

    // core is only informed about redirections of rs232/par/midi
    if(mode < CDC_REDIRECT_RS232)
      mode = 0;
    else
      mode -= CDC_REDIRECT_RS232 - 1;

    tos_update_sysctrl((tos_system_ctrl() & ~0x0c000000) | 
		       (((unsigned long)mode) << 26) );
  }
}

static void mist_dma_print() {
  static unsigned char scnt = 0;
  unsigned char buffer[16];
  
  EnableFpga();
  SPI(MIST_GET_DMASTATE);
  spi_read(buffer, 16);
  DisableFpga();

  if(buffer[3] != scnt) {
    iprintf("DMA addr = %x, dir=%s, scnt=%d\n",
	    256*256*buffer[0]+256*buffer[1]+(buffer[2]&0xfe),
	    (buffer[2]&1)?"write":"read", buffer[3]);
    scnt = buffer[3];
  }
}

void tos_set_video_adjust(char axis, char value) {
  config.video_adjust[axis] += value;

  EnableFpga();
  SPI(MIST_SET_VADJ);
  SPI(config.video_adjust[0]);
  SPI(config.video_adjust[1]);
  DisableFpga();
}

char tos_get_video_adjust(char axis) {
  return config.video_adjust[axis];
}

static void mist_memory_set_address(unsigned long a, unsigned char s, char rw) {
  //  iprintf("set addr = %x, %d, %d\n", a, s, rw);

  a |= rw?0x1000000:0;
  a >>= 1;

  EnableFpga();
  SPI(MIST_SET_ADDRESS);
  SPI(s);
  SPI((a >> 16) & 0xff);
  SPI((a >>  8) & 0xff);
  SPI((a >>  0) & 0xff);
  DisableFpga();
}

static void mist_set_control(unsigned long ctrl) {
  EnableFpga();
  SPI(MIST_SET_CONTROL);
  SPI((ctrl >> 24) & 0xff);
  SPI((ctrl >> 16) & 0xff);
  SPI((ctrl >>  8) & 0xff);
  SPI((ctrl >>  0) & 0xff);
  DisableFpga();
}

// grab/give bus from/to cpu
static void mist_request_bus(char req) {
#if 0
  EnableFpga();
  SPI(req?MIST_BUS_REQ:MIST_BUS_REL);
  DisableFpga();
#endif
}

static void mist_memory_read(char *data, unsigned long words) {
  EnableFpga();
  SPI(MIST_READ_MEMORY);

  // transmitted bytes must be multiple of 2 (-> words)
  while(words--) {
    *data++ = SPI(0);
    *data++ = SPI(0);
  }

  DisableFpga();
}

static void mist_memory_write(char *data, unsigned long words) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  while(words--) {
    SPI_WRITE(*data++);
    SPI_WRITE(*data++);
  }

  DisableFpga();
}

static void mist_memory_read_block(char *data) {
  EnableFpga();
  SPI(MIST_READ_MEMORY);

  spi_block_read(data);

  DisableFpga();
}

static void mist_memory_write_block(char *data) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  spi_block_write(data);

  DisableFpga();
}

void mist_memory_set(char data, unsigned long words) {
  EnableFpga();
  SPI(MIST_WRITE_MEMORY);

  while(words--) {
    SPI_WRITE(data);
    SPI_WRITE(data);
  }

  DisableFpga();
}

// enable direct sd card access on acsi0
void tos_set_direct_hdd(char on) {
  config.sd_direct = on;

  if(on) {
    tos_debugf("ACSI: enable direct sd access");
    hdd_direct = MMC_GetCapacity();

    // hddrivers auto detection doesn't cope with the full 1gb
    if(hdd_direct >= 0x1fffffl)
      hdd_direct = 0x1ffff0l;
    
    tos_debugf("ACSI: Direct capacity = %ld (%ld Bytes)", hdd_direct, hdd_direct*512);
    config.system_ctrl |= TOS_ACSI0_ENABLE;
  } else {
    tos_debugf("ACSI: disable direct sd access");
    config.system_ctrl &= ~TOS_ACSI0_ENABLE;
    hdd_direct = 0;

    // check if image access should be enabled instead
    if(hdd_image[0].size) {
      tos_debugf("ACSI: re-enabling image on ACSI0");
      config.system_ctrl |= TOS_ACSI0_ENABLE;
    }
  }

  mist_set_control(config.system_ctrl);
}

char tos_get_direct_hdd() {
  return config.sd_direct;
}

static void dma_ack(unsigned char status) {
  EnableFpga();
  SPI(MIST_ACK_DMA);
  SPI(status);
  DisableFpga(); 
}

static void dma_nak(void) {
  EnableFpga();
  SPI(MIST_NAK_DMA);
  DisableFpga(); 
}

static void handle_acsi(unsigned char *buffer) {
  static unsigned char asc[2] = { 0,0 };
  unsigned char target = buffer[9] >> 5;
  unsigned char device = buffer[10] >> 5;
  unsigned char cmd = buffer[9] & 0x1f;
  unsigned int dma_address = 256 * 256 * buffer[0] + 
    256 * buffer[1] + (buffer[2]&0xfe);
  unsigned char scnt = buffer[3];
  unsigned long lba = 256 * 256 * (buffer[10] & 0x1f) +
    256 * buffer[11] + buffer[12];
  unsigned short length = buffer[13];
  if(length == 0) length = 256;

  mist_request_bus(1);

  if(user_io_dip_switch1()) {
    tos_debugf("ACSI: target %d.%d, \"%s\"", target, device, acsi_cmd_name(cmd));
    tos_debugf("ACSI: lba %lu (%lx), length %u", lba, lba, length);
    tos_debugf("DMA: scnt %u, addr %p", scnt, dma_address);
    
    if(buffer[16] == 0xa5) {
      tos_debugf("DMA: fifo %d/%d %x %s", 
		 (buffer[17]>>4)&0x0f, buffer[17]&0x0f, 
		 buffer[18], (buffer[2]&1)?"OUT":"IN");
      tos_debugf("DMA stat=%x, mode=%x, fdc_irq=%d, acsi_irq=%d",
		 buffer[19], buffer[20], buffer[21], buffer[22]);
    }
  }

  //  mist_memory_set_address(dma_address, scnt, 0);

  // only a harddisk on ACSI 0/1 is supported
  // ACSI 0/1 is only supported if a image is loaded
  // ACSI 0 is only supported for direct IO
  if( ((target < 2) && (hdd_image[target].size != 0)) ||
      ((target == 0) && hdd_direct)) {
    unsigned long blocks = hdd_image[target].size / 512;

    // if in hdd direct mode then hdd_direct contains device sizee
    if(hdd_direct && target==0) blocks = hdd_direct;

    // only lun0 is fully supported
    switch(cmd) {
    case 0x00: // test drive ready
    case 0x04: // format
      if(device == 0) {
	asc[target] = 0x00;	
	dma_ack(0x00);
      } else {
	asc[target] = 0x25;	
	dma_ack(0x02);
      }
      break;
      
    case 0x03: // request sense
      if(device != 0)
	asc[target] = 0x25;

      bzero(dma_buffer, 512);
      dma_buffer[7] = 0x0b;
      if(asc[target] != 0) {
	dma_buffer[2] = 0x05;
	dma_buffer[12] = asc[target];
      }
      mist_memory_write(dma_buffer, 9); // 18 bytes      
      dma_ack(0x00);
      asc[target] = 0x00;
      break;
      
    case 0x08: // read sector
      if(device == 0) {
	if(lba+length <= blocks) {
	  DISKLED_ON;
	  while(length) {
	    if(hdd_direct && target == 0) {
	      if(user_io_dip_switch1()) 
		tos_debugf("ACSI: direct read %ld", lba);
	      MMC_Read(lba++, dma_buffer);
	    } else {
	      FileSeek(&hdd_image[target], lba++, SEEK_SET);
	      FileRead(&hdd_image[target], dma_buffer);
	    }
	    //	    hexdump(dma_buffer, 32, 0);
	    mist_memory_write_block(dma_buffer);
	    length--;
	  }
	  DISKLED_OFF;
	  dma_ack(0x00);
	  asc[target] = 0x00;
	} else {
	  tos_debugf("ACSI: read (%d+%d) exceeds device limits (%d)", 
		     lba, length, blocks);
	  dma_ack(0x02);
	  asc[target] = 0x21;
	}
      } else {
	dma_ack(0x02);
	asc[target] = 0x25;
      }
      break;
      
    case 0x0a: // write sector
      if(device == 0) {
	if(lba+length <= blocks) {
	  DISKLED_ON;
	  while(length) {
	    mist_memory_read_block(dma_buffer);
	    if(hdd_direct && target == 0) {
	      if(user_io_dip_switch1()) 
		tos_debugf("ACSI: direct write %ld", lba);
	      MMC_Write(lba++, dma_buffer);
	    } else {
	      FileSeek(&hdd_image[target], lba++, SEEK_SET);
	      FileWrite(&hdd_image[target], dma_buffer);
	    }
	    length--;
	  }
	  DISKLED_OFF;
	  dma_ack(0x00);
	  asc[target] = 0x00;
	} else {
	  tos_debugf("ACSI: write (%d+%d) exceeds device limits (%d)", 
		     lba, length, blocks);
	  dma_ack(0x02);
	  asc[target] = 0x21;
	}
      } else {
	dma_ack(0x02);
	asc[target] = 0x25;
      }
      break;
      
    case 0x12: // inquiry
      if(hdd_direct && target == 0) tos_debugf("ACSI: Inquiry DIRECT");
      else                          tos_debugf("ACSI: Inquiry %.11s", hdd_image[target].name);
      bzero(dma_buffer, 512);
      dma_buffer[2] = 1;                                   // ANSI version
      dma_buffer[4] = length-5;                            // len
      memcpy(dma_buffer+8,  "MIST    ", 8);                // Vendor
      memcpy(dma_buffer+16, "                ", 16);       // Clear device entry
      if(hdd_direct && target == 0) memcpy(dma_buffer+16, "SD DIRECT", 9);// Device 
      else                          memcpy(dma_buffer+16, hdd_image[target].name, 11);
      memcpy(dma_buffer+32, "ATH ", 4);                    // Product revision
      memcpy(dma_buffer+36, VDATE "  ", 8);                // Serial number
      if(device != 0) dma_buffer[0] = 0x7f;
      mist_memory_write(dma_buffer, length/2);      
      dma_ack(0x00);
      asc[target] = 0x00;
      break;
      
    case 0x1a: // mode sense
      if(device == 0) {
	tos_debugf("ACSI: mode sense, blocks = %u", blocks);
	bzero(dma_buffer, 512);
	dma_buffer[3] = 8;            // size of extent descriptor list
	dma_buffer[5] = blocks >> 16;
	dma_buffer[6] = blocks >> 8;
	dma_buffer[7] = blocks;
	dma_buffer[10] = 2;           // byte 1 of block size in bytes (512)
	mist_memory_write(dma_buffer, length/2);      
	dma_ack(0x00);
	asc[target] = 0x00;	
      } else {
	asc[target] = 0x25;	
	dma_ack(0x02);
      }
      break;
      
    case 0x1f: // ICD command?
      tos_debugf("ACSI: ICD command %s ($%02x)",
		 acsi_cmd_name(buffer[10] & 0x1f), buffer[10] & 0x1f);
      asc[target] = 0x05;	
      dma_ack(0x02);
      break;
      
    default:
      tos_debugf("ACSI: >>>>>>>>>>>> Unsupported command <<<<<<<<<<<<<<<<");
      asc[target] = 0x05;	
      dma_ack(0x02);
      break;
    }
  } else {
    tos_debugf("ACSI: Request for unsupported target");

    // tell acsi state machine that io controller is done 
    // but don't generate a acsi irq
    dma_nak();
  }

  mist_request_bus(0);
}

static void handle_fdc(unsigned char *buffer) {
  // extract contents
  unsigned int dma_address = 256 * 256 * buffer[0] + 
    256 * buffer[1] + (buffer[2]&0xfe);
  unsigned char scnt = buffer[3];
  unsigned char fdc_cmd = buffer[4];
  unsigned char fdc_track = buffer[5];
  unsigned char fdc_sector = buffer[6];
  unsigned char fdc_data = buffer[7];
  unsigned char drv_sel = 3-((buffer[8]>>2)&3); 
  unsigned char drv_side = 1-((buffer[8]>>1)&1); 

  //  mist_request_bus(1);

  //  tos_debugf("FDC: sel %d, cmd %x", drv_sel, fdc_cmd);
  
  // check if a matching disk image has been inserted
  if(drv_sel && fdd_image[drv_sel-1].file.size) {
    
    // if the fdc has been asked to write protect the disks, then
    // write sector commands should never reach the oi controller
    
    // read/write sector command
    if((fdc_cmd & 0xc0) == 0x80) {

      // convert track/sector/side into disk offset
      unsigned int offset = drv_side; 
      offset += fdc_track * fdd_image[drv_sel-1].sides;
      offset *= fdd_image[drv_sel-1].spt;
      offset += fdc_sector-1;

      if(user_io_dip_switch1())
	tos_debugf("FDC req %d sec (%c, SD:%d, T:%d, S:%d = %d) -> %p", scnt, 
		'A'+drv_sel-1, drv_side, fdc_track, fdc_sector, offset,
		dma_address);

#if 0
      static int nix = 0;
      if((drv_side == 0) && (fdc_track==0) && (fdc_sector == 1) && 
	 (dma_address == 0x12414) && !nix) {
	iprintf("urgs ...\n");
	scnt = 0;
	nix = 1;
      }
#endif

      while(scnt) {
	//	iprintf("  sector %d\n", offset);

	DISKLED_ON;
	
	FileSeek(&fdd_image[drv_sel-1].file, offset, SEEK_SET);

	if((fdc_cmd & 0xe0) == 0x80) { 
	  // read from disk ...
	  FileRead(&fdd_image[drv_sel-1].file, dma_buffer);	  
	  // ... and copy to ram
	  mist_memory_write_block(dma_buffer);
	} else {
	  // read from ram ...
	  mist_memory_read_block(dma_buffer);
	  // ... and write to disk
	  FileWrite(&(fdd_image[drv_sel-1].file), dma_buffer);
	}
	
	//	mist_dma_print();

	DISKLED_OFF;
	
	scnt--;
	dma_address += 512;
	offset += 1;

	// tell DMA that one sector has been read
	// ...
      }

      dma_ack(0x00);
      //      iprintf("done\n");

    } else if((fdc_cmd & 0xc0) == 0xc0) {
      char msg[32];

      if((fdc_cmd & 0xe0) == 0xc0) iprintf("READ ADDRESS\n");

      if((fdc_cmd & 0xf0) == 0xe0) {
	iprintf("READ TRACK %d SIDE %d\n", fdc_track, drv_side);
	siprintf(msg, "RD TRK %d S %d", fdc_track, drv_side);
	InfoMessage(msg);
      }

      if((fdc_cmd & 0xf0) == 0xf0) {
	iprintf("WRITE TRACK %d SIDE %d\n", fdc_track, drv_side);
	siprintf(msg, "WR TRK %d S %d", fdc_track, drv_side);
	InfoMessage(msg);
      }

      iprintf("scnt = %d\n", scnt);

      dma_ack(0x00);
    }
  }
  //  mist_request_bus(0);
}  

static void mist_get_dmastate() {
  unsigned char buffer[32];
  
  EnableFpga();
  SPI(MIST_GET_DMASTATE);
  spi_read(buffer, 32);
  DisableFpga();

  //  hexdump(buffer, 16, 0);

  //  check if acsi is busy
  if(buffer[15] & 0x01) 
    handle_acsi(buffer);

  // check if fdc is busy
  if(buffer[8] & 0x01) 
    handle_fdc(buffer);
}

// color test, used to test the shifter without CPU/TOS
#define COLORS   20
#define PLANES   4

static void tos_write(char *str);
static void tos_color_test() {
  unsigned short buffer[COLORS][PLANES];

  int y;
  for(y=0;y<13;y++) {
    int i, j;
    for(i=0;i<COLORS;i++)
      for(j=0;j<PLANES;j++)
	buffer[i][j] = ((y+i) & (1<<j))?0xffff:0x0000;

    for(i=0;i<16;i++) {
      mist_memory_set_address(VIDEO_BASE_ADDRESS + (16*y+i)*160, 1, 0);
      mist_memory_write((char*)buffer, COLORS*PLANES);
    }
  }

#if 1
  mist_memory_set_address(VIDEO_BASE_ADDRESS, 1, 0);
  mist_memory_set(0xf0, 40);

  mist_memory_set_address(VIDEO_BASE_ADDRESS+80, 1, 0);
  mist_memory_set(0x55, 40);

  mist_memory_set_address(VIDEO_BASE_ADDRESS+160, 1, 0);
  mist_memory_set(0x0f, 40);

#if 1
  tos_write("");
  tos_write("AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDDEEEEEEEEFFFFFFFFGGGGGGGGHHHHHHHHIIIIIIIIJJJJJJJJ");
  tos_write("ABCDEFGHIJHKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJHKLMNOPQRSTUVWXYZ0123456789");
#endif

  //  for(;;);
#endif
}

static void tos_write(char *str) {
  static int y = 0;
  int l;

  // empty string is "cursor home"
  if(!str) {
    y = 0;
    return;
  }

  // get next higher multiple of 16 for string length 
  // as dma works in 16 bytes chunks only
  int c = (strlen(str)+15) & ~15;
  {
    char buffer[c];

    // 16 pixel lines
    for(l=0;l<16;l++) {
      char *p = str, *f=buffer;
      while(*p)	*f++ = font[16 * *p++ + l];
      while(f < buffer+c) *f++ = font[16 * ' ' + l];

      mist_memory_set_address(VIDEO_BASE_ADDRESS + 80*(y+l), 1, 0);
      mist_memory_write(buffer, c/2);
    }
  }
  y+=16;
}

static void tos_clr() {
  mist_memory_set_address(VIDEO_BASE_ADDRESS, (32000+511)/512, 0);
  mist_memory_set(0, 16000);

  tos_write(NULL);
}

// the built-in OSD font, being used if everything else fails
extern unsigned char charfont[256][8];

static void tos_font_load() {
  fileTYPE file;
  if(FileOpen(&file,"SYSTEM  FNT")) {
    if(file.size == 4096) {
      int i;
      for(i=0;i<4;i++) {
	FileRead(&file, font+i*512);
	FileNextSector(&file);
      }

      return;
    } 
  }

  // if we couldn't load something, then just convert the 
  // built-on OSD font, so we see at least something
  unsigned char c, l, n;
  // copy 128 chars
  for(c=0;c<128;c++) {
    // each character is 8 pixel tall
    for(l=0;l<8;l++) {
      unsigned char *d = font + c*16 + 2*l;
      *d = 0;

      for(n=0;n<8;n++)
	if(charfont[c][n] & (1 << l))
	  *d |= 0x80 >> n;

      *(d+1) = *d;
    }
  }
}

void tos_load_cartridge(char *name) {
  fileTYPE file;

  if(name)
    strncpy(config.cart_img, name, 11);

  // upload cartridge 
  if(config.cart_img[0] && FileOpen(&file, config.cart_img)) {
    int i;
    char buffer[512];
	
    tos_debugf("%s:\n  size = %d", config.cart_img, file.size);

    int blocks = file.size / 512;
    tos_debugf("  blocks = %d", blocks);

    
    DISKLED_ON;
    for(i=0;i<blocks;i++) {
      FileRead(&file, buffer);

      if(!(i&0x7f))
	mist_memory_set_address(CART_BASE_ADDRESS+512*i, 128, 0);
      
      mist_memory_write_block(buffer);
      
      if(i != blocks-1)
	FileNextSector(&file);
    }
    DISKLED_OFF;
    
    tos_debugf("%s uploaded", config.cart_img);
    return; 
  }

  // erase that ram area to remove any previously uploaded
  // image
  tos_debugf("Erasing cart memory");
  mist_memory_set_address(CART_BASE_ADDRESS, 128, 0);
  mist_memory_set(0, 64*1024/2);
  mist_memory_set_address(CART_BASE_ADDRESS+128*512, 128, 0);
  mist_memory_set(0, 64*1024/2);
}

char tos_cartridge_is_inserted() {
  return config.cart_img[0];
}

void tos_upload(char *name) {
  fileTYPE file;

  // set video offset in fpga
  tos_set_video_adjust(0, 0);

  if(name)
    strncpy(config.tos_img, name, 11);

  // put cpu into reset
  config.system_ctrl |= TOS_CONTROL_CPU_RESET;
  mist_set_control(config.system_ctrl);

  tos_font_load();
  tos_clr();

  // do the MiST core handling
  tos_write("\x0e\x0f MIST core \x0e\x0f ");
  tos_write("Uploading TOS ... ");

  tos_debugf("Uploading TOS ...");

  DISKLED_ON;

  // upload and verify tos image
  if(FileOpen(&file, config.tos_img)) {
    int i;
    char buffer[512];
    unsigned long time;
    unsigned long tos_base = TOS_BASE_ADDRESS_192k;
	
    tos_debugf("TOS.IMG:\n  size = %d", file.size);

    if(file.size >= 256*1024)
      tos_base = TOS_BASE_ADDRESS_256k;
    else if(file.size != 192*1024)
      tos_debugf("WARNING: Unexpected TOS size!");

    int blocks = file.size / 512;
    tos_debugf("  blocks = %d", blocks);

    tos_debugf("  address = $%08x", tos_base);

    // clear first 16k
    mist_memory_set_address(0, 16384/512, 0);
    mist_memory_set(0x00, 8192);

#if 0  // spi transfer tests
    iprintf("SPI transfer test\n");

    // draw some max power pattern on screen, round to next sector
    // size
    mist_memory_set_address(VIDEO_BASE_ADDRESS, (32000+511)/512, 0);
    mist_memory_set(0x55, 16000);

    FileRead(&file, buffer);
    int run_ok = 0, run_fail = 0;

    while(1) {
      int j;
      char b2[512];

      for(j=0;j<512;j++) {
	buffer[j] ^= 0x55;
	b2[j] = 0xa5;
      }

      mist_memory_set_address(0, 1, 0);
      mist_memory_set(0xaa, 256);

      mist_memory_set_address(0, 1, 0);
      //      mist_memory_write_block(buffer);
      mist_memory_write(buffer, 256);

      mist_memory_set_address(0, 1, 1);
      //      mist_memory_read_block(b2);
      mist_memory_read(b2, 256);

      char ok = 1;
      for(j=0;j<512;j++) 
	if(buffer[j] != b2[j]) 
	  ok = 0;

      if(ok) run_ok++;
      else   run_fail++;

      if(!ok) {
	hexdump(buffer, 512, 0);
	hexdump(b2, 512, 0);

	// re-read to check whether reading fails
	mist_memory_set_address(0, 1, 1);
	mist_memory_read_block(b2);
	hexdump(b2, 512, 0);

	for(;;);
      }

      if(!((run_ok + run_fail)%10))
	iprintf("ok %d, failed %d\r", run_ok, run_fail);
    }
#endif

    time = GetTimer(0);
    tos_debugf("Uploading ...");

    for(i=0;i<blocks;i++) {
      FileRead(&file, buffer);

      // copy first 8 bytes to address 0 as well
      if(i == 0) {
	mist_memory_set_address(0, 1, 0);
	
	// write first 4 words
	// (actually 8 words/16 bytes as the dma cannot transfer less)
	mist_memory_write(buffer, 8);
      }

      // send address every 64k (128 sectors) as dma can max transfer
      // 255 sectors at once

      // set real tos base address
      if((i & 0x7f) == 0)
	mist_memory_set_address(tos_base+i*512, 128, 0);

      mist_memory_write_block(buffer);

      if(i != blocks-1)
	FileNextSector(&file);
    }

#if 1
    // verify
    if(user_io_dip_switch1()) {
      char b2[512];
      int j, ok;
	  
      FileSeek(&file, 0, SEEK_SET);    
      for(i=0;i<blocks;i++) {

	if(!(i&0x7f))
	  mist_memory_set_address(tos_base+i*512, 128, 1);

	FileRead(&file, b2);
	mist_memory_read_block(buffer);

	ok = -1;
	for(j=0;j<512;j++)
	  if(buffer[j] != b2[j])
	    if(ok < 0)
	      ok = j;

	if(ok >= 0) {
	  iprintf("Failed in block %d/%x (%x != %x)\n", i, ok, 0xff & buffer[ok], 0xff & b2[ok]);

	  hexdump(buffer, 512, 0);
	  puts("");
	  hexdump(b2, 512, 0);

	  // re-read to check whether read or write failed
	  bzero(buffer, 512);
	  mist_memory_set_address(tos_base+i*512, 1, 1);
	  mist_memory_read_block(buffer);

	  ok = -1;
	  for(j=0;j<512;j++)
	    if(buffer[j] != b2[j])
	      if(ok < 0)
		ok = j;

	  if(ok >= 0) {
	    iprintf("Re-read failed in block %d/%x (%x != %x)\n", i, ok, 0xff & buffer[ok], 0xff & b2[ok]);
	    hexdump(buffer, 512, 0);
	  } else
	    iprintf("Re-read ok!\n");

	  for(;;);
	}
	
	if(i != blocks-1)
	  FileNextSector(&file);
      }
      iprintf("Verify: %s\n", ok?"ok":"failed");
    }
#endif
    
    time = GetTimer(0) - time;
    tos_debugf("TOS.IMG uploaded in %lu ms (%d kB/s / %d kBit/s)", 
	    time >> 20, file.size/(time >> 20), 8*file.size/(time >> 20));
    
  } else {
    tos_debugf("Unable to find tos.img");
    tos_write("Unable to find tos.img");

    DISKLED_OFF;
    return;
  }
  
  DISKLED_OFF;

  // This is the initial boot if no name was given. Otherwise the
  // user reloaded a new os
  if(!name) {
    // load
    tos_load_cartridge(NULL);

    // try to open both floppies
    int i;
    for(i=0;i<2;i++) {
      char msg[] = "Found floppy disk image for drive X: ";
      char name[] = "DISK_A  ST ";
      msg[34] = name[5] = 'A'+i;
      
      fileTYPE file;
      if(FileOpen(&file, name)) {
	tos_write(msg);
	tos_insert_disk(i, &file);
      }
    }

    if(config.sd_direct) {
      tos_set_direct_hdd(1);
      tos_write("Enabling direct SD card access via ACSI0");
    } else {
      // try to open harddisk image
      for(i=0;i<2;i++) {
	if(FileOpen(&file, config.acsi_img[i])) {
	  char msg[] = "Found hard disk image for ACSIX";
	  msg[30] = '0'+i;
	  tos_write(msg);
	  tos_select_hdd_image(i, &file);
	}
      }
    }
  }

  tos_write("Booting ... ");

  // clear sector count register -> stop DMA
  mist_memory_set_address(0,0,0);

  ikbd_reset();

  // let cpu run (release reset)
  config.system_ctrl &= ~TOS_CONTROL_CPU_RESET;
  mist_set_control(config.system_ctrl);
}

static unsigned long get_long(char *buffer, int offset) {
  unsigned long retval = 0;
  int i;
  
  for(i=0;i<4;i++)
    retval = (retval << 8) + *(unsigned char*)(buffer+offset+i);

  return retval;
}

void tos_poll() {
  // 1 == button not pressed, 2 = 1 sec exceeded, else timer running
  static unsigned long timer = 1;

  mist_get_dmastate();  
 
  // check the user button
  if(user_io_user_button()) {
    if(timer == 1) 
      timer = GetTimer(1000);
    else if(timer != 2)
      if(CheckTimer(timer)) {
	tos_reset(1);
	timer = 2;
      }
  } else {
    // released while still running (< 1 sec)
    if(!(timer & 3))
      tos_reset(0);

    timer = 1;
  }
}

void tos_update_sysctrl(unsigned long n) {
  //  iprintf(">>>>>>>>>>>> set sys %x, eth is %s\n", n, (n&TOS_CONTROL_ETHERNET)?"on":"off");

  config.system_ctrl = n;
  mist_set_control(config.system_ctrl);
}

static void nice_name(char *dest, char *src) {
  char *c;

  // copy and append nul
  strncpy(dest, src, 8);
  for(c=dest+7;*c==' ';c--); c++;
  *c++ = '.';
  strncpy(c, src+8, 3);
  for(c+=2;*c==' ';c--); c++;
  *c++='\0';
}

static char buffer[17];  // local buffer to assemble file name (8+3+2)

char *tos_get_disk_name(char index) {
  fileTYPE file;
  char *c;

  if(index <= 1) 
    file = fdd_image[index].file;
  else
    file = hdd_image[index-2];
  
  if(!file.size) {
    strcpy(buffer, "* no disk *");
    return buffer;
  }
  
  nice_name(buffer, file.name);

  return buffer;
}

char *tos_get_image_name() {
  nice_name(buffer, config.tos_img);
  return buffer;
}

char *tos_get_cartridge_name() {
  if(!config.cart_img[0])  // no cart name set
    strcpy(buffer, "* no cartridge *");
  else
    nice_name(buffer, config.cart_img);

  return buffer;
}

char tos_disk_is_inserted(char index) {
  if(index <= 1) 
    return (fdd_image[index].file.size != 0);

  return hdd_image[index-2].size != 0;
}

void tos_select_hdd_image(char i, fileTYPE *file) {
  tos_debugf("Select ACSI%c image %11s", '0'+i, file->name);

  if(file) memcpy(config.acsi_img[i], file->name, 12);
  else     config.acsi_img[i][0] = 0;

  // try to open harddisk image
  hdd_image[i].size = 0;
  config.system_ctrl &= ~(TOS_ACSI0_ENABLE<<i);

  if(file) {
    config.system_ctrl |= (TOS_ACSI0_ENABLE<<i);
    hdd_image[i] = *file;
  }

  // update system control
  mist_set_control(config.system_ctrl);
}

void tos_insert_disk(char i, fileTYPE *file) {
  if(i > 1) {
    tos_select_hdd_image(i-2, file);
    return;
  }

  tos_debugf("%c: eject", i+'A');

  // toggle write protect bit to help tos detect a media change
  int wp_bit = (!i)?TOS_CONTROL_FDC_WR_PROT_A:TOS_CONTROL_FDC_WR_PROT_B;

  // any disk ejected is "write protected" (as nothing covers the write protect mechanism)
  mist_set_control(config.system_ctrl | wp_bit);

  // first "eject" disk
  fdd_image[i].file.size = 0;
  fdd_image[i].sides = 1;
  fdd_image[i].spt = 0;

  // no new disk given?
  if(!file) return;

  // open floppy
  fdd_image[i].file = *file;
  tos_debugf("%c: insert %.11s", i+'A', fdd_image[i].file.name);

  // check image size and parameters
    
  // check if image size suggests it's a two sided disk
  if(fdd_image[i].file.size > 80*9*512)
    fdd_image[i].sides = 2;
    
  // try common sector/track values
  int m, s, t;
  for(m=0;m<=2;m++)  // multiplier for hd/ed disks
    for(s=9;s<=12;s++)
      for(t=80;t<=85;t++)
	if(512*(1<<m)*s*t*fdd_image[i].sides == fdd_image[i].file.size)
	  fdd_image[i].spt = s*(1<<m);
  
  if(!fdd_image[i].spt) {
    // read first sector from disk
    if(MMC_Read(0, dma_buffer)) {
      fdd_image[i].spt = dma_buffer[24] + 256 * dma_buffer[25];
      fdd_image[i].sides = dma_buffer[26] + 256 * dma_buffer[27];
    } else
      fdd_image[i].file.size = 0;
  }

  if(fdd_image[i].file.size) {
    // restore state of write protect bit
    tos_update_sysctrl(config.system_ctrl);
    tos_debugf("%c: detected %d sides with %d sectors per track", 
	    i+'A', fdd_image[i].sides, fdd_image[i].spt);
  }
}

// force ejection of all disks (SD card has been removed)
void tos_eject_all() {
  int i;
  for(i=0;i<2;i++) 
    tos_insert_disk(i, NULL);

  // ejecting an SD card while a hdd image is mounted may be a bad idea
  for(i=0;i<2;i++) {
    if(hdd_direct)
      hdd_direct = 0;

    if(hdd_image[i].size) {
      InfoMessage("Card removed:\nDisabling Harddisk!");
      hdd_image[i].size = 0;
    }
  }
}

void tos_reset(char cold) {
  ikbd_reset();

  tos_update_sysctrl(config.system_ctrl |  TOS_CONTROL_CPU_RESET);  // set reset

  if(cold) {
#if 0 // clearing mem should be sifficient. But currently we upload TOS as it may be damaged
    // clear first 16k
    mist_memory_set_address(8);
    mist_memory_set(0x00, 8192-4);
#else
    tos_upload(NULL);
#endif
  }

  tos_update_sysctrl(config.system_ctrl & ~TOS_CONTROL_CPU_RESET);  // release reset
}

unsigned long tos_system_ctrl(void) {
  return config.system_ctrl;
}

void tos_config_init(void) {
  fileTYPE file;

  // set default values
  config.system_ctrl = TOS_MEMCONFIG_4M | TOS_CONTROL_BLITTER;
  memcpy(config.tos_img, "TOS     IMG", 12);
  config.cart_img[0] = 0;
  memcpy(config.acsi_img[0], "HARDDISKHD ", 12);
  config.acsi_img[1][0] = 0;
  config.video_adjust[0] = config.video_adjust[1] = 0;
  config.cdc_control_redirect = CDC_REDIRECT_NONE;

  // try to load config
  if (FileOpen(&file, CONFIG_FILENAME))  {
    tos_debugf("Configuration file size: %lu (should be %lu)", 
	       file.size, sizeof(tos_config_t));
    if(file.size == sizeof(tos_config_t)) {
      FileRead(&file, sector_buffer);
      memcpy(&config, sector_buffer, sizeof(tos_config_t));
    }
  }

  // ethernet is auto detected later
  config.system_ctrl &= ~TOS_CONTROL_ETHERNET;
}

// save configuration
void tos_config_save(void) {
  fileTYPE file;

  // save configuration data
  if (FileOpen(&file, CONFIG_FILENAME))  {
    tos_debugf("Existing conf file size: %lu", file.size);
    if(file.size != sizeof(tos_config_t)) {
      file.size = sizeof(tos_config_t);
      if (!UpdateEntry(&file))
	return;
    }
  } else {
    tos_debugf("Creating new config");
    strncpy(file.name, CONFIG_FILENAME, 11);
    file.attributes = 0;
    file.size = sizeof(tos_config_t);
    if(!FileCreate(0, &file)) {
      tos_debugf("File creation failed.");
      return;
    }
  }

  // finally write the config
  memcpy(sector_buffer, &config, sizeof(tos_config_t));
  FileWrite(&file, sector_buffer);
}
