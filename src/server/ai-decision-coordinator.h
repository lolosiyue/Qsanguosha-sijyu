#ifndef AI_DECISION_COORDINATOR_H
#define AI_DECISION_COORDINATOR_H

#include "ai.h"

#include <QHash>
#include <QSet>

class Room;

class AiDecisionCoordinator
{
public:
    explicit AiDecisionCoordinator(Room &room);

    AIWorldView buildWorldView(ServerPlayer *viewer) const;
    bool isMarkVisibleTo(const ServerPlayer *owner, const QString &mark,
                         const ServerPlayer *viewer) const;
    void setMarkVisibility(const ServerPlayer *owner, const QString &mark, int value,
                           const QList<ServerPlayer *> &viewers);

    AIRequest makeRequest(ServerPlayer *player, AIRequest::DecisionKind kind,
                          CardUseStruct::CardUseReason reason, const QString &pattern,
                          const QString &prompt, Card::HandlingMethod method) const;
    bool buildSkillActionRequest(ServerPlayer *player, const SkillInstance &instance,
                                 CardUseStruct::CardUseReason reason, const QString &pattern,
                                 const QString &prompt, Card::HandlingMethod method,
                                 AIRequest &request) const;
    bool decideSkillAction(ServerPlayer *player, CardUseStruct::CardUseReason reason,
                           const QString &pattern, const QString &prompt,
                           Card::HandlingMethod method, CardUseStruct &cardUse) const;
    bool decide(ServerPlayer *player, const AIRequest &request,
                CardUseStruct &cardUse) const;
    bool applyResult(ServerPlayer *player, const AIRequest &request,
                     const AIResult &result, CardUseStruct &cardUse) const;

    int skillActionInstanceId(ServerPlayer *player, const QString &skillName) const;
    AiLegacyRequestView skillActionContext(ServerPlayer *player, const QString &skillName,
                                           CardUseStruct::CardUseReason reason,
                                           const QString &pattern, const QString &prompt,
                                           Card::HandlingMethod method) const;

private:
    Room &m_room;
    QHash<QString, QSet<QString>> m_markViewers;
};

#endif
