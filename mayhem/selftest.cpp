// Behavioral self-test (mayhem/test.sh). Known-input -> known-output checks against UltraVNC's real
// rectangle decoders and the repeater's real web server:
//   * ZRLE (zlib and zstd) and XZ streams are built here with the reference compressors from tiles
//     that use every ZRLE sub-encoding (raw, solid, packed palette, plain RLE, palette RLE) in every
//     pixel format; the decoders must reproduce the exact pixel sequence (checked as a hash of it).
//   * Corrupt streams must be rejected with rdr::Exception, not decoded.
//   * HTTP requests to the web server must produce the documented status line and body.
// `selftest --emit-seeds <dir>` writes the fuzz seed corpora instead.
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <zstd.h>
#include <string>
#include <vector>
#include "uvnc_decoders.h"
#include "webio_drive.h"

typedef std::vector<uint8_t> Bytes;
static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* what) {
  if (ok) {
    g_pass++;
  } else {
    g_fail++;
    fprintf(stderr, "FAIL: %s\n", what);
  }
}

static const int kBytesPerPixel[UVNC_NFMT] = {1, 2, 2, 3, 3, 4};  // bytes on the wire (CPIXEL)
static const int kBytesOut[UVNC_NFMT] = {1, 2, 2, 4, 4, 4};

static uint32_t pixelValue(int fmt, uint32_t seed) {
  uint32_t v = seed * 2654435761u + 12345u;
  switch (fmt) {
    case UVNC_FMT_8: return (v >> 7) & 0xFF;
    case UVNC_FMT_15: return (v >> 5) & 0x7FFF;
    case UVNC_FMT_16: return (v >> 5) & 0xFFFF;
    case UVNC_FMT_24A: return (v >> 3) & 0xFFFFFF;
    case UVNC_FMT_24B: return (v >> 3) & 0xFFFFFF00;
    default: return v;
  }
}

static void putPixel(Bytes& out, int fmt, uint32_t v) {
  if (fmt == UVNC_FMT_24B) v >>= 8;
  for (int i = 0; i < kBytesPerPixel[fmt]; i++) out.push_back((v >> (8 * i)) & 0xFF);
}

static void putRun(Bytes& out, int len) {
  len -= 1;
  while (len >= 255) { out.push_back(255); len -= 255; }
  out.push_back((uint8_t)len);
}

// Encodes one tile of n pixels with sub-encoding `kind`; appends the expected pixels to `expect`.
static void encodeTile(Bytes& out, std::vector<uint32_t>& expect, int fmt, int kind, int tw, int th,
                       uint32_t seed) {
  int n = tw * th;
  switch (kind % 5) {
    case 0:  // raw
      out.push_back(0);
      for (int i = 0; i < n; i++) { uint32_t v = pixelValue(fmt, seed + i); putPixel(out, fmt, v); expect.push_back(v); }
      break;
    case 1: {  // solid
      out.push_back(1);
      uint32_t v = pixelValue(fmt, seed);
      putPixel(out, fmt, v);
      for (int i = 0; i < n; i++) expect.push_back(v);
      break;
    }
    case 2: {  // packed palette
      static const int sizes[] = {2, 3, 5, 17};
      int k = sizes[(seed / 7) % 4];
      int bppp = k > 16 ? 8 : k > 4 ? 4 : k > 2 ? 2 : 1;
      out.push_back((uint8_t)k);
      uint32_t pal[17];
      for (int i = 0; i < k; i++) { pal[i] = pixelValue(fmt, seed * 31 + i); putPixel(out, fmt, pal[i]); }
      for (int y = 0; y < th; y++) {
        uint8_t byte = 0; int nbits = 0;
        for (int x = 0; x < tw; x++) {
          int idx = (x * 5 + y * 3 + x / 3) % k;
          byte = (uint8_t)((byte << bppp) | idx); nbits += bppp;
          if (nbits == 8) { out.push_back(byte); byte = 0; nbits = 0; }
          expect.push_back(pal[idx]);
        }
        if (nbits) out.push_back((uint8_t)(byte << (8 - nbits)));
      }
      break;
    }
    case 3: {  // plain RLE
      out.push_back(128);
      int done = 0, r = 0;
      while (done < n) {
        int len = 1 + ((r * 37 + seed) % 300);
        if (len > n - done) len = n - done;
        uint32_t v = pixelValue(fmt, seed * 17 + r);
        putPixel(out, fmt, v);
        putRun(out, len);
        for (int i = 0; i < len; i++) expect.push_back(v);
        done += len; r++;
      }
      break;
    }
    default: {  // palette RLE
      int k = 2 + seed % 9;
      out.push_back((uint8_t)(128 | k));
      uint32_t pal[16];
      for (int i = 0; i < k; i++) { pal[i] = pixelValue(fmt, seed * 13 + i); putPixel(out, fmt, pal[i]); }
      int done = 0, r = 0;
      while (done < n) {
        int len = (r % 3 == 0) ? 1 : 1 + ((r * 53 + seed) % 400);
        if (len > n - done) len = n - done;
        int idx = (r * 7 + seed) % k;
        if (len == 1) out.push_back((uint8_t)idx);
        else { out.push_back((uint8_t)(128 | idx)); putRun(out, len); }
        for (int i = 0; i < len; i++) expect.push_back(pal[idx]);
        done += len; r++;
      }
    }
  }
}

// Tiles of a w x h rectangle in the order the decoders visit them; sub-encodings cycle from `kind0`.
static void encodeRect(Bytes& raw, std::vector<uint32_t>& expect, int fmt, int w, int h, int kind0) {
  int t = 0;
  for (int ty = 0; ty < h; ty += 64)
    for (int tx = 0; tx < w; tx += 64, t++)
      encodeTile(raw, expect, fmt, kind0 + t, w - tx < 64 ? w - tx : 64, h - ty < 64 ? h - ty : 64, 100 + t);
}

static uint64_t expectedHash(int fmt, const std::vector<uint32_t>& px) {
  (void)fmt;
  uint64_t s = 0;
  for (uint32_t v : px) s = s * 1099511628211ULL + (uint64_t)v + 1;
  return s;
}

static Bytes deflateSync(const Bytes& raw) {
  z_stream zs;
  memset(&zs, 0, sizeof zs);
  deflateInit(&zs, 6);
  Bytes out(deflateBound(&zs, raw.size()) + 64);
  zs.next_in = (Bytes::value_type*)raw.data();
  zs.avail_in = (uInt)raw.size();
  zs.next_out = out.data();
  zs.avail_out = (uInt)out.size();
  deflate(&zs, Z_SYNC_FLUSH);
  out.resize(zs.total_out);
  deflateEnd(&zs);
  return out;
}

static Bytes zstdFrame(const Bytes& raw) {
  Bytes out(ZSTD_compressBound(raw.size()));
  size_t n = ZSTD_compress(out.data(), out.size(), raw.data(), raw.size(), 3);
  out.resize(n);
  return out;
}

static Bytes xzStream(const Bytes& raw) {
  Bytes out(lzma_stream_buffer_bound(raw.size()));
  size_t pos = 0;
  lzma_easy_buffer_encode(1, LZMA_CHECK_CRC32, nullptr, raw.data(), raw.size(), out.data(), &pos, out.size());
  out.resize(pos);
  return out;
}

static void be32(Bytes& b, uint32_t v) { for (int i = 3; i >= 0; i--) b.push_back((v >> (8 * i)) & 0xFF); }
static void be16(Bytes& b, int v) { b.push_back((v >> 8) & 0xFF); b.push_back(v & 0xFF); }

static Bytes zrleWire(const Bytes& compressed) {
  Bytes w;
  be32(w, (uint32_t)compressed.size());
  w.insert(w.end(), compressed.begin(), compressed.end());
  return w;
}

static const char* kFmtName[UVNC_NFMT] = {"8", "15", "16", "24A", "24B", "32"};

static void testZrle() {
  const int dims[][2] = {{10, 7}, {64, 64}, {100, 70}, {130, 3}};
  for (int fmt = 0; fmt < UVNC_NFMT; fmt++)
    for (int zstd = 0; zstd < 2; zstd++)
      for (auto& d : dims) {
        Bytes raw; std::vector<uint32_t> expect;
        encodeRect(raw, expect, fmt, d[0], d[1], fmt + d[0]);
        Bytes wire = zrleWire(zstd ? zstdFrame(raw) : deflateSync(raw));
        uint64_t sum = 0, px = 0;
        int rc = uvnc_zrle_decode(fmt, zstd, 0, 0, 0, d[0], d[1], wire.data(), wire.size(), &sum, &px);
        char what[160];
        snprintf(what, sizeof what, "ZRLE %s fmt=%s %dx%d decodes", zstd ? "zstd" : "zlib", kFmtName[fmt], d[0], d[1]);
        check(rc == 0, what);
        snprintf(what, sizeof what, "ZRLE %s fmt=%s %dx%d pixel count", zstd ? "zstd" : "zlib", kFmtName[fmt], d[0], d[1]);
        check(px == (uint64_t)d[0] * d[1], what);
        snprintf(what, sizeof what, "ZRLE %s fmt=%s %dx%d pixel values", zstd ? "zstd" : "zlib", kFmtName[fmt], d[0], d[1]);
        check(sum == expectedHash(fmt, expect), what);
      }

  // Every sub-encoding exercised on its own with 32-bit pixels: a solid 64x64 tile of 0x11223344.
  {
    Bytes raw = {1, 0x44, 0x33, 0x22, 0x11};
    Bytes wire = zrleWire(deflateSync(raw));
    uint64_t sum = 0, px = 0;
    int rc = uvnc_zrle_decode(UVNC_FMT_32, 0, 0, 0, 0, 64, 64, wire.data(), wire.size(), &sum, &px);
    std::vector<uint32_t> e(64 * 64, 0x11223344u);
    check(rc == 0 && px == 4096 && sum == expectedHash(UVNC_FMT_32, e), "ZRLE solid 32bpp tile");
  }

  // ZYWRLE: a raw 32-bit tile goes through the inverse wavelet; the result is a fixed function of the input.
  {
    Bytes raw; raw.push_back(0); raw.push_back(0);  // ZYWRLE raw tile: mode byte is read twice (goto top)
    for (int i = 0; i < 64 * 64; i++) putPixel(raw, UVNC_FMT_32, pixelValue(UVNC_FMT_32, i) & 0x00F8F8F8u);
    Bytes wire = zrleWire(deflateSync(raw));
    uint64_t s1 = 0, s2 = 0, px = 0;
    int rc1 = uvnc_zrle_decode(UVNC_FMT_32, 0, 1, 0, 0, 64, 64, wire.data(), wire.size(), &s1, &px);
    int rc2 = uvnc_zrle_decode(UVNC_FMT_32, 0, 0, 0, 0, 64, 64, wire.data(), wire.size(), &s2, &px);
    check(rc1 == 0 && rc2 == 0 && px == 4096, "ZYWRLE tile decodes");
    check(s1 != s2, "ZYWRLE level 1 transforms the raw tile");
    check(s1 == 0x406fb2cb19ec2c69ULL, "ZYWRLE level 1 golden hash");
  }

  // Corrupt input must be refused.
  {
    Bytes raw; std::vector<uint32_t> e;
    encodeRect(raw, e, UVNC_FMT_32, 64, 64, 0);
    Bytes wire = zrleWire(deflateSync(raw));
    Bytes bad = wire;
    for (size_t i = 6; i < bad.size(); i++) bad[i] ^= 0x5A;
    check(uvnc_zrle_decode(UVNC_FMT_32, 0, 0, 0, 0, 64, 64, bad.data(), bad.size(), nullptr, nullptr) == -1, "corrupt zlib stream rejected");
    Bytes trunc(wire.begin(), wire.begin() + wire.size() / 2);
    check(uvnc_zrle_decode(UVNC_FMT_32, 0, 0, 0, 0, 64, 64, trunc.data(), trunc.size(), nullptr, nullptr) == -1, "truncated zlib stream rejected");
    Bytes zbad = zrleWire(zstdFrame(raw));
    for (size_t i = 8; i < zbad.size(); i++) zbad[i] ^= 0xA5;
    check(uvnc_zrle_decode(UVNC_FMT_32, 1, 0, 0, 0, 64, 64, zbad.data(), zbad.size(), nullptr, nullptr) == -1, "corrupt zstd stream rejected");
  }
}

static Bytes xzPayload(int fmt, int nrects, std::vector<uint32_t>& expect) {
  Bytes raw;
  const int rects[4][4] = {{0, 0, 70, 20}, {5, 9, 16, 16}, {0, 0, 64, 64}, {3, 3, 1, 1}};
  for (int i = 0; i < nrects; i++) { be16(raw, rects[i][0]); be16(raw, rects[i][1]); be16(raw, rects[i][2]); be16(raw, rects[i][3]); }
  for (int i = 0; i < nrects; i++) encodeRect(raw, expect, fmt, rects[i][2], rects[i][3], fmt + i);
  return xzStream(raw);
}

static void testXz() {
  for (int fmt = 0; fmt < UVNC_NFMT; fmt++)
    for (int nrects = 1; nrects <= 4; nrects++) {
      std::vector<uint32_t> expect;
      Bytes x = xzPayload(fmt, nrects, expect);
      uint64_t sum = 0, px = 0;
      int rc = uvnc_xz_decode(fmt, 0, nrects, x.data(), x.size(), &sum, &px);
      char what[96];
      snprintf(what, sizeof what, "XZ fmt=%s rects=%d decodes", kFmtName[fmt], nrects);
      check(rc == 0, what);
      snprintf(what, sizeof what, "XZ fmt=%s rects=%d pixel values", kFmtName[fmt], nrects);
      check(px == expect.size() && sum == expectedHash(fmt, expect), what);
    }
  std::vector<uint32_t> e;
  Bytes x = xzPayload(UVNC_FMT_32, 2, e);
  Bytes bad = x;
  for (size_t i = 12; i < bad.size(); i++) bad[i] ^= 0x33;
  check(uvnc_xz_decode(UVNC_FMT_32, 0, 2, bad.data(), bad.size(), nullptr, nullptr) == -1, "corrupt xz stream rejected");
  Bytes trunc(x.begin(), x.begin() + x.size() / 2);
  check(uvnc_xz_decode(UVNC_FMT_32, 0, 2, trunc.data(), trunc.size(), nullptr, nullptr) == -1, "truncated xz stream rejected");
  check(uvnc_xz_decode(UVNC_FMT_32, 0, 2, x.data(), 1, nullptr, nullptr) == -1, "one-byte xz stream rejected");
  Bytes hdr;
  be16(hdr, 0); be16(hdr, 0); be16(hdr, 64); be16(hdr, 64);
  Bytes starved = xzStream(hdr);
  check(uvnc_xz_decode(UVNC_FMT_32, 0, 1, starved.data(), starved.size(), nullptr, nullptr) == -1, "valid xz stream without tile data rejected");
}

static std::string exchange(const char* req) {
  static uint8_t resp[65536];
  size_t n = webio_exchange((const uint8_t*)req, strlen(req), resp, sizeof resp);
  return std::string((const char*)resp, n);
}

static void testWebio() {
  const char* auth = "Authorization: Basic YWRtaW46YWRtaW4=\r\n";  // admin:admin
  std::string r = exchange((std::string("GET /index.html HTTP/1.0\r\n") + auth + "\r\n").c_str());
  check(r.compare(0, 15, "HTTP/1.1 200 OK") == 0, "GET /index.html with admin:admin -> 200 OK");
  check(r.find("Server: Webio Embedded server v1.0") != std::string::npos, "Server header present");
  check(r.find("Content-Type: text/html") != std::string::npos, "index.html is text/html");
  check(r.find("UltraVnc Repeater Stats") != std::string::npos, "index.html body is the repeater stats page");

  r = exchange("GET /index.html HTTP/1.0\r\n\r\n");
  check(r.compare(0, 12, "HTTP/1.1 401") == 0, "index.html without credentials -> 401");
  check(r.find("WWW-Authenticate: Basic realm=") != std::string::npos, "401 carries a Basic challenge");

  r = exchange("GET /index.html HTTP/1.0\r\nAuthorization: Basic YWRtaW46d3Jvbmc=\r\n\r\n");
  check(r.compare(0, 12, "HTTP/1.1 401") == 0, "wrong password -> 401");

  r = exchange("GET /no-such-page.html HTTP/1.0\r\n\r\n");
  check(r.compare(0, 12, "HTTP/1.1 333") == 0, "unknown page -> 333 (only built-in pages)");

  r = exchange((std::string("GET /settings.html HTTP/1.0\r\n") + auth + "\r\n").c_str());
  check(r.compare(0, 15, "HTTP/1.1 200 OK") == 0 && r.find("<form") != std::string::npos, "settings.html is a form page");
}

static void writeFile(const std::string& dir, const std::string& name, const Bytes& b) {
  std::string p = dir + "/" + name;
  FILE* f = fopen(p.c_str(), "wb");
  if (!f) { perror(p.c_str()); exit(2); }
  fwrite(b.data(), 1, b.size(), f);
  fclose(f);
}

static void emitSeeds(const std::string& root) {
  for (int fmt = 0; fmt < UVNC_NFMT; fmt++)
    for (int zstd = 0; zstd < 2; zstd++) {
      Bytes raw; std::vector<uint32_t> e;
      int w = 40 + fmt * 20, h = 10 + fmt * 9;
      encodeRect(raw, e, fmt, w, h, fmt);
      Bytes wire = zrleWire(zstd ? zstdFrame(raw) : deflateSync(raw));
      Bytes seed = {(uint8_t)(fmt | (zstd << 3)), (uint8_t)(w - 1), (uint8_t)(h - 1)};
      seed.insert(seed.end(), wire.begin(), wire.end());
      char name[64];
      snprintf(name, sizeof name, "rect_%s_%s.bin", kFmtName[fmt], zstd ? "zstd" : "zlib");
      writeFile(root + "/fuzz_zrle/testsuite", name, seed);
      if (zstd == 0 && (fmt == UVNC_FMT_32 || fmt == UVNC_FMT_16)) {
        Bytes zy = {(uint8_t)(fmt | (2 << 4)), 63, 63};
        Bytes raw2; raw2.push_back(0); raw2.push_back(0);
        for (int i = 0; i < 4096; i++) putPixel(raw2, fmt, pixelValue(fmt, i) & (fmt == UVNC_FMT_32 ? 0x00F8F8F8u : 0xF7DEu));
        Bytes w2 = zrleWire(deflateSync(raw2));
        zy.insert(zy.end(), w2.begin(), w2.end());
        snprintf(name, sizeof name, "zywrle_%s.bin", kFmtName[fmt]);
        writeFile(root + "/fuzz_zrle/testsuite", name, zy);
      }
    }
  for (int fmt = 0; fmt < UVNC_NFMT; fmt++)
    for (int nrects = 1; nrects <= 3; nrects += 2) {
      std::vector<uint32_t> e;
      Bytes x = xzPayload(fmt, nrects, e);
      Bytes seed = {(uint8_t)(fmt | ((nrects - 1) << 3))};
      seed.insert(seed.end(), x.begin(), x.end());
      char name[64];
      snprintf(name, sizeof name, "rects%d_%s.bin", nrects, kFmtName[fmt]);
      writeFile(root + "/fuzz_xz/testsuite", name, seed);
    }
}

int main(int argc, char** argv) {
  if (argc == 3 && !strcmp(argv[1], "--emit-seeds")) { emitSeeds(argv[2]); return 0; }
  testZrle();
  testXz();
  testWebio();
  printf("selftest: %d passed, %d failed\n", g_pass, g_fail);
  printf("CTRF_COUNTS %d %d\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
