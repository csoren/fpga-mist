#ifndef OSD_H_INCLUDED
#define OSD_H_INCLUDED

/*constants*/
#define OSDCTRLUP        0x01        /*OSD up control*/
#define OSDCTRLDOWN      0x02        /*OSD down control*/
#define OSDCTRLSELECT    0x04        /*OSD select control*/
#define OSDCTRLMENU      0x08        /*OSD menu control*/
#define OSDCTRLRIGHT     0x10        /*OSD right control*/
#define OSDCTRLLEFT      0x20        /*OSD left control*/

// some constants
#define OSDNLINE         8           // number of lines of OSD
#define OSDLINELEN       256         // single line length in bytes

// ---- old Minimig v1 constants -------
#define MM1_OSDCMDREAD     0x00      // OSD read controller/key status
#define MM1_OSDCMDWRITE    0x20      // OSD write video data command
#define MM1_OSDCMDENABLE   0x41      // OSD enable command
#define MM1_OSDCMDDISABLE  0x40      // OSD disable command
#define MM1_OSDCMDRST      0x80      // OSD reset command
#define MM1_OSDCMDAUTOFIRE 0x84      // OSD autofire command
#define MM1_OSDCMDCFGSCL   0xA0      // OSD settings: scanlines effect
#define MM1_OSDCMDCFGIDE   0xB0      // OSD enable HDD command
#define MM1_OSDCMDCFGFLP   0xC0      // OSD settings: floppy config
#define MM1_OSDCMDCFGCHP   0xD0      // OSD settings: chipset config
#define MM1_OSDCMDCFGFLT   0xE0      // OSD settings: filter
#define MM1_OSDCMDCFGMEM   0xF0      // OSD settings: memory config
#define MM1_OSDCMDCFGCPU   0xFC      // OSD settings: CPU config

// ---- new Minimig v2 constants -------
#define OSD_CMD_READ      0x00
#define OSD_CMD_RST       0x08
#define OSD_CMD_CLK       0x18
#define OSD_CMD_OSD       0x28
#define OSD_CMD_CHIP      0x04
#define OSD_CMD_CPU       0x14
#define OSD_CMD_MEM       0x24
#define OSD_CMD_VID       0x34
#define OSD_CMD_FLP       0x44
#define OSD_CMD_HDD       0x54
#define OSD_CMD_JOY       0x64
#define OSD_CMD_OSD_WR    0x0c
#define OSD_CMD_WR        0x1c

#define DISABLE_KEYBOARD 0x02        // disable keyboard while OSD is active

#define REPEATDELAY      500         // repeat delay in 1ms units
#define REPEATRATE       50          // repeat rate in 1ms units
#define BUTTONDELAY      20          // repeat rate in 1ms units

#define KEY_UPSTROKE     0x80
#define KEY_MENU         0x69
#define KEY_PGUP         0x6C
#define KEY_PGDN         0x6D
#define KEY_HOME         0x6A
#define KEY_ESC          0x45
#define KEY_KPENTER      0x43
#define KEY_ENTER        0x44
#define KEY_BACK         0x41
#define KEY_SPACE        0x40
#define KEY_UP           0x4C
#define KEY_DOWN         0x4D
#define KEY_LEFT         0x4F
#define KEY_RIGHT        0x4E
#define KEY_F1           0x50
#define KEY_F2           0x51
#define KEY_F3           0x52
#define KEY_F4           0x53
#define KEY_F5           0x54
#define KEY_F6           0x55
#define KEY_F7           0x56
#define KEY_F8           0x57
#define KEY_F9           0x58
#define KEY_F10          0x59
#define KEY_CTRL         0x63
#define KEY_LALT         0x64
#define KEY_KPPLUS       0x5E
#define KEY_KPMINUS      0x4A
#define KEY_KP0          0x0F

#define CONFIG_TURBO     1
#define CONFIG_NTSC      2
#define CONFIG_A1000     4
#define CONFIG_ECS       8
#define CONFIG_AGA      16

#define CONFIG_FLOPPY1X  0
#define CONFIG_FLOPPY2X  1

#define RESET_NORMAL 0
#define RESET_BOOTLOADER 1

#define OSD_ARROW_LEFT 1
#define OSD_ARROW_RIGHT 2

/*functions*/
void OsdSetTitle(char *s,int arrow);	// arrow > 0 = display right arrow in bottom right, < 0 = display left arrow
void OsdWrite(unsigned char n, char *s, unsigned char inver, unsigned char stipple);
void OsdWriteOffset(unsigned char n, char *s, unsigned char inver, unsigned char stipple, char offset); // Used for scrolling "Exit" text downwards...
void OsdClear(void);
void OsdEnable(unsigned char mode);
void OsdDisable(void);
void OsdWaitVBL(void);
void OsdReset(unsigned char boot);
void ConfigFilter(unsigned char lores, unsigned char hires);
void OsdReconfig(); // Reset to Chameleon core.
// deprecated functions from Minimig 1
void MM1_ConfigFilter(unsigned char lores, unsigned char hires);
void MM1_ConfigScanlines(unsigned char scanlines);
void ConfigVideo(unsigned char hires, unsigned char lores, unsigned char scanlines);
void ConfigMemory(unsigned char memory);
void ConfigCPU(unsigned char cpu);
void ConfigChipset(unsigned char chipset);
void ConfigFloppy(unsigned char drives, unsigned char speed);
void ConfigIDE(unsigned char gayle, unsigned char master, unsigned char slave);
void ConfigAutofire(unsigned char autofire);
unsigned char OsdGetCtrl(void);
unsigned char GetASCIIKey(unsigned char c);
void OSD_PrintText(unsigned char line, char *text, unsigned long start, unsigned long width, unsigned long offset, unsigned char invert);
void OsdWriteDoubleSize(unsigned char n, char *s, unsigned char pass);
//void OsdDrawLogo(unsigned char n, char row);
void OsdDrawLogo(unsigned char n, char row,char superimpose);
void ScrollText(char n,const char *str, int len, int max_len,unsigned char invert);
void ScrollReset();
void StarsInit();
void StarsUpdate();

void OsdKeySet(unsigned char);
unsigned char OsdKeyGet();

#endif

