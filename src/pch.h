#ifndef PCH_H
#define PCH_H

#ifdef _MSC_VER
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
#include <QRandomGenerator>
#include <QRegExp>
#include <QTextCodec>
#include <cstdlib>

// Preserve the existing qrand/qsrand call sites and their explicit seeding.
inline QRandomGenerator &qsanRng()
{
    static QRandomGenerator generator(*QRandomGenerator::system());
    return generator;
}

inline int qrand()
{
    return int(qsanRng().bounded(uint(RAND_MAX) + 1u));
}

inline void qsrand(uint seed)
{
    qsanRng().seed(seed);
}

// Include algorithm for std::sort and std::stable_sort.
#include <algorithm>
#include <memory>
#include <utility>

#if __cplusplus < 201402L
namespace std {
template <class T, class... Args>
inline unique_ptr<T> make_unique(Args&&... args)
{
    return unique_ptr<T>(new T(std::forward<Args>(args)...));
}
}
#endif

#ifndef Q_OS_WINRT
#include <QtQml>
#endif

#ifdef AUDIO_SUPPORT
#include <fmod.hpp>
#endif

#endif

#endif // PCH_H
