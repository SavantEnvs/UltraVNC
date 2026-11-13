// Instantiates UltraVNC's real ZRLE/ZYWRLE (zlib + zstd) and XZ/XZYW rectangle decoders
// (rfb/zrleDecode.h, rfb/xzDecode.h) on top of the real rdr:: in-streams, exactly as
// vncviewer/zrle.cpp and vncviewer/xz.cpp do, with the viewer's drawing replaced by a checksum sink.
// Shared by the fuzz harnesses and the behavioral self-test.
#include <assert.h>
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

#define zrleDecode ClientConnection::zrleDecode
#define BPP 8
#define ZYWRLE_ENDIAN ENDIAN_NO
#define IMAGE_RECT(x,y,w,h,data) sink((data), (w)*(h))
#include <rfb/zrleDecode.h>
#undef BPP
#undef ZYWRLE_ENDIAN
#define BPP 16
#define ZYWRLE_ENDIAN ENDIAN_LITTLE
#include <rfb/zrleDecode.h>
#undef BPP
#undef ZYWRLE_ENDIAN
#define BPP 15
#define ZYWRLE_ENDIAN ENDIAN_LITTLE
#include <rfb/zrleDecode.h>
#undef BPP
#undef ZYWRLE_ENDIAN
#define BPP 32
#define ZYWRLE_ENDIAN ENDIAN_LITTLE
#include <rfb/zrleDecode.h>
#define CPIXEL 24A
#include <rfb/zrleDecode.h>
#undef CPIXEL
#define CPIXEL 24B
#include <rfb/zrleDecode.h>
#undef CPIXEL
#undef BPP
#undef ZYWRLE_ENDIAN
#undef zrleDecode
#undef IMAGE_RECT

// The tile buffer is CheckBufferSize(64*64*4) bytes in the viewer; keep it 4-byte aligned.
alignas(8) static rdr::U8 tileBuf[rfbZRLETileWidth * rfbZRLETileHeight * 4];

template <class S>
static void zrleDispatch(ClientConnection& cc, int fmt, int x, int y, int w, int h,
                         rdr::InStream* fis, S* zis) {
  switch (fmt) {
    case UVNC_FMT_8:    cc.zrleDecode8NE(x, y, w, h, fis, zis, (rdr::U8*)tileBuf); break;
    case UVNC_FMT_15:   cc.zrleDecode15LE(x, y, w, h, fis, zis, (rdr::U16*)tileBuf); break;
    case UVNC_FMT_16:   cc.zrleDecode16LE(x, y, w, h, fis, zis, (rdr::U16*)tileBuf); break;
    case UVNC_FMT_24A:  cc.zrleDecode24ALE(x, y, w, h, fis, zis, (rdr::U32*)tileBuf); break;
    case UVNC_FMT_24B:  cc.zrleDecode24BLE(x, y, w, h, fis, zis, (rdr::U32*)tileBuf); break;
    default:            cc.zrleDecode32LE(x, y, w, h, fis, zis, (rdr::U32*)tileBuf); break;
  }
}

int uvnc_zrle_decode(int fmt, int zstd, int zywrle_level, int x, int y, int w, int h,
                     const uint8_t* data, size_t size, uint64_t* sum, uint64_t* pixels) {
  ClientConnection cc;
  cc.zywrle = zywrle_level > 0;
  cc.zywrle_level = zywrle_level;
  memset(tileBuf, 0, sizeof tileBuf);
  rdr::MemInStream fis(data, (int)size);
  int rc = 0;
  try {
    if (zstd) {
      rdr::ZstdInStream zis;
      zrleDispatch(cc, fmt, x, y, w, h, &fis, &zis);
    } else {
      rdr::ZlibInStream zis;
      zrleDispatch(cc, fmt, x, y, w, h, &fis, &zis);
    }
  } catch (rdr::Exception&) {
    rc = -1;
  }
  if (sum) *sum = cc.sum;
  if (pixels) *pixels = cc.pixels;
  return rc;
}

