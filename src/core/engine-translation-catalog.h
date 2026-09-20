#ifndef QSAN_ENGINE_TRANSLATION_CATALOG_H
#define QSAN_ENGINE_TRANSLATION_CATALOG_H

#include <QHash>
#include <QString>
#include <QVariantMap>

// Owns the mutable engine-level catalog. RoomRuntime overlays remain the
// authoritative per-room source; this class only stores the bootstrap catalog
// and its initial snapshot, preserving the existing Engine facade semantics.
class EngineTranslationCatalog final
{
public:
    void add(const QString &key, const QString &value);
    QString translate(const QString &key, bool initial = false) const;
    bool contains(const QString &key) const;
    QVariantMap table() const;

private:
    QHash<QString, QString> m_values;
    QHash<QString, QString> m_initialValues;
};

#endif
