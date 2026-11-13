// Stand-ins for the symbols the repeater's embedded web server (repeater/webgui/*.c) expects from the
// Windows-only parts of the repeater (webtest.c / wsfcode.c / settings.c): clock, listening port,
// authentication hook, tracing, and the SSI/CGI page handlers. The handlers only emit fixed text
// (SSI) or read the same form fields and redirect (CGI), so the real HTTP parsing, form decoding,
// file lookup, SSI expansion and reply code in webio.c/webutils.c/webfs.c/webobjs.c is what runs.
#include "websys.h"
#include "webio.h"
#include "webfs.h"
#include "wsfdata.h"

u_long cticks = 0;
int saved_portHTTP = 0;

int wfs_auth(void* fd, char* name, char* password) {
  EOFILE* eofile = (EOFILE*)fd;
  if (eofile->eo_emfile->em_flags & EMF_AUTH) {
    if (_stricmp(name, "admin")) return 0;
    if (_stricmp(password, "admin")) return 0;
  }
  return 1;
}

void ws_dtrap(void) {}
void panic(char* msg) { (void)msg; abort(); }

char* wi_getdate(wi_sess* sess) {
  (void)sess;
  return "Thu, 01 Jan 1970 00:00:00";
}

int wi_cvariables(wi_sess* sess, int token) {
  if (token == MEMHITS_VAR31) return wi_putlong(sess, 0);
  return 0;
}

#define SSI_STUB(name)                              \
  int name(wi_sess* sess, EOFILE* eofile) {         \
    (void)eofile;                                   \
    wi_printf(sess, #name);                         \
    return 0;                                       \
  }
SSI_STUB(memory_ssi)
SSI_STUB(mode1_ssi)
SSI_STUB(mode2_ssi)
SSI_STUB(sport_ssi)
SSI_STUB(vport_ssi)
SSI_STUB(acon_ssi)
SSI_STUB(rcon_ssi)
SSI_STUB(aid_ssi)
SSI_STUB(acons_ssi)
SSI_STUB(rcons_ssi)
SSI_STUB(aids_ssi)
SSI_STUB(webport_ssi)
SSI_STUB(log_ssi)
SSI_STUB(ucom_ssi)
SSI_STUB(listcomment_ssi)
SSI_STUB(connections_ssi)
SSI_STUB(server_access_ssi)
SSI_STUB(viewer_access_ssi)
SSI_STUB(keepalive_ssi)
SSI_STUB(pushtest_func)

char* testaction_cgi(wi_sess* sess, EOFILE* eofile) {
  (void)eofile;
  wi_formvalue(sess, "mode1");
  wi_formvalue(sess, "server_port");
  wi_formvalue(sess, "allow_con");
  wi_redirect(sess, "settings.html");
  return NULL;
}
char* testaction2_cgi(wi_sess* sess, EOFILE* eofile) { (void)eofile; wi_redirect(sess, "index.html"); return NULL; }
char* testaction3_cgi(wi_sess* sess, EOFILE* eofile) { (void)eofile; wi_redirect(sess, "index.html"); return NULL; }
char* testaction4_cgi(wi_sess* sess, EOFILE* eofile) { (void)eofile; wi_redirect(sess, "index.html"); return NULL; }
char* testaction5_cgi(wi_sess* sess, EOFILE* eofile) { (void)eofile; wi_redirect(sess, "index.html"); return NULL; }

char* passwd_cgi(wi_sess* sess, EOFILE* eofile) {
  (void)eofile;
  char* old_pass = wi_formvalue(sess, "oldpass");
  char* new_pass = wi_formvalue(sess, "newpass");
  if (old_pass != NULL && new_pass != NULL)
    wi_redirect(sess, strcmp(old_pass, "admin") == 0 ? "ok.html" : "nok.html");
  else
    wi_redirect(sess, "passwd.html");
  return NULL;
}
