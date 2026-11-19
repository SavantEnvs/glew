/*
 * fuzz_glew_query — glewIsSupported(name) / glewGetExtension(name) on a fuzz-supplied query
 * string, after a one-time glewInit() against a fixed, valid mock driver (GL 4.6 core path,
 * every entry point resolvable). Reaches the generated extension-name matcher
 * (_glewStrSame1/2/3 over ~900 names), _glewStrLen and the _glewBsearchExtension lookup.
 * The init state is read-only afterwards: the queries do not modify GLEW globals.
 * Input: the query string (exact-size NUL-terminated heap copy).
 */
#include <GL/glew.h>
#include <GL/osmesa.h>
#include <stdio.h>
#include "mock_driver.h"

OSMESAproc OSMesaGetProcAddress(const char *name) { return mock_gl_lookup(name); }

static const char k_init[] =
  "\x02" "4.6 Mock\n"
  "GL_ARB_multitexture GL_ARB_vertex_buffer_object GL_EXT_texture_filter_anisotropic "
  "GL_ARB_debug_output GL_KHR_debug GL_NV_mesh_shader GL_AMD_gpu_shader_half_float "
  "GL_ARB_bindless_texture GL_EXT_framebuffer_object GL_ARB_sparse_texture";

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  GLenum r;
  (void)argc; (void)argv;
  mock_load_gl((const uint8_t *)k_init, sizeof(k_init) - 1, NULL);
  r = glewInit();
  if (r != GLEW_OK) {
    fprintf(stderr, "fuzz_glew_query: mock glewInit failed: %s\n", glewGetErrorString(r));
    abort();
  }
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  volatile GLboolean sink;
  char *name = mock_dup(data, size);
  sink = glewIsSupported(name);
  sink ^= glewGetExtension(name);
  (void)sink;
  free(name);
  return 0;
}
