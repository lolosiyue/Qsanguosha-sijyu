#ifndef AI_STRATEGY_ROUND40_PLANNING_H
#define AI_STRATEGY_ROUND40_PLANNING_H
#include <QJsonObject>
struct AIRequest; class Room; class ServerPlayer;
// Readonly fixed fcDIY4788 constructor only. No activation/material authority.
bool aiRound40PlanningConstruction(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &);
// Same source's later #tyshouli:onePublicHorse:slash parse. Actual issued scope
// identity and enabled provider are validated by the existing seed endpoint.
bool aiRound40TyshouliSeed(const AIRequest &, Room &, const ServerPlayer *,
    const QString &skill, const QString &prototype, const QJsonObject &proposal);
#endif
