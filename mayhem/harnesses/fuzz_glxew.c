/*
 * fuzz_glxew — the default Linux (GLX) build: glewInit() = glewContextInit() + glxewInit(),
 * against a mock GL/GLX driver fed from the fuzz input, then glxewIsSupported(name) /
 * glxewGetExtension(name) / glewIsSupported(name) on a fuzz-supplied query string.
 * Reaches glxewInit's glXQueryVersion handling, the GLX client extension string scan
 * (_glewSearchExtension) and every _glewInit_GLX_* loader, plus the GLX name matcher.
 * Input: flags byte, GLX major byte, GLX minor byte, then
 *        "<GL_VERSION>\n<GL_EXTENSIONS>\n<GLX_EXTENSIONS>\n<query name>".
 * No X server is needed: the harness defines the GLX symbols glew.c links against, and the
 * Display* it hands out is an opaque token never dereferenced by GLEW.
 */
#include <GL/glew.h>
#include <GL/glxew.h>
#include "mock_driver.h"

static int   g_glx_major, g_glx_minor;
static char *g_glx_ext;
static char  g_fake_display_storage[64];
static Display *const g_fake_display = (Display *)g_fake_display_storage;

static Display *mock_glXGetCurrentDisplay(void) { return g_fake_display; }

Bool glXQueryVersion(Display *dpy, int *major, int *minor) {
  (void)dpy;
  *major = g_glx_major;
  *minor = g_glx_minor;
  return True;
}

const char *glXGetClientString(Display *dpy, int name) {
  (void)dpy;
  return name == GLX_EXTENSIONS ? g_glx_ext : NULL;
}

void (*glXGetProcAddressARB(const GLubyte *procName))(void) {
  const char *name = (const char *)procName;
  if (!strcmp(name, "glXGetCurrentDisplay")) return (mock_proc)mock_glXGetCurrentDisplay;
  if (!strncmp(name, "glX", 3)) return (mock_proc)mock_dummy; /* GLX 1.2+ core entry points */
  return mock_gl_lookup(name);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const uint8_t *p, *end = data + size, *f;
  size_t n;
  char *query;
  volatile GLboolean sink = 0;
  int ok;
  if (size < 3) return 0;
  g_glx_major = data[1];
  g_glx_minor = data[2];
  /* mock_load_gl reads the flags byte at data[0]; feed it data[0] + the fields after byte 2 */
  {
    uint8_t *buf = (uint8_t *)malloc(size - 2);
    if (!buf) abort();
    buf[0] = data[0];
    memcpy(buf + 1, data + 3, size - 3);
    p = mock_load_gl(buf, size - 2, NULL);
    end = buf + (size - 2);
    free(g_glx_ext);
    f = mock_field(&p, end, &n);
    g_glx_ext = mock_dup(f, n);
    f = mock_field(&p, end, &n);
    query = mock_dup(f, n);
    free(buf);
  }
  ok = glewInit() == GLEW_OK;
  sink ^= glxewIsSupported(query);
  /* the *GetExtension query calls the driver; GLEW requires a successful glewInit first */
  if (ok) sink ^= glxewGetExtension(query);
  sink ^= glewIsSupported(query);
  (void)sink;
  free(query);
  return 0;
}
