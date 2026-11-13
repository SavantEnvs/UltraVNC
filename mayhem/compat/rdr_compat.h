// Force-included (-include) when compiling UltraVNC's rdr/ streams and the rfb/ decoder templates
// on Linux: supplies the few MSVC CRT / Win32 names they use. Upstream files stay untouched.
#ifndef UVNC_RDR_COMPAT_H
#define UVNC_RDR_COMPAT_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t CARD8;
typedef uint16_t CARD16;
typedef uint32_t CARD32;
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

// MSVC secure-CRT template overloads for fixed-size arrays (truncating; rdr::Exception passes room-1 counts).
template <size_t N>
inline int strncat_s(char (&dst)[N], const char* src, size_t count) {
  size_t dl = strlen(dst);
  size_t room = N - dl - 1;
  size_t n = strnlen(src, count);
  if (n > room) n = room;
  memcpy(dst + dl, src, n);
  dst[dl + n] = 0;
  return 0;
}
inline int strerror_s(char* buf, size_t size, int err) {
  snprintf(buf, size, "errno %d", err);
  return 0;
}
template <size_t N>
inline int sprintf_s(char (&buf)[N], const char* fmt, int v) {
  return snprintf(buf, N, fmt, v);
}
#endif
