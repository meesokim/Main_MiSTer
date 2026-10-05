#ifndef ZMX_SERVICE_H
#define ZMX_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void zmx_service_init(const char *rom_path);
void zmx_service_poll();
void zmx_service_stop();

#ifdef __cplusplus
}
#endif

#endif // ZMX_SERVICE_H
