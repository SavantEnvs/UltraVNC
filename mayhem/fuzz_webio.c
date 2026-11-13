// libFuzzer harness: the UltraVNC repeater's embedded HTTP server (repeater/webgui/webio.c et al.).
// The input is the raw bytes a browser sends on one connection; the server's poll loop parses the
// request line and headers, decodes Basic auth and POST forms, looks up the embedded file, expands
// SSIs and builds the reply, all over a socketpair.
#include <stddef.h>
#include <stdint.h>
#include "webio_drive.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static uint8_t resp[65536];
  webio_exchange(data, size, resp, sizeof resp);
  return 0;
}
