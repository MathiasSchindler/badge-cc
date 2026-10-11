#ifndef UNIVERSE26_WIFI_BRINGUP_VIEW_H
#define UNIVERSE26_WIFI_BRINGUP_VIEW_H

#include "../platform/badger/graphics.h"
#include "../radio/wifi_probe.h"

void wifi_bringup_render(const wifi_probe_report_t *report, badger_canvas_t *canvas);

#endif
