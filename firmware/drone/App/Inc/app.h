#ifndef APP_H
#define APP_H

/* Call once after HAL, clocks and board peripherals are initialized. */
void app_init(void);

/* Run repeatedly from the foreground loop. */
void app_process(void);

#endif
