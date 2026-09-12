#ifndef PCH_H
#define PCH_H

#include <QtGlobal>

#if defined(_MSC_VER) && !defined(QSAN_XP_LEGACY)
#pragma execution_character_set("utf-8")
#endif

//#define LOGNETWORK

#ifndef Q_OS_ANDROID
#include <ft2build.h>
#endif

#ifdef __cplusplus

//#include <QtCore>
//#include <QtNetwork>
//#include <QtGui>
#include <QtWidgets>

// Qt 6 no longer exposes these compatibility APIs through umbrella headers.
#include "game-rng.h"

// Include algorithm for std::sort and std::stable_sort.
#include <algorithm>
#include <memory>
#include <utility>

#ifndef QSAN_ENABLE_QML
#define QSAN_ENABLE_QML 1
#endif

#if __cplusplus < 201402L
namespace std {
template <class T, class... Args>
inline unique_ptr<T> make_unique(Args&&... args)
{
    return unique_ptr<T>(new T(std::forward<Args>(args)...));
}
}
#endif

#if !defined(Q_OS_WINRT) && QSAN_ENABLE_QML
#include <QtQml>
#endif

// The bundled FMOD header is only on the Windows Release include path. AUDIO_SUPPORT now
// only means "an audio facade exists", so a backend-specific define is needed here.
#ifdef QSAN_AUDIO_BACKEND_FMOD
#include <fmod.hpp>
#endif

#endif

#endif // PCH_H
