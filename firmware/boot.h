// boot.h
// bootscreen functions
// 2014, rok.krajnc@gmail.com


#ifndef __BOOT_H__
#define __BOOT_H__


//// defines ////
#define SCREEN_WIDTH    640
#define SCREEN_HEIGHT   256
#define SCREEN_SIZE     SCREEN_WIDTH * SCREEN_HEIGHT
#define SCREEN_MEM_SIZE 2*SCREEN_SIZE/8
#define SCREEN_ADDRESS  0x80000
#define SCREEN_BPL1     0x80000
#define SCREEN_BPL2     0x85000


#define LOGO_WIDTH      208
#define LOGO_HEIGHT     32
#define LOGO_OFFSET     (64*SCREEN_WIDTH/8+24)
#define LOGO_LSKIP      (SCREEN_WIDTH-LOGO_WIDTH)/8
#define LOGO_SIZE       0x680
#define LOGO_FILE       "MINIMIG ART"

#define BALL_SIZE       0x4000
#define BALL_ADDRESS    0x8a000
#define BALL_FILE       "MINIMIG BAL"

#define COPPER_SIZE     0x35c
#define COPPER_ADDRESS  0x8e680
#define COPPER_FILE     "MINIMIG COP"

#define BLITS           64

#define MEM_UPLOAD_INIT(adr) {typeof(adr) _adr = (adr); \
                              EnableOsd(); \
                              SPI(OSD_CMD_WR); \
                              SPI(_adr&0xff); _adr = _adr>>8; \
                              SPI(_adr&0xff); _adr = _adr>>8; \
                              SPI(_adr&0xff); _adr = _adr>>8; \
                              SPI(_adr&0xff); _adr = _adr>>8; \
                             }
#define MEM_UPLOAD_FINI()   {DisableOsd(); SPIN(); SPIN(); SPIN(); SPIN();}
#define MEM_WRITE16(x)      {SPI((((x)>>8)&0xff)); SPI(((x)&0xff)); SPIN(); SPIN(); SPIN(); SPIN();}


//// functions ////
void BootInit();
void BootPrintEx(char * str);

extern int bootscreen_adr;


#endif // __BOOT_H__

