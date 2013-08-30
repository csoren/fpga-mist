#ifndef IKBD_H
#define IKBD_H

void ikbd_init(void);
void ikbd_poll(void);
void ikbd_joystick(unsigned char joy, unsigned char map);
void ikbd_mouse(unsigned char buttons, char x, char y);
void ikbd_keyboard(unsigned char code);
void ikbd_update_time(void);

#endif // IKBD_H
