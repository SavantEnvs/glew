/*
 * fuzz_glew_init — glewInit() (== glewContextInit() in GLEW_OSMESA mode) against a mock driver
 * whose GL_VERSION / GL_EXTENSIONS / glGetStringi answers come from the fuzz input.
 * Reaches the GL_VERSION parser (_glewStrCLen/_glewInteger, upstream fix b5827f8e), the
 * GL_EXTENSIONS tokenizer (_glewNextSpace/_glewStrCopy), the glGetStringi loop, the extension
 * bsearch and every _glewInit_GL_* loader. Afterwards the public query API is exercised with
 * the driver's own extension names.
 * Input: flags byte, then "<GL_VERSION>\n<GL_EXTENSIONS>" (see mock_driver.h).
 */
#include <GL/glew.h>
#include <GL/osmesa.h>
#include "mock_driver.h"

OSMESAproc OSMesaGetProcAddress(const char *name) { return mock_gl_lookup(name); }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int i;
  volatile GLboolean sink = 0;
  if (size < 1) return 0;
  mock_load_gl(data, size, NULL);
  if (glewInit() == GLEW_OK) {
    for (i = 0; i < g_ntok && i < 64; i++) {
      sink ^= glewGetExtension(g_tokens[i]);
      sink ^= glewIsSupported(g_tokens[i]);
    }
    if (g_version) sink ^= glewIsSupported(g_version);
  }
  (void)sink;
  return 0;
}
