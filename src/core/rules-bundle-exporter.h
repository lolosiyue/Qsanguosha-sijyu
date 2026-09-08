#ifndef QSAN_RULES_BUNDLE_EXPORTER_H
#define QSAN_RULES_BUNDLE_EXPORTER_H
#include <QJsonObject>
class Engine;
namespace QSanRules {
// Captured before Engine loads Lua. Server-only AI is excluded from the closure.
QJsonObject builtinLuaSnapshot();
QJsonObject exportIdentity(const Engine &engine, const QJsonObject &loadedLua);
QJsonObject exportRegistry(const Engine &engine);
}
#endif
