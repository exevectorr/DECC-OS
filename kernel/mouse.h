/* mouse.h */
#ifndef MOUSE_H
#define MOUSE_H
void mouse_init(void);
void mouse_handler(void);       /* called from IRQ12 */
int  mouse_x(void);
int  mouse_y(void);
int  mouse_left(void);
void mouse_draw_cursor(void);
#endif