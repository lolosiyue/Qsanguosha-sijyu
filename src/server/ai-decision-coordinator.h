#ifndef AI_DECISION_COORDINATOR_H
#define AI_DECISION_COORDINATOR_H

#include "ai.h"
#include "engine.h"

#include <QHash>
#include <QMap>
#include <QSet>

#include <functional>

class Room;
class DistanceSkill;
class SkillRuntimeCoordinator;
class AiDecisionAudit;

class AiDecisionCoordinator
{
    friend class RoomManagedState;
public:
    AiDecisionCoordinator(Room &room, SkillRuntimeCoordinator &skillRuntime);

    AIWorldView buildWorldView(ServerPlayer *viewer, bool compactPolicy = false,
                               bool eventOnly = false) const;
    // Called once per trigger, while the structs are still alive, to keep a bounded
    // value log. Nothing from the QVariant survives the call.
    void recordEvent(int triggerEvent, ServerPlayer *target, const QVariant &data);
    bool isMarkVisibleTo(const ServerPlayer *owner, const QString &mark,
                         const ServerPlayer *viewer) const;
    void setMarkVisibility(const ServerPlayer *owner, const QString &mark, int value,
                           const QList<ServerPlayer *> &viewers);

    AIRequest makeRequestHeader(ServerPlayer *player, AIRequest::DecisionKind kind,
                                CardUseStruct::CardUseReason reason, const QString &pattern,
                                const QString &prompt, Card::HandlingMethod method) const;
    AIRequest makeRequest(ServerPlayer *player, AIRequest::DecisionKind kind,
                          CardUseStruct::CardUseReason reason, const QString &pattern,
                          const QString &prompt, Card::HandlingMethod method) const;
    bool buildCardConversions(ServerPlayer *player, AIRequest &request, int &projectionBudget) const;
    bool buildSkillActionContext(ServerPlayer *player, const SkillInstance &instance,
                                 CardUseStruct::CardUseReason reason, const QString &pattern,
                                 AiSkillActionContext &actionContext) const;
    // project=false builds only the request header and skill context, without board state or candidates; used by read-only activation/instance queries.
    bool buildSkillActionRequest(ServerPlayer *player, const SkillInstance &instance,
                                 CardUseStruct::CardUseReason reason, const QString &pattern,
                                 const QString &prompt, Card::HandlingMethod method,
                                 AIRequest &request, bool project = true) const;
    bool decideSkillAction(ServerPlayer *player, CardUseStruct::CardUseReason reason,
                           const QString &pattern, const QString &prompt,
                           Card::HandlingMethod method, CardUseStruct &cardUse) const;
    bool decide(ServerPlayer *player, const AIRequest &request,
                CardUseStruct &cardUse) const;
    bool applyResult(ServerPlayer *player, const AIRequest &request,
                     const AIResult &result, CardUseStruct &cardUse) const;

    // Value-typed decisions. Each builds an AIRequest, runs the configured route and
    // converts the AIResult back into the answer the gameplay call site expects.
    AIRequest makeChoiceRequest(ServerPlayer *player, AIRequest::DecisionKind kind,
                                const AIChoiceOptions &options) const;
    bool decideSkillInvoke(ServerPlayer *player, const QString &skillName,
                           const QVariant &data, bool &invoked) const;
    bool decideChoice(ServerPlayer *player, const QString &skillName, const QString &choices,
                      const QVariant &data, QString &answer) const;
    bool decideSuit(ServerPlayer *player, const QString &reason, Card::Suit &suit) const;
    bool decideKingdom(ServerPlayer *player, const QString &reason,
                       const QStringList &kingdoms, QString &answer) const;
    bool decideGeneral(ServerPlayer *player, const QStringList &generals,
                       const QString &defaultChoice, const QString &reason,
                       QString &answer) const;
    bool decideDiscard(ServerPlayer *player, const QString &reason, int discardNum,
                       int minNum, bool optional, bool includeEquip, const QString &pattern,
                       const QList<int> &candidates, QList<int> &cards) const;
    bool decideAmazingGrace(ServerPlayer *player, const QList<int> &cardIds, bool refusable,
                            const QString &reason, int &cardId) const;
    bool decideCardChosen(ServerPlayer *player, ServerPlayer *who, const QString &flags,
                          const QString &reason, Card::HandlingMethod method,
                          int &cardId) const;
    bool decideYiji(ServerPlayer *player, const QList<int> &cards, const QString &reason,
                    const QList<ServerPlayer *> &candidates, ServerPlayer *&target,
                    int &cardId) const;
    bool decidePlayerChosen(ServerPlayer *player, const QList<ServerPlayer *> &targets,
                            const QString &reason, ServerPlayer *&choice) const;
    bool decidePlayersChosen(ServerPlayer *player, const QList<ServerPlayer *> &targets,
                             const QString &reason, int maxNum, int minNum,
                             QList<ServerPlayer *> &chosen) const;

    bool decideGuanxing(ServerPlayer *player, const QList<int> &cards, int guanxingType,
                        QList<int> &up, QList<int> &bottom) const;
    bool decideTriggerOrder(ServerPlayer *player, const QString &reason,
                           const QStringList &candidates,
                           QMap<ServerPlayer *, QStringList> &skills, bool optional,
                           const QVariant &data, QString &answer) const;

    // Responses carry physical IDs or issued conversion specs across the isolated
    // boundary. Only the authoritative builder creates native response cards.
    const Card *decideResponseCard(ServerPlayer *player, const QString &pattern,
                                   const QString &prompt, const QVariant &data,
                                   Card::HandlingMethod method) const;
    const Card *decideNullification(ServerPlayer *player, const Card *trick,
                                    ServerPlayer *from, ServerPlayer *to, bool positive) const;
    const Card *decideCardShow(ServerPlayer *player, ServerPlayer *requestor,
                               const QString &reason) const;
    const Card *decidePindian(ServerPlayer *player, ServerPlayer *requestor,
                              const QString &reason) const;
    const Card *decideSinglePeach(ServerPlayer *player, ServerPlayer *dying) const;

    int skillActionInstanceId(ServerPlayer *player, const QString &skillName) const;
    AiLegacyRequestView skillActionContext(ServerPlayer *player, const QString &skillName,
                                           CardUseStruct::CardUseReason reason,
                                           const QString &pattern, const QString &prompt,
                                           Card::HandlingMethod method) const;

private:
    friend struct RoomTestAccess;
    // The legacy answer runs live on this Room, so it is produced on demand and only
    // for the routes that need it.
    typedef std::function<AIResult(const AIRequest &)> LegacyAnswer;
    // fromIsolated says the answer came from the isolated VM. Candidate and count
    // checks only apply to those: a legacy answer keeps the behaviour it had before
    // the route existed.
    bool runAnswer(ServerPlayer *player, const AIRequest &request, const QString &callbackName,
                   const LegacyAnswer &legacy, AIResult &result, AiDecisionAudit &audit,
                   bool *fromIsolated = nullptr) const;
    bool externalAnswer(ServerPlayer *player, const AIRequest &request, AIResult &result,
                        const std::function<bool(const AIResult &)> &validate) const;
    static AIResult legacyAnswerResult(const AIRequest &request, const QString &answer);
    // Above AiLargeRoomAlive players a SmartAI fallback costs minutes per decision.
    // There an isolated fault retries once at double budget, then sets faultContained
    // and the caller declines (call-site default) instead of asking SmartAI.
    AIResult decideIsolatedContained(ServerPlayer *player, const AIRequest &request,
                                     bool &faultContained) const;
    void projectDecisionContext(ServerPlayer *viewer, const QVariant &data,
                                AIRequest &request) const;
    Card *buildSpecCard(ServerPlayer *player, const AIRequest &request,
                        const AICardSpec &spec) const;
    typedef std::function<const Card *()> LegacyCard;
    AIRequest makeResponseRequest(ServerPlayer *player, const QString &question,
                                  const QString &reason, const QString &pattern,
                                  const QString &prompt, Card::HandlingMethod method) const;
    const Card *decideResponse(ServerPlayer *player, const AIRequest &request,
                               const QString &callbackName, const LegacyCard &legacy) const;
    const Card *responseCard(ServerPlayer *player, const AIRequest &request,
                             const AIResult &result) const;

    // Observer-independent facts for one board revision. Hidden cards, roles and
    // policy stay out of this snapshot and are filled per viewer.
    struct PublicPile {
        QString name;
        bool handPile = false;
        QList<int> cardIds;
        QList<AICardView> cards;
    };
    struct PublicSkill {
        AISkillView view;
        SkillInstance instance;
    };
    struct PublicPlayer {
        ServerPlayer *player = nullptr;
        AIPlayerView view;
        QMap<QString, int> marks;
        QList<PublicPile> piles;
        QList<PublicSkill> skills;
        QString kingdom;
        QString hegemonyKingdom;
        bool hasActualGeneral1 = false;
        bool shownOneGeneral = false;
        bool shownGeneral = false;
        bool shownGeneral2 = false;
        QString generalName;
        QString general2Name;
        QString actualGeneral1Name;
        QString actualGeneral2Name;
        QString role;
        bool roleRevealed = false;
        bool armorBasis = false;
        QString activeArmorName;
        bool armorEffectNamesKnown = false;
        QStringList armorEffectNames;
    };
    struct PublicBoard {
        quint64 revision = 0;
        QString modeId;
        QString currentPlayer;
        int currentPhase = int(Player::NotActive);
        QStringList playerOrder;
        QStringList alivePlayerOrder;
        bool customRoles = false;
        QList<PublicPlayer> players;
    };

    PublicBoard publicBoard() const;
    PublicBoard buildPublicBoard() const;
    bool publicBoardCurrent() const;
    // Basic prototype rules per player object name, paired with the cached board.
    QHash<QString, QJsonObject> prototypeRules() const;
    QHash<QString, QJsonObject> buildPrototypeRules() const;
    // Each alive player's distance to each alive lord, paired with the cached board.
    QMap<QString, QMap<QString, int>> lordDistances() const;
    AIWorldView projectWorldView(const PublicBoard &board, ServerPlayer *viewer,
                                 bool compactPolicy, bool eventOnly) const;

    Room &m_room;    SkillRuntimeCoordinator &m_skillRuntime;
    QHash<QString, QSet<QString>> m_markViewers;
    QList<AIEventView> m_events;
    quint64 m_eventSequence = 0;
    // Extend the recordEvent memo across events and requests at the same revision.
    // Never memoize projections: visibility flags, private state and policy are live.
    mutable bool m_publicBoardValid = false;
    mutable PublicBoard m_publicBoard;
    mutable quint64 m_publicBoardGeneration = 0;
    mutable quint64 m_publicBoardDefinitions = 0;
    mutable QList<ServerPlayer *> m_publicBoardPlayers;
    mutable QList<ServerPlayer *> m_publicBoardAlivePlayers;
    mutable quint64 m_publicBoardStamp = 0;
    mutable bool m_prototypeRulesValid = false;
    mutable quint64 m_prototypeRulesStamp = 0;
    mutable QHash<QString, QJsonObject> m_prototypeRules;
    mutable bool m_lordDistancesValid = false;
    mutable quint64 m_lordDistancesStamp = 0;
    mutable QMap<QString, QMap<QString, int>> m_lordDistances;
    // Only the room-wide geometry is shared; hidden cards and policy stay viewer-scoped.
    mutable bool m_distanceCacheValid = false;
    mutable quint64 m_distanceRevision = 0;
    mutable quint64 m_distanceSkillGeneration = 0;
    mutable QList<ServerPlayer *> m_distancePlayers;
    mutable QList<const DistanceSkill *> m_distanceSkills;
    mutable QMap<QString, QMap<QString, int>> m_worldDistances;
    // Same-revision distance/attack-range answers shared by consecutive requests,
    // e.g. every seat asked in one nullification or peach round.
    mutable TargetModMemoScope::Carry m_targetModCarry;
};

#endif
