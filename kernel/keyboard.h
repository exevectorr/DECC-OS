#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>

void keyboard_init(void);
int  keyboard_haschar(void);
char keyboard_getchar(void);       /* blocking */
void keyboard_handler(void);       /* called from IRQ1 */
int  keyboard_shift(void);
int  keyboard_ctrl(void);

#endif