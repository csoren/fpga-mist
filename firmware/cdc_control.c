/*
  cdc_control.c

*/

#include "cdc_enumerate.h"
#include "cdc_control.h"
#include "hardware.h"
#include "user_io.h"
#include "tos.h"
#include "debug.h"

// if cdc itself is to be debugged the debug output cannot be redirected to USB
#ifndef CDC_DEBUG
char cdc_control_debug = 0;
#endif

char cdc_control_rs232_redirect = 0;

extern const char version[];

void cdc_control_open(void) {
  iprintf("CDC control open\n");

  usb_cdc_open();
}

void cdc_control_tx(char c, char flush) {
  static char buffer[32];
  static unsigned char fill = 0;

  if(c)
    buffer[fill++] = c;

  if(fill && ((fill == sizeof(buffer)) || flush)) {
    usb_cdc_write(buffer, fill);
    fill = 0;
  }
}

static void cdc_puts(char *str) {
  unsigned char i=0;
  
  while(*str) {
    if(*str == '\n')
      cdc_control_tx('\r', 0);

    cdc_control_tx(*str++, 0);
  }

  cdc_control_tx('\r', 0);
  cdc_control_tx('\n', 1);
}

void cdc_control_poll(void) {
  // low level usb handling happens inside usb_cdc_poll
  if(usb_cdc_poll()) {
    char key;

    // check for user input
    if(usb_cdc_read(&key, 1)) {

      if(cdc_control_rs232_redirect)
	user_io_serial_tx(key);
      else {
	// force lower case
	if((key >= 'A') && (key <= 'Z'))
	  key = key - 'A' + 'a';
	
	switch(key) {
	case '\r':
	  cdc_puts("\n\033[7m <<< MIST board controller >>> \033[0m");
	  cdc_puts("Firmware version ATH" VDATE);
	  cdc_puts("Commands:");
	  cdc_puts("\033[7mR\033[0meset");
	  cdc_puts("\033[7mC\033[0moldreset");
#ifndef CDC_DEBUG
	  cdc_puts("\033[7mD\033[0mebug");
#endif
	  cdc_puts("R\033[7mS\033[0m232 redirect");
	  cdc_puts("");
	  break;
	  
	case 'r':
	  cdc_puts("Reset ...");
	  tos_reset(0);
	  break;
	  
      case 'c':
	cdc_puts("Coldreset ...");
	tos_reset(1);
	break;
	
#ifndef CDC_DEBUG
	case 'd':
	  cdc_puts("Debug enabled");
	  cdc_control_debug = 1;
	  break;
#endif
	  
	case 's':
	  cdc_puts("RS232 redirect enabled");
	  cdc_control_rs232_redirect = 1;
	  break;
	}
      }
    }
  }
}
