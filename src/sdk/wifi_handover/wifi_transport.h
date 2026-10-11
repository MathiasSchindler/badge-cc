#ifndef WIFI_HANDOVER_WIFI_TRANSPORT_H
#define WIFI_HANDOVER_WIFI_TRANSPORT_H

#include "experiment.h"

/* All calls, raw-lwIP callbacks and cyw43_arch_poll run on core0.
 * The parent initializes CYW43 once (shared with BLE); init/stop do not deinit it.
 * Parent must poll CYW43 before this transport and advertise only at WIFI_READY.
 * No DHCP/DNS: /24 addresses are 192.168.4.(slot+1), gateway unset.
 * Credentials channel 0 selects 6; supported channels are 1..11.
 * Every public false return sets view.status/error_code, including in WIFI_READY.
 * Routine traffic and automatic benchmark completion preserve reported errors;
 * explicit successful PM/benchmark/session actions may replace the status.
 */
void wifi_transport_init(void);
bool wifi_transport_host(const handover_credentials_t *session);
bool wifi_transport_join(const handover_credentials_t *credentials);
void wifi_transport_stop(void);
void wifi_transport_member(uint8_t slot, uint64_t token);
void wifi_transport_input(int direction);
void wifi_transport_poll(uint32_t now_ms);
bool wifi_transport_set_power_save(bool enabled);
bool wifi_transport_benchmark(uint8_t target);
void wifi_transport_cancel_benchmark(void);
const experiment_wifi_view_t *wifi_transport_view(void);

#endif
