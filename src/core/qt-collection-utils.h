#ifndef QSAN_QT_COLLECTION_UTILS_H
#define QSAN_QT_COLLECTION_UTILS_H

#include <QSet>
#include <algorithm>

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

// Preserve order while filtering QList/QStringList on both Qt 5.6 and Qt 6.
template<typename Container, typename Predicate>
void qsanRemoveIf(Container &values, Predicate predicate)
{
    values.erase(std::remove_if(values.begin(), values.end(), predicate), values.end());
}

#endif
