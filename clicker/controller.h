#ifndef CLICKER_CONTROLLER_H
#define CLICKER_CONTROLLER_H

#include "input.h"
#include "graphics.h"

void clicker_controller_run(badger_canvas_t *canvas, clicker_input_t *input,
                             uint8_t (*buttons_read)(void));

#endif
