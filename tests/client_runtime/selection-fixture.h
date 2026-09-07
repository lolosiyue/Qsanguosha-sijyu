#ifndef CLIENT_SELECTION_FIXTURE_H
#define CLIENT_SELECTION_FIXTURE_H

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace ClientRulesFixtures {
// Sorted object keys; array order is significant (subcards, targets and votes).
QByteArray canonicalJson(const QJsonValue &value);

// EngineBootstrap must be initialized and no live room may be registered.
// One client-visible scene per invocation; no socket, TUI or server is started.
// All transient native handles are consumed before a JSON result is returned.
bool run(const QJsonObject &fixture, QJsonObject *output, QString *error);
}

#endif
