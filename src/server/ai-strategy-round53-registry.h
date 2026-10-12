#ifndef AI_STRATEGY_ROUND53_REGISTRY_H
#define AI_STRATEGY_ROUND53_REGISTRY_H
#include <QJsonObject>
#include <QString>
class Room;class ServerPlayer;struct AIRequest;struct lua_State;
// Native-only finite state. No Lua registry/key/actor selector is exported.
// Requires native observer fanout; no replacement of another observer.
bool initializeAIRound53Registry(Room&,lua_State*,QString*error=nullptr);
void clearAIRound53Registry(Room&);
// Source receipts for the actual R53 writer. Never import a registry value or
// renew a retired stamp. These replace the obsolete shadow-ledger Xufa token.
ServerPlayer *currentAIRound53XufaWriter(Room&) noexcept;
quint64 currentAIRound53XufaSerial(Room&) noexcept;
void retireAIRound53Registry(Room&) noexcept;
// Call BEFORE private applicability/selection at every admitted source policy
// entry. Unknown/foreign actor and nested/reentrant entry revoke unconditionally.
// An outer operation's public actor alone is NOT proof of an inner AI's actor.
void enterAIRound53RegistryPolicy(Room&,ServerPlayer*actualActor,bool reentrant);
// Until the consumer's finite native entry coverage has been reviewed, this
// remains false. It is set only by trusted native bootstrap, never by policy.
void enableAIRound53RegistryEntryCoverage(Room&,bool complete);
QJsonObject writeAIRound53XufaChoice(Room&,ServerPlayer*,const AIRequest&,const QString&);
QJsonObject readAIRound53XufaChoice(Room&,ServerPlayer*,const AIRequest&);
#endif
