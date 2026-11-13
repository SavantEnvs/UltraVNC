// libFuzzer harness: UltraVNC viewer XZ / XZYW rectangle decoder (rfb/xzDecode.h) over the real
// rdr::xzInStream. Input layout: [cfg][xz stream...]; the xz stream carries the rectangle headers
// followed by the tile data, as ClientConnection.cpp reads them.
//   cfg bits 0-2: pixel format (8, 15, 16, 24A, 24B, 32; values 6-7 map to 32)
//   cfg bits 3-4: number of rectangles - 1
//   cfg bits 5-6: XZYW level (0 = plain XZ)
#include <stddef.h>
#include <stdint.h>
#include "uvnc_decoders.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2) return 0;  // no room for the config byte plus any stream
  int cfg = data[0];
  int fmt = (cfg & 7) > UVNC_FMT_32 ? UVNC_FMT_32 : (cfg & 7);
  int n_rects = 1 + ((cfg >> 3) & 3);
  int level = (cfg >> 5) & 3;
  uvnc_xz_decode(fmt, level, n_rects, data + 1, size - 1, nullptr, nullptr);
  return 0;
}
