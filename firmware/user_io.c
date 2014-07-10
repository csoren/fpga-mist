#include "AT91SAM7S256.h"
#include <stdio.h>
#include <string.h>
#include "hardware.h"
#include "osd.h"

#include "user_io.h"
#include "cdc_control.h"
#include "usb.h"
#include "debug.h"
#include "keycodes.h"
#include "ikbd.h"
#include "fat.h"

#define BREAK  0x8000

extern fileTYPE file;
extern char s[40];

typedef enum { EMU_NONE, EMU_MOUSE, EMU_JOY0, EMU_JOY1 } emu_mode_t;
static emu_mode_t emu_mode = EMU_NONE;
static unsigned char emu_state = 0;
static long emu_timer;
#define EMU_MOUSE_FREQ 5

static unsigned char core_type = CORE_TYPE_UNKNOWN;
static unsigned char adc_state = 0;

AT91PS_ADC a_pADC = AT91C_BASE_ADC;
AT91PS_PMC a_pPMC = AT91C_BASE_PMC;

static char caps_lock_toggle = 0;

static void PollOneAdc() {
  static unsigned char adc_cnt = 0xff;

  // fetch result from previous run
  if(adc_cnt != 0xff) {
    unsigned int result;

    // wait for end of convertion
    while(!(AT91C_BASE_ADC->ADC_SR & (1 << (4+adc_cnt))));
    
    switch (adc_cnt) {
    case 0: result = AT91C_BASE_ADC->ADC_CDR4; break;
    case 1: result = AT91C_BASE_ADC->ADC_CDR5; break;
    case 2: result = AT91C_BASE_ADC->ADC_CDR6; break;
    case 3: result = AT91C_BASE_ADC->ADC_CDR7; break;
    }
    
    if(result < 128) adc_state |=  (1<<adc_cnt);
    if(result > 128) adc_state &= ~(1<<adc_cnt);
  }
  
  adc_cnt = (adc_cnt + 1)&3;
  
  // Enable desired chanel
  AT91C_BASE_ADC->ADC_CHER = 1 << (4+adc_cnt);
  
  // Start conversion
  AT91C_BASE_ADC->ADC_CR = AT91C_ADC_START;
}

static void InitADC(void) {
  // Enable clock for interface
  AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_ADC;

  // Reset
  AT91C_BASE_ADC->ADC_CR = AT91C_ADC_SWRST;
  AT91C_BASE_ADC->ADC_CR = 0x0;

  // Set maximum startup time and hold time
  AT91C_BASE_ADC->ADC_MR = 0x0F1F0F00 | AT91C_ADC_LOWRES_8_BIT;

  // make sure we get the first values immediately
  PollOneAdc();
  PollOneAdc();
  PollOneAdc();
  PollOneAdc();
}

// poll one adc channel every 25ms
static void PollAdc() {
  static long adc_timer = 0;

  if(CheckTimer(adc_timer)) {
    adc_timer = GetTimer(25);
    PollOneAdc();
  }
}

void user_io_init() {
  InitADC();

  ikbd_init();
}

unsigned char user_io_core_type() {
  return core_type;
}

char user_io_create_config_name(char *s) {
  char *p = user_io_8bit_get_string(0);  // get core name
  if(p && p[0]) {
    strcpy(s, p);
    while(strlen(s) < 8) strcat(s, " ");
    strcat(s, "CFG");
    
    return 0;
  }
  return 1;
}

extern DIRENTRY DirEntry[MAXDIRENTRIES];
extern unsigned char sort_table[MAXDIRENTRIES];
extern unsigned char nDirEntries;

void user_io_detect_core_type() {
  EnableIO();
  core_type = SPI(0xff);
  DisableIO();

  if((core_type != CORE_TYPE_DUMB) &&
     (core_type != CORE_TYPE_MINIMIG) &&
     (core_type != CORE_TYPE_PACE) &&
     (core_type != CORE_TYPE_MIST) &&
     (core_type != CORE_TYPE_8BIT))
    core_type = CORE_TYPE_UNKNOWN;

  switch(core_type) {
  case CORE_TYPE_UNKNOWN:
    puts("Unable to identify core!");
    break;
    
  case CORE_TYPE_DUMB:
    puts("Identified core without user interface");
    break;
    
  case CORE_TYPE_MINIMIG:
    puts("Identified Minimig core");
    break;
    
  case CORE_TYPE_PACE:
    puts("Identified PACE core");
    break;
    
  case CORE_TYPE_MIST:
    puts("Identified MiST core");
    break;

  case CORE_TYPE_8BIT: {
    puts("Identified 8BIT core");

    // send a reset
    user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);

    // try to load config
    user_io_create_config_name(s);
    iprintf("Loading config %s\n", s);

    if (FileOpen(&file, s))  {
      iprintf("Found config\n");
      if(file.size == 1) {
	FileRead(&file, sector_buffer);
	user_io_8bit_set_status(sector_buffer[0], 0xff);
      }
    }

    // release reset
    user_io_8bit_set_status(0, UIO_STATUS_RESET);

  } break;
  }
}

void user_io_joystick(unsigned char joystick, unsigned char map) {
  //  iprintf("j%d: %x\n", joystick, map);

  // "only" 6 joysticks are supported
  if(joystick >= 6)
    return;

  // mist cores process joystick events for joystick 0 and 1 via the 
  // ikbd
  if((core_type == CORE_TYPE_MINIMIG) || 
     (core_type == CORE_TYPE_PACE)  || 
     ((core_type == CORE_TYPE_MIST) && (joystick >= 2))  || 
     (core_type == CORE_TYPE_8BIT)) {
    // joystick 3 and 4 were introduced later
    EnableIO();
    SPI((joystick < 2)?(UIO_JOYSTICK0 + joystick):((UIO_JOYSTICK3 + joystick - 2)));
    SPI(map);
    DisableIO();
  }

  // atari ST handles joystick 0 and 1 through the ikbd emulated by the io controller
  if((core_type == CORE_TYPE_MIST) && (joystick < 2))
    ikbd_joystick(joystick, map);
}

// transmit serial/rs232 data into core
void user_io_serial_tx(char *chr, uint16_t cnt) {
  EnableIO();
  SPI(UIO_SERIAL_OUT);
  while(cnt--) SPI(*chr++);
  DisableIO();
}
  
// transmit midi data into core
void user_io_midi_tx(char chr) {
  EnableIO();
  SPI(UIO_MIDI_OUT);
  SPI(chr);
  DisableIO();
}

// send ethernet mac address into FPGA
void user_io_eth_send_mac(uint8_t *mac) {
  uint8_t i;

  EnableIO();
  SPI(UIO_ETH_MAC);
  for(i=0;i<6;i++) SPI(*mac++);
  DisableIO();
}

// read 32 bit ethernet status word from FPGA
uint32_t user_io_eth_get_status(void) {
  uint32_t s;

  EnableIO();
  SPI(UIO_ETH_STATUS);
  s = SPI(0);
  s = (s<<8) | SPI(0);
  s = (s<<8) | SPI(0);
  s = (s<<8) | SPI(0);
  DisableIO();

  return s;
}

// read ethernet frame from FPGAs ethernet tx buffer
void user_io_eth_receive_tx_frame(uint8_t *d, uint16_t len) {
  EnableIO();
  SPI(UIO_ETH_FRM_IN);
  while(len--) *d++=SPI(0);
  DisableIO();
}

// write ethernet frame to FPGAs rx buffer
void user_io_eth_send_rx_frame(uint8_t *s, uint16_t len) {
  EnableIO();
  SPI(UIO_ETH_FRM_OUT);
  SPI_write(s, len);
  SPI(0);     // one additional byte to allow fpga to store the previous one
  DisableIO();
}

// the physical joysticks (db9 ports at the right device side)
// as well as the joystick emulation are renumbered if usb joysticks
// are present in the system. The USB joystick(s) replace joystick 1
// and 0 and the physical joysticks are "shifted up". 
//
// Since the primary joystick is in port 1 the first usb joystick 
// becomes joystick 1 and only the second one becomes joystick 0
// (mouse port)

static uint8_t joystick_renumber(uint8_t j) {
  uint8_t usb_sticks = hid_get_joysticks();

  // no usb sticks present: no changes are being made
  if(!usb_sticks) return j;

  if(j == 0) {
    // if usb joysticks are present, then physical joystick 0 (mouse port)
    // becomes becomes 2,3,...
    j = usb_sticks + 1;
  } else {
    // if one usb joystick is present, then physical joystick 1 (joystick port)
    // becomes physical joystick 0 (mouse) port. If more than 1 usb joystick
    // is present it becomes 2,3,...
    if(usb_sticks == 1) j = 0;
    else                j = usb_sticks;
  }
  return j;
}

// 16 byte fifo for amiga key codes to limit max key rate sent into the core
#define KBD_FIFO_SIZE  16   // must be power of 2
static unsigned short kbd_fifo[KBD_FIFO_SIZE];
static unsigned char kbd_fifo_r=0, kbd_fifo_w=0;
static long kbd_timer = 0;

static void kbd_fifo_minimig_send(unsigned short code) {
  EnableIO();
  if(code & OSD) SPI(UIO_KBD_OSD);   // code for OSD
  else           SPI(UIO_KEYBOARD);
  SPI(code & 0xff);
  DisableIO();

  kbd_timer = GetTimer(10);  // next key after 10ms earliest
}

static void kbd_fifo_enqueue(unsigned short code) {
  // if fifo full just drop the value. This should never happen
  if(((kbd_fifo_w+1)&(KBD_FIFO_SIZE-1)) == kbd_fifo_r)
    return;

  // store in queue
  kbd_fifo[kbd_fifo_w] = code;
  kbd_fifo_w = (kbd_fifo_w + 1)&(KBD_FIFO_SIZE-1);
}

// send pending bytes if timer has run up
static void kbd_fifo_poll() {
  // timer enabled and runnig?
  if(kbd_timer && !CheckTimer(kbd_timer))
    return;
 
  kbd_timer = 0;  // timer == 0 means timer is not running anymore

  if(kbd_fifo_w == kbd_fifo_r)
    return;

  kbd_fifo_minimig_send(kbd_fifo[kbd_fifo_r]);
  kbd_fifo_r = (kbd_fifo_r + 1)&(KBD_FIFO_SIZE-1);
}

void user_io_file_tx(fileTYPE *file) {
  unsigned long bytes2send = file->size;

  /* transmit the entire file using one transfer */

  iprintf("Selected file %s with %lu bytes to send\n", file->name, bytes2send);

  // prepare transmission of new file
  EnableFpga();
  SPI(UIO_FILE_TX);
  SPI(0xff);
  DisableFpga();

  while(bytes2send) {
    iprintf(".");

    unsigned short c, chunk = (bytes2send>512)?512:bytes2send;
    char *p;

    FileRead(file, sector_buffer);

    EnableFpga();
    SPI(UIO_FILE_TX_DAT);

    for(p = sector_buffer, c=0;c < chunk;c++)
      SPI(*p++);

    DisableFpga();
    
    bytes2send -= chunk;

    // still bytes to send? read next sector
    if(bytes2send)
      FileNextSector(file);
  }

  // signal end of transmission
  EnableFpga();
  SPI(UIO_FILE_TX);
  SPI(0x00);
  DisableFpga();

  iprintf("\n");
}

// 8 bit cores have a config string telling the firmware how
// to treat it
char *user_io_8bit_get_string(char index) {
  unsigned char i, lidx = 0, j = 0;
  static char buffer[32+1];  // max 32 bytes per config item

  // clear buffer
  buffer[0] = 0;

  EnableIO();
  SPI(UIO_GET_STRING);
  
  i = SPI(0);
  // the first char returned will be 0xff if the core doesn't support
  // config strings. atari 800 returns 0xa4 which is the status byte
  if((i == 0xff) || (i == 0xa4)) {
    DisableIO();
    return NULL;
  }

  //  iprintf("String: ");

  while ((i != 0) && (i!=0xff) && (j<sizeof(buffer))) {
    if(i == ';') {
      if(lidx == index) buffer[j++] = 0;
      lidx++;
    } else {
      if(lidx == index)
	buffer[j++] = i;
    }

    //    iprintf("%c", i);
    i = SPI(0);
  }
    
  DisableIO();
  //  iprintf("\n");

  // if this was the last string in the config string list, then it still
  // needs to be terminated
  if(lidx == index)
    buffer[j] = 0;

  // also return NULL for empty strings
  if(!buffer[0]) 
    return NULL;

  return buffer;
}    

unsigned char user_io_8bit_set_status(unsigned char new_status, unsigned char mask) {
  static unsigned char status = 0;

  // if mask is 0 just return the current status 
  if(mask) {
    // keep everything not masked
    status &= ~mask;
    // updated masked bits
    status |= new_status & mask;
    
    EnableIO();
    SPI(UIO_SET_STATUS);
    SPI(status);
    DisableIO();
  }

  return status;
}

void user_io_poll() {
  if((core_type != CORE_TYPE_MINIMIG) &&
     (core_type != CORE_TYPE_PACE) &&
     (core_type != CORE_TYPE_MIST) &&
     (core_type != CORE_TYPE_8BIT)) {
    return;  // no user io for the installed core
  }

  if(core_type == CORE_TYPE_MIST) {
    char redirect = tos_get_cdc_control_redirect();

    ikbd_poll();

    // check for input data on usart
    USART_Poll();
      
    unsigned char c = 0;

    // check for incoming serial data. this is directly forwarded to the
    // arm rs232 and mixes with debug output. Useful for debugging only of
    // e.g. the diagnostic cartridge
    EnableIO();
    SPI(UIO_SERIAL_IN);
    // character 0xff is returned if FPGA isn't configured
    while(SPI(0) && (c!= 0xff)) {
      c = SPI(0);
      putchar(c);
      
      // forward to USB if redirection via USB/CDC enabled
      if(redirect == CDC_REDIRECT_RS232)
	cdc_control_tx(c);
    }
    DisableIO();
    
    // check for incoming parallel/midi data
    if((redirect == CDC_REDIRECT_PARALLEL) || (redirect == CDC_REDIRECT_MIDI)) {
      EnableIO();
      SPI((redirect == CDC_REDIRECT_PARALLEL)?UIO_PARALLEL_IN:UIO_MIDI_IN);
      // character 0xff is returned if FPGA isn't configured
      c = 0;
      while(SPI(0) && (c!= 0xff)) {
	c = SPI(0);
	cdc_control_tx(c);
      }
      DisableIO();
      
      // always flush when doing midi to reduce latencies
      if(redirect == CDC_REDIRECT_MIDI)
	cdc_control_flush();
    }
  }

  // poll db9 joysticks
  static int joy0_state = JOY0;
  if((*AT91C_PIOA_PDSR & JOY0) != joy0_state) {
    joy0_state = *AT91C_PIOA_PDSR & JOY0;
    
    unsigned char joy_map = 0;
    if(!(joy0_state & JOY0_UP))    joy_map |= JOY_UP;
    if(!(joy0_state & JOY0_DOWN))  joy_map |= JOY_DOWN;
    if(!(joy0_state & JOY0_LEFT))  joy_map |= JOY_LEFT;
    if(!(joy0_state & JOY0_RIGHT)) joy_map |= JOY_RIGHT;
    if(!(joy0_state & JOY0_BTN1))  joy_map |= JOY_BTN1;
    if(!(joy0_state & JOY0_BTN2))  joy_map |= JOY_BTN2;

    user_io_joystick(joystick_renumber(0), joy_map);
  }
  
  static int joy1_state = JOY1;
  if((*AT91C_PIOA_PDSR & JOY1) != joy1_state) {
    joy1_state = *AT91C_PIOA_PDSR & JOY1;
    
    unsigned char joy_map = 0;
    if(!(joy1_state & JOY1_UP))    joy_map |= JOY_UP;
    if(!(joy1_state & JOY1_DOWN))  joy_map |= JOY_DOWN;
    if(!(joy1_state & JOY1_LEFT))  joy_map |= JOY_LEFT;
    if(!(joy1_state & JOY1_RIGHT)) joy_map |= JOY_RIGHT;
    if(!(joy1_state & JOY1_BTN1))  joy_map |= JOY_BTN1;
    if(!(joy1_state & JOY1_BTN2))  joy_map |= JOY_BTN2;
    
    user_io_joystick(joystick_renumber(1), joy_map);
  }

  // frequently poll the adc the switches 
  // and buttons are connected to
  PollAdc();
  
  static unsigned char key_map = 0;
  unsigned char map = 0;
  if(adc_state & 1) map |= SWITCH2;
  if(adc_state & 2) map |= SWITCH1;

  if(adc_state & 4) map |= BUTTON1;
  if(adc_state & 8) map |= BUTTON2;
  
  if(map != key_map) {
    key_map = map;
    
    EnableIO();
    SPI(UIO_BUT_SW);
    SPI(map);
    DisableIO();
  }

  // mouse movement emulation is continous 
  if(emu_mode == EMU_MOUSE) {
    if(CheckTimer(emu_timer)) {
      emu_timer = GetTimer(EMU_MOUSE_FREQ);
      
      if(emu_state & JOY_MOVE) {
	unsigned char b = 0;
	char x = 0, y = 0;
	if((emu_state & (JOY_LEFT | JOY_RIGHT)) == JOY_LEFT)  x = -1; 
	if((emu_state & (JOY_LEFT | JOY_RIGHT)) == JOY_RIGHT) x = +1; 
	if((emu_state & (JOY_UP   | JOY_DOWN))  == JOY_UP)    y = -1; 
	if((emu_state & (JOY_UP   | JOY_DOWN))  == JOY_DOWN)  y = +1; 
	
	if(emu_state & JOY_BTN1) b |= 1;
	if(emu_state & JOY_BTN2) b |= 2;
	
	user_io_mouse(b, x, y);
      }
    }
  }

  if(core_type == CORE_TYPE_MINIMIG) {
    kbd_fifo_poll();
  }

  if(core_type == CORE_TYPE_MIST) {
    // do some tos specific monitoring here
    tos_poll();
  }

  if(core_type == CORE_TYPE_8BIT) {
    // raw sector io for cores like the atari800 core which include a full
    // file system driver usually implemented using a second cpu
    static unsigned long bit8_status = 0;
    unsigned long status;

    /* read status byte */
    EnableFpga();
    SPI(UIO_GET_STATUS);
    status = SPI(0);
    status = (status << 8) | SPI(0);
    status = (status << 8) | SPI(0);
    status = (status << 8) | SPI(0);
    DisableFpga();

    if(status != bit8_status) {
      unsigned long sector = (status>>8)&0xffffff;
      char buffer[512];

      bit8_status = status;
      
      // sector read testing 
      DISKLED_ON;

      // sector read
      if((status & 0xff) == 0xa5) {
	if(MMC_Read(sector, buffer)) {
	  // data is now stored in buffer. send it to fpga
	  EnableFpga();
	  SPI(UIO_SECTOR_SND);     // send sector data IO->FPGA
	  SPI_block_write(buffer);
	  DisableFpga();
	} else
	  bit8_debugf("rd %ld fail", sector);
      }

      // sector write
      if((status & 0xff) == 0xa6) {

	// read sector from FPGA
	EnableFpga();
	SPI(UIO_SECTOR_RCV);     // receive sector data FPGA->IO
	SPI_block_read(buffer);
	DisableFpga();

	if(!MMC_Write(sector, buffer)) 
	  bit8_debugf("wr %ld fail", sector);
      }

      DISKLED_OFF;
    }
  }
}

char user_io_dip_switch1() {
  return((adc_state & 2)?1:0);
}

char user_io_menu_button() {
  return((adc_state & 4)?1:0);
}

char user_io_user_button() {
  return((adc_state & 8)?1:0);
}

static void send_keycode(unsigned short code) {
  if(core_type == CORE_TYPE_MINIMIG) {
    // amiga has "break" marker in msb
    if(code & BREAK) code = (code & 0xff) | 0x80;

    // send immediately if possible
    if(CheckTimer(kbd_timer) &&(kbd_fifo_w == kbd_fifo_r) )
      kbd_fifo_minimig_send(code);
    else
      kbd_fifo_enqueue(code);
  }

  if(core_type == CORE_TYPE_MIST) {
    // atari has "break" marker in msb
    if(code & BREAK) code = (code & 0xff) | 0x80;

    ikbd_keyboard(code);
  }

  if(core_type == CORE_TYPE_8BIT) {
    // send ps2 keycodes for those cores that prefer ps2
    EnableIO();
    SPI(UIO_KEYBOARD);

    // "pause" has a complex code 
    if((code&0xff) == 0x77) {

      // pause does not have a break code
      if(!(code & BREAK)) {

	// Pause key sends E11477E1F014E077
	static const unsigned char c[] = { 0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77, 0x00 };
	const unsigned char *p = c;
	
	iprintf("TX PS2 ");
	while(*p) {
	  iprintf("%x ", *p);
	  SPI(*p++);
	}
	iprintf("\n");
      }
    } else {
      iprintf("TX PS2 ");
      if(code & EXT)   iprintf("e0 ");
      if(code & BREAK) iprintf("f0 ");
      iprintf("%x\n", code & 0xff);
      
      if(code & EXT)    // prepend extended code flag if required
	SPI(0xe0);
      
      if(code & BREAK)  // prepend break code if required
	SPI(0xf0);
      
      SPI(code & 0xff);  // send code itself
    }

    DisableIO();
  }
}

void user_io_mouse(unsigned char b, char x, char y) {

  // send mouse data as minimig expects it
  if((core_type == CORE_TYPE_MINIMIG) || 
     (core_type == CORE_TYPE_8BIT)) {
    EnableIO();
    SPI(UIO_MOUSE);
    SPI(x);
    SPI(y);
    SPI(b);
    DisableIO();
  }

  // send mouse data as mist expects it
  if(core_type == CORE_TYPE_MIST)
    ikbd_mouse(b, x, y);
}

// check if this is a key that's supposed to be suppressed
// when emulation is active
static unsigned char is_emu_key(unsigned char c) {
  static const unsigned char m[] = { JOY_RIGHT, JOY_LEFT, JOY_DOWN, JOY_UP };

  if(emu_mode == EMU_NONE)
    return 0;

  // direction keys R/L/D/U
  if(c >= 0x4f && c <= 0x52)
    return m[c-0x4f];

  return 0;
}  

#define EMU_BTN1  0  // left control
#define EMU_BTN2  4  // right control

unsigned short keycode(unsigned char in) {
  if(core_type == CORE_TYPE_MINIMIG) 
    return usb2ami[in];

  // atari st and the 8 bit core (currently only used for atari 800)
  // use the same key codes
  if(core_type == CORE_TYPE_MIST)
    return usb2atari[in];

  if(core_type == CORE_TYPE_8BIT)
    return usb2ps2[in];

  return MISS;
}

void check_reset(unsigned char modifiers) {
  if(core_type==CORE_TYPE_MINIMIG) {
    if(modifiers==0x45) // ctrl - alt - alt
      OsdReset(RESET_NORMAL);
  }
}

unsigned short modifier_keycode(unsigned char index) {
  /* usb modifer bits: 
        0     1     2    3    4     5     6    7
      LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
  */

  if(core_type == CORE_TYPE_MINIMIG) {
    static const unsigned short amiga_modifier[] = 
      { 0x63, 0x60, 0x64, 0x66, 0x63, 0x61, 0x65, 0x67 };
    return amiga_modifier[index];
  }

  if(core_type == CORE_TYPE_MIST) {
    static const unsigned short atari_modifier[] = 
      { 0x1d, 0x2a, 0x38, MISS, 0x1d, 0x36, 0x38, MISS };
    return atari_modifier[index];
  } 

  if(core_type == CORE_TYPE_8BIT) {
    static const unsigned short ps2_modifier[] = 
      { 0x14, 0x12, 0x11, EXT|0x1f, EXT|0x14, 0x59, EXT|0x11, EXT|0x27 };
    return ps2_modifier[index];
  } 

  return MISS;
}

// set by OSD code to suppress forwarding of those keys to the core which
// may be in use by an active OSD
static char osd_eats_keys = false;

void user_io_osd_key_enable(char on) {
  osd_eats_keys = on;
}

static char key_used_by_osd(unsigned short s) {
  // this key is only used in OSD and has no keycode
  if((s & OSD_LOC) && !(s & 0xff))  return true; 

  // no keys are suppressed if the OSD is inactive
  if(!osd_eats_keys) return false;

  // in atari mode eat all keys if the OSD is online,
  // else none as it's up to the core to forward keys
  // to the OSD
  return((core_type == CORE_TYPE_MIST) ||
	 (core_type == CORE_TYPE_8BIT));
}

void user_io_kbd(unsigned char m, unsigned char *k) {
  if((core_type == CORE_TYPE_MINIMIG) ||
     (core_type == CORE_TYPE_MIST) ||
     (core_type == CORE_TYPE_8BIT)) {

    static unsigned char modifier = 0, pressed[6] = { 0,0,0,0,0,0 };
    int i, j;
    
    // modifier keys are used as buttons in emu mode
    if(emu_mode != EMU_NONE) {
      char last_btn = emu_state & (JOY_BTN1 | JOY_BTN2);
      if(m & (1<<EMU_BTN1)) emu_state |=  JOY_BTN1;
      else                  emu_state &= ~JOY_BTN1;
      if(m & (1<<EMU_BTN2)) emu_state |=  JOY_BTN2;
      else                  emu_state &= ~JOY_BTN2;
      
      // check if state of mouse buttons has changed
      if(last_btn != (emu_state & (JOY_BTN1 | JOY_BTN2))) {
	if(emu_mode == EMU_MOUSE) {
	  unsigned char b;
	  if(emu_state & JOY_BTN1) b |= 1;
	  if(emu_state & JOY_BTN2) b |= 2;
	  user_io_mouse(b, 0, 0);
	}
	
	if(emu_mode == EMU_JOY0) 
	  user_io_joystick(joystick_renumber(0), emu_state);
	
	if(emu_mode == EMU_JOY1) 
	  user_io_joystick(joystick_renumber(1), emu_state);
      }
    }
    
    // handle modifier keys
    if(m != modifier) {
      for(i=0;i<8;i++) {
	// Do we have a downstroke on a modifier key?
	if((m & (1<<i)) && !(modifier & (1<<i))) {
	  // check for special events in modifier presses
	  check_reset(m);

	  // shift keys are used for mouse joystick emulation in emu mode
	  if(((i != EMU_BTN1) && (i != EMU_BTN2)) || (emu_mode == EMU_NONE))
	    if(modifier_keycode(i) != MISS)
	      send_keycode(modifier_keycode(i));
	}
	if(!(m & (1<<i)) && (modifier & (1<<i)))
	  if(((i != EMU_BTN1) && (i != EMU_BTN2)) || (emu_mode == EMU_NONE))
	    if(modifier_keycode(i) != MISS)
	      send_keycode(BREAK | modifier_keycode(i));
      }
      
      modifier = m;
    }
    
    // check if there are keys in the pressed list which aren't 
    // reported anymore
    for(i=0;i<6;i++) {
      unsigned short code = keycode(pressed[i]);
      
      if(pressed[i] && code != MISS) {
	for(j=0;j<6 && pressed[i] != k[j];j++);
	
	// don't send break for caps lock
	if(j == 6) {
	  // special OSD key handled internally 
	  OsdKeySet(0x80 | usb2ami[pressed[i]]);

	  if(!key_used_by_osd(code)) {
	    if(is_emu_key(pressed[i])) {
	      emu_state &= ~is_emu_key(pressed[i]);
	    
	      if(emu_mode == EMU_JOY0) 
		user_io_joystick(joystick_renumber(0), emu_state);
	      
	      if(emu_mode == EMU_JOY1) 
		user_io_joystick(joystick_renumber(1), emu_state);

	    } else if(!(code & CAPS_LOCK_TOGGLE) &&
		      !(code & NUM_LOCK_TOGGLE))
	      send_keycode(BREAK | code);	
	  }
	}
      }  
    }
    
    for(i=0;i<6;i++) {
      unsigned short code = keycode(k[i]);

      if(k[i] && (k[i] <= KEYCODE_MAX) && code != MISS) {
	// check if this key is already in the list of pressed keys
	for(j=0;j<6 && k[i] != pressed[j];j++);

	if(j == 6) {
	  // special OSD key handled internally 
	  OsdKeySet(usb2ami[k[i]]); 

	  // no further processing of any key that is currently 
	  // redirected to the OSD
	  if(!key_used_by_osd(code)) {
	    if (is_emu_key(k[i])) {
	      emu_state |= is_emu_key(k[i]);

	      // joystick emulation is also affected by the presence of
	      // usb joysticks
	      if(emu_mode == EMU_JOY0) 
		user_io_joystick(joystick_renumber(0), emu_state);
	      
	      if(emu_mode == EMU_JOY1) 
		user_io_joystick(joystick_renumber(1), emu_state);

	    } else if(!(code & CAPS_LOCK_TOGGLE)&&
		      !(code & NUM_LOCK_TOGGLE)) 
	      send_keycode(code);
	    else {
	      if(code & CAPS_LOCK_TOGGLE) {
		// send alternating make and break codes for caps lock
		send_keycode((code & 0xff) | (caps_lock_toggle?BREAK:0));
		caps_lock_toggle = !caps_lock_toggle;
		
		hid_set_kbd_led(HID_LED_CAPS_LOCK, caps_lock_toggle);
	      }
	      if(code & NUM_LOCK_TOGGLE) {
		// num lock has four states indicated by leds:
		// all off: normal
		// num lock on, scroll lock on: mouse emu
		// num lock on, scroll lock off: joy0 emu
		// num lock off, scroll lock on: joy1 emu
		
		if(emu_mode == EMU_MOUSE)
		  emu_timer = GetTimer(EMU_MOUSE_FREQ);
		
		emu_mode = (emu_mode+1)&3;
		if(emu_mode == EMU_MOUSE || emu_mode == EMU_JOY0) 
		  hid_set_kbd_led(HID_LED_NUM_LOCK, true);
		else
		  hid_set_kbd_led(HID_LED_NUM_LOCK, false);
		
		if(emu_mode == EMU_MOUSE || emu_mode == EMU_JOY1) 
		  hid_set_kbd_led(HID_LED_SCROLL_LOCK, true);
		else
		  hid_set_kbd_led(HID_LED_SCROLL_LOCK, false);
	      }
	    }
	  }
	}
      }
    }
    
  for(i=0;i<6;i++) 
    pressed[i] = k[i];
  }
}
