/*
 * fuzz_eglew — the GLEW_EGL build: glewInit() = glewContextInit() + eglewInit(display),
 * against a mock GL/EGL driver fed from the fuzz input, then eglewIsSupported(name) /
 * eglewGetExtension(name) / glewIsSupported(name) on a fuzz-supplied query string.
 * Reaches eglewInit's eglInitialize version handling, the EGL_EXTENSIONS scan
 * (_glewSearchExtension), every _glewInit_EGL_* loader, and the EGL name matcher.
 * Input: flags byte (bit3: eglInitialize fails), EGL major byte, EGL minor byte, then
 *        "<GL_VERSION>\n<GL_EXTENSIONS>\n<EGL_EXTENSIONS>\n<query name>".
 */
#include <GL/glew.h>
#include <GL/eglew.h>
#include "mock_driver.h"

static EGLint g_egl_major, g_egl_minor;
static int    g_egl_init_fails;
static char  *g_egl_ext;
static char   g_fake_display_storage[64];

static EGLDisplay EGLAPIENTRY mock_eglGetCurrentDisplay(void) {
  return (EGLDisplay)g_fake_display_storage;
}

static EGLBoolean EGLAPIENTRY mock_eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
  (void)dpy;
  if (g_egl_init_fails) return EGL_FALSE;
  if (major) *major = g_egl_major;
  if (minor) *minor = g_egl_minor;
  return EGL_TRUE;
}

static const char *EGLAPIENTRY mock_eglQueryString(EGLDisplay dpy, EGLint name) {
  (void)dpy;
  return name == EGL_EXTENSIONS ? g_egl_ext : NULL;
}

__eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *name) {
  if (!strcmp(name, "eglGetCurrentDisplay")) return (__eglMustCastToProperFunctionPointerType)mock_eglGetCurrentDisplay;
  if (!strcmp(name, "eglInitialize")) return (__eglMustCastToProperFunctionPointerType)mock_eglInitialize;
  if (!strcmp(name, "eglQueryString")) return (__eglMustCastToProperFunctionPointerType)mock_eglQueryString;
  return (__eglMustCastToProperFunctionPointerType)mock_gl_lookup(name);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const uint8_t *p, *end, *f;
  size_t n;
  char *query;
  uint8_t flags;
  volatile GLboolean sink = 0;
  int ok;
  if (size < 3) return 0;
  /* Precondition from the EGL contract: eglInitialize reports EGL 1.x or later (EGL 1.5 spec
   * §3.2 "major ... of the EGL implementation"; no 0.x EGL exists). GLEW skips loading the EGL 1.0
   * entry points for 0.x and then eglewGetExtension would call through them. */
  if (data[1] == 0) return -1;
  g_egl_major = data[1];
  g_egl_minor = data[2];
  {
    uint8_t *buf = (uint8_t *)malloc(size - 2);
    if (!buf) abort();
    buf[0] = data[0];
    memcpy(buf + 1, data + 3, size - 3);
    p = mock_load_gl(buf, size - 2, &flags);
    end = buf + (size - 2);
    g_egl_init_fails = (flags & 8) != 0;
    free(g_egl_ext);
    f = mock_field(&p, end, &n);
    g_egl_ext = mock_dup(f, n);
    f = mock_field(&p, end, &n);
    query = mock_dup(f, n);
    free(buf);
  }
  ok = glewInit() == GLEW_OK;
  sink ^= eglewIsSupported(query);
  /* the *GetExtension query calls the driver; GLEW requires a successful glewInit first */
  if (ok) sink ^= eglewGetExtension(query);
  sink ^= glewIsSupported(query);
  (void)sink;
  free(query);
  return 0;
}
