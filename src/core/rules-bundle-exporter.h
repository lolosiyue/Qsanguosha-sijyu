#ifndef QSAN_RULES_BUNDLE_EXPORTER_H
#define QSAN_RULES_BUNDLE_EXPORTER_H
#include <QJsonObject>
class Engine;
namespace QSanRules {
// Captured before Engine loads Lua. W2 accepts only the controlled builtin closure.
QJsonObject builtinLuaSnapshot();
QJsonObject exportIdentity(const Engine &engine, const QJsonObject &loadedLua);
QJsonObject exportRegistry(const Engine &engine);
}
#endif
