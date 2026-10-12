#ifndef UNIVERSE26_HOME_H
#define UNIVERSE26_HOME_H

#include <stdint.h>

void badger_home_init(void);
int badger_home_pressed(void);
/* Core 0 owns IRQ21 while enabled; both button edges generate events. */
void badger_home_wake_enable(void);
void badger_home_wake_disable(void);
void badger_home_wait_event(void);
void bare_gpio_wake_irq(void);

#endif
