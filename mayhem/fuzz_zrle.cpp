// libFuzzer harness: UltraVNC viewer ZRLE / ZYWRLE rectangle decoder (rfb/zrleDecode.h) over the real
// rdr::ZlibInStream or rdr::ZstdInStream. Input layout: [cfg][w][h][stream...], where the stream is
// the bytes the server sends for the rectangle (a U32 compressed length, then the compressed data).
//   cfg bits 0-2: pixel format (8, 15, 16, 24A, 24B, 32; values 6-7 map to 32)
//   cfg bit 3   : 1 = ZSTD stream, 0 = zlib
//   cfg bits 4-5: ZYWRLE level (0 = plain ZRLE)
//   w, h        : rectangle size 1..200 (the header's own size fields are 16-bit; capped so one input
//                 covers at most 4x4 tiles and stays fast)
#include <stddef.h>
#include <stdint.h>
#include "uvnc_decoders.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 3) return 0;  // no room for the three config bytes
  int cfg = data[0];
  int fmt = (cfg & 7) > UVNC_FMT_32 ? UVNC_FMT_32 : (cfg & 7);
  int zstd = (cfg >> 3) & 1;
  int level = (cfg >> 4) & 3;
  int w = 1 + data[1] % 200;
  int h = 1 + data[2] % 200;
  uvnc_zrle_decode(fmt, zstd, level, 0, 0, w, h, data + 3, size - 3, nullptr, nullptr);
  return 0;
}
