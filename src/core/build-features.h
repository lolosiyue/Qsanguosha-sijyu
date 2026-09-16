#ifndef QSAN_BUILD_FEATURES_H
#define QSAN_BUILD_FEATURES_H

#ifndef QSAN_ENABLE_QML
#define QSAN_ENABLE_QML 1
#endif

#ifndef QSAN_ENABLE_SPINE
#define QSAN_ENABLE_SPINE 1
#endif

#ifndef QSAN_ENABLE_VIDEO
#define QSAN_ENABLE_VIDEO 1
#endif

#ifndef QSAN_ENABLE_WEBSOCKETS
#define QSAN_ENABLE_WEBSOCKETS 1
#endif

#ifndef QSAN_USE_RASTER_VIEWPORT
#if defined(__ANDROID__)
// Keep Android QWidget composition independent of the lifecycle of EGL surfaces.
#define QSAN_USE_RASTER_VIEWPORT 1
#else
#define QSAN_USE_RASTER_VIEWPORT 0
#endif
#endif

#endif
