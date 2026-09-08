#ifndef CLIENT_RULES_SESSION_H
#define CLIENT_RULES_SESSION_H

#include <QJsonObject>

// An initialized Engine serves many queries. Each query owns a fresh visible
// scene; no native object or transient card escapes the JSON boundary.
class ClientRulesSession final
{
public:
    QJsonObject registry() const;
    QJsonObject evaluate(const QJsonObject &input) const;
};

#endif
