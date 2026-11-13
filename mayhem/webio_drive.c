// Drives the repeater's real webio server (wi_poll state machine) over a socketpair: one HTTP request
// in, the server's reply out. Used by the fuzz harness and the behavioral self-test.
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>
#include "websys.h"
#include "webio.h"
#include "webfs.h"
#include "webio_drive.h"

extern socktype wi_listen;
extern int wfs_auth(void* fd, char* name, char* password);

static int drain(int fd, uint8_t* out, size_t cap, size_t* len) {
  uint8_t tmp[4096];
  int got = 0;
  for (;;) {
    ssize_t r = read(fd, tmp, sizeof tmp);
    if (r <= 0) break;
    got = 1;
    size_t room = cap - *len;
    size_t n = (size_t)r < room ? (size_t)r : room;
    memcpy(out + *len, tmp, n);
    *len += n;
  }
  return got;
}

static int ensure_listener(void) {
  if (wi_listen != INVALID_SOCKET) return 1;
  emfs.wfs_fauth = wfs_auth;  // webtest.c main() installs it after wi_init()
  // wi_poll() selects on the listen socket and never accepts unless it is readable. One end of an
  // unnamed socketpair whose peer stays open and silent is never readable, and has no name to collide on.
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return 0;
  wi_listen = pair[0];
  return 1;
}

size_t webio_exchange(const uint8_t* req, size_t n, uint8_t* resp, size_t cap) {
  size_t len = 0;
  int sv[2];
  if (!ensure_listener()) return 0;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return 0;
  WI_NOBLOCKSOCK(sv[1]);
  wi_sess* sess = wi_newsess();
  sess->ws_socket = sv[0];
  WI_NOBLOCKSOCK(sv[0]);
  size_t off = 0;
  while (off < n) {
    ssize_t w = write(sv[1], req + off, n - off);
    if (w <= 0) break;
    off += (size_t)w;
  }
  for (int i = 0; i < 16 && wi_sessions; i++) {
    wi_poll();
    drain(sv[1], resp, cap, &len);
  }
  while (wi_sessions) wi_delsess(wi_sessions);
  drain(sv[1], resp, cap, &len);
  close(sv[1]);
  return len;
}
