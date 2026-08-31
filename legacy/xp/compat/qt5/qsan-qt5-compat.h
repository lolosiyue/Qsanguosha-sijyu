#ifndef QSAN_QT5_COMPAT_H
#define QSAN_QT5_COMPAT_H

#ifdef __cplusplus

#include <QtCore/qglobal.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QTextStream>

// Qt 6 widened container indexes.  Qt 5.6 containers still use int.
typedef int qsizetype;

// Preserve the Qt 6 spelling at shared call sites.
namespace Qt {
static const QString::SplitBehavior KeepEmptyParts = QString::KeepEmptyParts;
static const QString::SplitBehavior SkipEmptyParts = QString::SkipEmptyParts;
using ::endl;
}

// QRecursiveMutex was introduced after Qt 5.6.  QMutex already implements the
// required recursion semantics when constructed in Recursive mode.
class QRecursiveMutex : public QMutex
{
public:
    QRecursiveMutex()
        : QMutex(QMutex::Recursive)
    {
    }
};

inline QString qEnvironmentVariable(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name));
}

// Shared sources use the Qt 6 QTextStream API.  On Qt 5.6 UTF-8 is selected by
// codec name; the macro keeps the compatibility decision in this one header.
struct QStringConverter
{
    static constexpr const char *Utf8 = "UTF-8";
};
#define setEncoding setCodec

// Qt 5.6 QFontMetrics uses width(); Qt 6 renamed the metric.
#define horizontalAdvance width

// Qt 5.6 exposes the same QImage byte count under its former name.
#define sizeInBytes byteCount

// Qt 5.6 names the indexed QList swap overload swap().
#define swapItemsAt swap

#endif
#endif

#endif
