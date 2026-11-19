/* Minimal stand-in for Mesa's <GL/osmesa.h>: glew.c in GLEW_OSMESA mode only needs the
 * OSMesaGetProcAddress declaration. The harness (mock GL driver) defines the function. */
#ifndef MAYHEM_STUB_OSMESA_H
#define MAYHEM_STUB_OSMESA_H
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*OSMESAproc)(void);
OSMESAproc OSMesaGetProcAddress(const char *funcName);
#ifdef __cplusplus
}
#endif
#endif
