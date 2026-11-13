#ifndef WEBIO_DRIVE_H
#define WEBIO_DRIVE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Sends `req` to a fresh webio session and returns the number of reply bytes copied into resp.
size_t webio_exchange(const uint8_t* req, size_t n, uint8_t* resp, size_t cap);
#ifdef __cplusplus
}
#endif
#endif
