#ifndef QSAN_QT_COLLECTION_UTILS_H
#define QSAN_QT_COLLECTION_UTILS_H

#include <QSet>

// QSet gained an iterator-range constructor after the Qt 5.6 baseline.
// Keep the call sites source-compatible without changing container semantics.
template<typename Container>
QSet<typename Container::value_type> qsanToSet(const Container &values)
{
    QSet<typename Container::value_type> result;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        result.insert(*it);
    return result;
}

#endif
