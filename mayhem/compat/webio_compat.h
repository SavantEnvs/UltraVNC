// Force-included (-include) when compiling repeater/webgui/*.c with -DLINUX on Linux: supplies the
// Windows types and MSVC CRT names those files use. Upstream files stay untouched.
#ifndef WEBIO_COMPAT_H
#define WEBIO_COMPAT_H
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
typedef unsigned long DWORD;
typedef unsigned long ULONG;
typedef unsigned char BYTE;
typedef int BOOL;
#define GetLastError() 0UL
#define _stricmp strcasecmp
#define _strnicmp strncasecmp

// MSVC strncpy_s(dst, dstsz, src, count): copies at most `count` chars and NUL-terminates; when the
// copied length does not fit in dstsz the CRT's invalid-parameter handler runs, which by default
// terminates the process. abort() models that, so the same inputs that kill the Windows build crash here.
static inline int uvnc_strncpy_s(char* dst, size_t dstsz, const char* src, size_t count) {
  size_t n = strnlen(src, count);
  if (n >= dstsz) abort();
  memcpy(dst, src, n);
  dst[n] = 0;
  return 0;
}
#define strncpy_s(dst, dstsz, src, count) uvnc_strncpy_s((dst), (dstsz), (src), (count))
#endif
