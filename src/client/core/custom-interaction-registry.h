#ifndef CLIENT_CUSTOM_INTERACTION_REGISTRY_H
#define CLIENT_CUSTOM_INTERACTION_REGISTRY_H

#include <QHash>
#include <QString>
#include <QStringList>

struct CustomInteractionRegistration
{
    int schemaVersion = 1;
    QString rendererId;
};

// QtCore registry that makes custom interaction support explicit. A request is
// never left pending unless a renderer for its type and schema is registered.
class CustomInteractionRegistry
{
public:
    bool registerType(const QString &typeName, int schemaVersion, const QString &rendererId)
    {
        if (typeName.isEmpty() || schemaVersion <= 0 || rendererId.isEmpty())
            return false;
        m_types.insert(typeName, CustomInteractionRegistration { schemaVersion, rendererId });
        return true;
    }

    bool supports(const QString &typeName, int schemaVersion) const
    {
        const QHash<QString, CustomInteractionRegistration>::const_iterator it
            = m_types.constFind(typeName);
        return it != m_types.constEnd() && it->schemaVersion == schemaVersion;
    }

    CustomInteractionRegistration registration(const QString &typeName) const
    {
        return m_types.value(typeName);
    }

    QStringList registeredTypes() const { return m_types.keys(); }

private:
    QHash<QString, CustomInteractionRegistration> m_types;
};

#endif
