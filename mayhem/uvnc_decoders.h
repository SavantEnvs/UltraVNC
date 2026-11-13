#ifndef UVNC_DECODERS_H
#define UVNC_DECODERS_H
#include <stddef.h>
#include <stdint.h>

enum { UVNC_FMT_8, UVNC_FMT_15, UVNC_FMT_16, UVNC_FMT_24A, UVNC_FMT_24B, UVNC_FMT_32, UVNC_NFMT };
enum { UVNC_XZ_MAX_RECTS = 4 };

// Returns 0 on success, -1 if the decoder raised rdr::Exception. sum/pixels summarize the decoded pixels.
int uvnc_zrle_decode(int fmt, int zstd, int zywrle_level, int x, int y, int w, int h,
                     const uint8_t* data, size_t size, uint64_t* sum, uint64_t* pixels);
int uvnc_xz_decode(int fmt, int xyw_level, int n_rects, const uint8_t* data, size_t size,
                   uint64_t* sum, uint64_t* pixels);
#endif
