/* Keep Linux x86_64 executables startable on glibc 2.38 (Ubuntu 24.04, Debian 13).
 *
 * glibc 2.43 gave acosf, atan2f and sqrtf a new symbol version.  Linking on a 2.43
 * host binds the new one, and the GUI then refuses to start anywhere older with
 * "version `GLIBC_2.43' not found", although nothing else it uses needs more than
 * 2.38.  CMakeLists.txt links this file with -Wl,--wrap=acosf,--wrap=atan2f,--wrap=sqrtf,
 * so every call lands in __wrap_* below, which calls the GLIBC_2.2.5 version that
 * every x86_64 libm exports (on an older libm it is simply the only version).
 *
 * Done at link time on purpose: a forced -include carrying __asm__(".symver") is a C
 * token and makes GCC silently ignore the project's precompiled headers.
 *
 * tools/packaging/check-glibc-floor.py (ctest qsanguosha_glibc_floor) names any new
 * symbol that needs pinning: add a declaration, a .symver line, a wrapper here and a
 * --wrap entry in CMakeLists.txt.
 */
float qsan_acosf_glibc225(float);
float qsan_atan2f_glibc225(float, float);
float qsan_sqrtf_glibc225(float);
__asm__(".symver qsan_acosf_glibc225,acosf@GLIBC_2.2.5");
__asm__(".symver qsan_atan2f_glibc225,atan2f@GLIBC_2.2.5");
__asm__(".symver qsan_sqrtf_glibc225,sqrtf@GLIBC_2.2.5");

float __wrap_acosf(float x);
float __wrap_atan2f(float y, float x);
float __wrap_sqrtf(float x);

float __wrap_acosf(float x) { return qsan_acosf_glibc225(x); }
float __wrap_atan2f(float y, float x) { return qsan_atan2f_glibc225(y, x); }
float __wrap_sqrtf(float x) { return qsan_sqrtf_glibc225(x); }
