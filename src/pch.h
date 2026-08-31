#ifndef PCH_H
#define PCH_H

#if defined(_MSC_VER) && !defined(QSAN_XP_LEGACY)
#pragma execution_character_set("utf-8")
#endif

//#define LOGNETWORK

#ifndef ANDROID
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

// bundled FMOD header 只喺 Windows Release 的 include path。AUDIO_SUPPORT 而家
// 淨係代表「有 audio facade」，所以呢度要用 backend 專屬的定義。
#ifdef QSAN_AUDIO_BACKEND_FMOD
#include <fmod.hpp>
#endif

#endif

#endif // PCH_H
