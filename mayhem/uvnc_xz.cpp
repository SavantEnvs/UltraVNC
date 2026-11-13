// Instantiates UltraVNC's real ZRLE/ZYWRLE (zlib + zstd) and XZ/XZYW rectangle decoders
// (rfb/zrleDecode.h, rfb/xzDecode.h) on top of the real rdr:: in-streams, exactly as
// vncviewer/zrle.cpp and vncviewer/xz.cpp do, with the viewer's drawing replaced by a checksum sink.
// Shared by the fuzz harnesses and the behavioral self-test.
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <rdr/MemInStream.h>
#include <rdr/ZlibInStream.h>
#include <rdr/ZstdInStream.h>
#include <rdr/xzInStream.h>
#include <rfb/rfbproto.h>
#include "uvnc_decoders.h"

struct omni_mutex_lock {
  template <class T> explicit omni_mutex_lock(T&) {}
};
static inline void InvalidateRect(void*, void*, int) {}

#define ENDIAN_LITTLE 0
#define ENDIAN_BIG 1
#define ENDIAN_NO 2

struct ClientConnection {
  int m_bitmapdcMutex = 0;
  void* m_hwndcn = nullptr;
  int initialupdate_counter = 100;
  bool directx_used = true;
  uint64_t sum = 0;
  uint64_t pixels = 0;
  long zywrle = 0;
  long zywrle_level = 0;
  int zywrleBuf[rfbZRLETileWidth * rfbZRLETileHeight];
  long xzyw = 0;
  long xzyw_level = 0;
  int xzywBuf[rfbXZTileWidth * rfbXZTileHeight];

  template <class T>
  void sink(const T* p, int n) {
    for (int i = 0; i < n; i++) sum = sum * 1099511628211ULL + (uint64_t)p[i] + 1;
    pixels += (uint64_t)n;
  }

  template <class myInStream>
  void zrleDecode8NE(int x, int y, int w, int h, rdr::InStream* is, myInStream* zis, rdr::U8* buf);
  template <class myInStream>
  void zrleDecode15LE(int x, int y, int w, int h, rdr::InStream* is, myInStream* zis, rdr::U16* buf);
  template <class myInStream>
  void zrleDecode16LE(int x, int y, int w, int h, rdr::InStream* is, myInStream* zis, rdr::U16* buf);
  template <class myInStream>
  void zrleDecode24ALE(int x, int y, int w, int h, rdr::InStream* is, myInStream* zis, rdr::U32* buf);
  template <class myInStream>
  void zrleDecode24BLE(int x, int y, int w, int h, rdr::InStream* is, myInStream* zis, rdr::U32* buf);
  template <class myInStream>
  void zrleDecode32LE(int x, int y, int w, int h, rdr::InStream* is, myInStream* zis, rdr::U32* buf);

  void xzDecode8NE(int x, int y, int w, int h, rdr::InStream* is, rdr::xzInStream* xzis, rdr::U8* buf);
  void xzDecode15LE(int x, int y, int w, int h, rdr::InStream* is, rdr::xzInStream* xzis, rdr::U16* buf);
  void xzDecode16LE(int x, int y, int w, int h, rdr::InStream* is, rdr::xzInStream* xzis, rdr::U16* buf);
  void xzDecode24ALE(int x, int y, int w, int h, rdr::InStream* is, rdr::xzInStream* xzis, rdr::U32* buf);
  void xzDecode24BLE(int x, int y, int w, int h, rdr::InStream* is, rdr::xzInStream* xzis, rdr::U32* buf);
  void xzDecode32LE(int x, int y, int w, int h, rdr::InStream* is, rdr::xzInStream* xzis, rdr::U32* buf);
};

#define xzDecode ClientConnection::xzDecode
#define BPP 8
#define XZYW_ENDIAN ENDIAN_NO
#define IMAGE_RECT(x,y,w,h,data) sink((data), (w)*(h))
#include <rfb/xzDecode.h>
#undef BPP
#undef XZYW_ENDIAN
#define BPP 16
#define XZYW_ENDIAN ENDIAN_LITTLE
#include <rfb/xzDecode.h>
#undef BPP
#undef XZYW_ENDIAN
#define BPP 15
#define XZYW_ENDIAN ENDIAN_LITTLE
#include <rfb/xzDecode.h>
#undef BPP
#undef XZYW_ENDIAN
#define BPP 32
#define XZYW_ENDIAN ENDIAN_LITTLE
#include <rfb/xzDecode.h>
#define CPIXEL 24A
#include <rfb/xzDecode.h>
#undef CPIXEL
#define CPIXEL 24B
#include <rfb/xzDecode.h>
#undef CPIXEL
#undef BPP
#undef XZYW_ENDIAN
#undef xzDecode
#undef IMAGE_RECT

// The tile buffer is CheckBufferSize(64*64*4) bytes in the viewer; keep it 4-byte aligned.
alignas(8) static rdr::U8 tileBuf[rfbZRLETileWidth * rfbZRLETileHeight * 4];

static void xzDispatch(ClientConnection& cc, int fmt, int x, int y, int w, int h,
                       rdr::InStream* fis, rdr::xzInStream* xzis) {
  switch (fmt) {
    case UVNC_FMT_8:    cc.xzDecode8NE(x, y, w, h, fis, xzis, (rdr::U8*)tileBuf); break;
    case UVNC_FMT_15:   cc.xzDecode15LE(x, y, w, h, fis, xzis, (rdr::U16*)tileBuf); break;
    case UVNC_FMT_16:   cc.xzDecode16LE(x, y, w, h, fis, xzis, (rdr::U16*)tileBuf); break;
    case UVNC_FMT_24A:  cc.xzDecode24ALE(x, y, w, h, fis, xzis, (rdr::U32*)tileBuf); break;
    case UVNC_FMT_24B:  cc.xzDecode24BLE(x, y, w, h, fis, xzis, (rdr::U32*)tileBuf); break;
    default:            cc.xzDecode32LE(x, y, w, h, fis, xzis, (rdr::U32*)tileBuf); break;
  }
}

int uvnc_xz_decode(int fmt, int xyw_level, int n_rects, const uint8_t* data, size_t size,
                   uint64_t* sum, uint64_t* pixels) {
  ClientConnection cc;
  memset(tileBuf, 0, sizeof tileBuf);
  rdr::MemInStream fis(data, (int)size);
  int rc = 0;
  try {
    rdr::xzInStream xzis;
    // Known upstream bug, deliberately not reported by this target: xzInStream::decompress()
    // assert(false)s (xzInStream.cxx:118; release builds spin) when the declared byte count runs out
    // while liblzma still wants input. A hostile server reaches it from the viewer (ClientConnection.cpp
    // reads nDataLength bytes, then setUnderlying(fis, nDataLength)) with a truthful length and a
    // truncated stream. With the count equal to the input it fires on every truncated stream, 2-byte
    // inputs included, which aborts Mayhem's -runs=5 smoketest. Declaring INT_MAX lets MemInStream's
    // end-of-data exception end a starved decode instead; the decoders and the stream stay unmodified.
    xzis.setUnderlying(&fis, INT_MAX);
    // As ClientConnection.cpp does: first the rectangle headers, then one decode per rectangle.
    rfbRectangle rects[UVNC_XZ_MAX_RECTS];
    for (int i = 0; i < n_rects; i++) {
      xzis.readBytes(&rects[i], sz_rfbRectangle);
      rects[i].x = (CARD16)((rects[i].x >> 8) | (rects[i].x << 8));
      rects[i].y = (CARD16)((rects[i].y >> 8) | (rects[i].y << 8));
      rects[i].w = (CARD16)((rects[i].w >> 8) | (rects[i].w << 8));
      rects[i].h = (CARD16)((rects[i].h >> 8) | (rects[i].h << 8));
    }
    for (int i = 0; i < n_rects; i++) {
      cc.xzyw = xyw_level > 0;
      cc.xzyw_level = xyw_level;
      xzDispatch(cc, fmt, rects[i].x, rects[i].y, rects[i].w, rects[i].h, &fis, &xzis);
    }
  } catch (rdr::Exception&) {
    rc = -1;
  }
  if (sum) *sum = cc.sum;
  if (pixels) *pixels = cc.pixels;
  return rc;
}
