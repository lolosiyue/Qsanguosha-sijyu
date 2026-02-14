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

// Qt 5.14 compatibility: Include algorithm for std::sort and std::stable_sort
#include <algorithm>

#ifndef Q_OS_WINRT
#include <QtQml>
#endif

#ifdef AUDIO_SUPPORT
#include <fmod.hpp>
#endif

#endif

#endif // PCH_H