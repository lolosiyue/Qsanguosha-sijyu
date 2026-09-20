#include "engine-translation-catalog.h"

void EngineTranslationCatalog::add(const QString &key, const QString &value)
{
    if (!m_values.contains(key))
        m_initialValues.insert(key, value);
    m_values.insert(key, value);
}

QString EngineTranslationCatalog::translate(const QString &key, bool initial) const
{
    const auto &catalog = initial ? m_initialValues : m_values;
    return catalog.value(key, key);
}

bool EngineTranslationCatalog::contains(const QString &key) const
{
    return m_values.contains(key);
}

QVariantMap EngineTranslationCatalog::table() const
{
    QVariantMap result;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it)
        result.insert(it.key(), it.value());
    return result;
}
