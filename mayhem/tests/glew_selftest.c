/*
 * glew_selftest — known-answer tests for GLEW's public init/query API, run against the same
 * mock driver the fuzz harnesses use (GLEW has no upstream test suite; its only "tests" need a
 * live GL context). Built with normal flags in GLEW_OSMESA mode. Prints TAP lines
 * ("ok N - name" / "not ok N - name"); mayhem/test.sh counts them against the expected total.
 */
#include <GL/glew.h>
#include <GL/osmesa.h>
#include <stdio.h>
#include "../harnesses/mock_driver.h"

OSMESAproc OSMesaGetProcAddress(const char *name) { return mock_gl_lookup(name); }

static int g_n, g_fail;
static void check(int cond, const char *name) {
  ++g_n;
  if (!cond) ++g_fail;
  printf("%sok %d - %s\n", cond ? "" : "not ", g_n, name);
}

static GLenum init_with(uint8_t flags, const char *version, const char *exts) {
  char buf[4096];
  size_t n = (size_t)snprintf(buf, sizeof buf, "%c%s\n%s", (char)flags, version, exts);
  mock_load_gl((const uint8_t *)buf, n, NULL);
  return glewInit();
}

int main(void) {
  GLenum r;
  const char *s;

  r = init_with(2, "4.6.0 NVIDIA 550.54", "GL_ARB_multitexture GL_KHR_debug GL_ARB_bindless_texture");
  check(r == GLEW_OK, "4.6 driver initialises");
  check(GLEW_VERSION_4_6 && GLEW_VERSION_3_0 && GLEW_VERSION_1_1, "4.6 sets every core version flag");
  check(glewGetExtension("GL_KHR_debug") == GL_TRUE, "glGetStringi extension is reported");
  check(glewGetExtension("GL_ARB_sparse_texture") == GL_FALSE, "unlisted extension is not reported");
  check(glewIsSupported("GL_VERSION_4_5 GL_ARB_multitexture") == GL_TRUE, "glewIsSupported list, all present");
  check(glewIsSupported("GL_VERSION_4_5 GL_ARB_sparse_texture") == GL_FALSE, "glewIsSupported list, one missing");
  check(glewIsSupported("GL_NOT_A_REAL_THING") == GL_FALSE, "glewIsSupported unknown name");

  r = init_with(2, "12.1", "");
  check(r == GLEW_OK && GLEW_VERSION_4_6, "multi-digit major version 12.1");

  r = init_with(2, "3.10", "");
  check(r == GLEW_OK && GLEW_VERSION_3_3 && !GLEW_VERSION_4_0, "multi-digit minor version 3.10");

  r = init_with(2, "2.1 Mesa", "GL_ARB_multitexture  GL_EXT_framebuffer_object GL_EXT_bogus");
  check(r == GLEW_OK && GLEW_VERSION_2_1 && !GLEW_VERSION_3_0, "2.1 sets 2.1 but not 3.0");
  check(glewGetExtension("GL_EXT_framebuffer_object") == GL_TRUE, "legacy GL_EXTENSIONS string is tokenised");
  check(glewGetExtension("GL_KHR_debug") == GL_FALSE, "legacy path forgets the previous driver's extensions");
  check(GLEW_EXT_framebuffer_object == GL_TRUE, "extension with resolvable entry points is enabled");

  r = init_with(0, "2.1", "GL_EXT_framebuffer_object");
  check(r == GLEW_OK && GLEW_EXT_framebuffer_object == GL_FALSE, "extension with missing entry points is disabled");

  check(init_with(2, "abc", "") == GLEW_ERROR_NO_GL_VERSION, "non-numeric version is rejected");
  check(init_with(2, "", "") == GLEW_ERROR_NO_GL_VERSION, "empty version is rejected");
  check(init_with(2, "4", "") == GLEW_ERROR_NO_GL_VERSION, "version without a dot is rejected");
  check(init_with(2, ".5", "") == GLEW_ERROR_NO_GL_VERSION, "version without a major is rejected");
  check(init_with(4, "4.6", "") == GLEW_ERROR_NO_GL_VERSION, "NULL GL_VERSION is rejected");
  check(init_with(2, "1.0", "") == GLEW_ERROR_GL_VERSION_10_ONLY, "GL 1.0 is rejected as too old");

  s = (const char *)glewGetErrorString(GLEW_ERROR_NO_GL_VERSION);
  check(s && strcmp(s, "Missing GL version") == 0, "glewGetErrorString(NO_GL_VERSION)");
  s = (const char *)glewGetString(GLEW_VERSION_MAJOR);
  check(s && strcmp(s, "2") == 0, "glewGetString(GLEW_VERSION_MAJOR)");

  mock_free_all();
  printf("1..%d\n", g_n);
  return g_fail ? 1 : 0;
}
