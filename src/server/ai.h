#ifndef _AI_H
#define _AI_H

class ResponseSkill;

struct lua_State;

#include "card.h"
#include "lua-runtime.h"
#include "structs.h"

struct AICardView {
    int cardId;
    QString objectName;
    QString className;
    int suit;
    int number;
    QString skillName;

    AICardView() : cardId(-1), suit(int(Card::NoSuit)), number(0) {}
};

struct AISkillView {
    QString skillName;
    int instanceId;
    int source;
    bool invalid;
    bool hasAmountOverride;
    int amount;

    AISkillView()
        : instanceId(0), source(int(SourceInnate)), invalid(false),
          hasAmountOverride(false), amount(0) {}
};

struct AIPlayerView {
    QString objectName;
    int seat;
    int hp;
    int maxHp;
    int handcardCount;
    int phase;
    bool alive;
    bool removed;
    bool faceUp;
    bool chained;
    QString kingdom;
    QString role;
    QString generalName;
    QString general2Name;
    QList<AICardView> equips;
    QList<AICardView> judgingArea;
    QMap<QString, int> publicMarks;
    QList<AISkillView> skills;

    AIPlayerView()
        : seat(0), hp(0), maxHp(0), handcardCount(0), phase(int(Player::NotActive)),
          alive(false), removed(false), faceUp(true), chained(false) {}
};

struct AIWorldView {
    quint64 revision;
    AIPlayerView self;
    QList<AIPlayerView> players;
    QList<AICardView> handCards;
    QString currentPlayer;
    int currentPhase;

    AIWorldView() : revision(0), currentPhase(int(Player::NotActive)) {}
};

struct AiSkillActionContext {
    SkillInstanceRef activationRef;
    SkillInstanceRef sourceRef;
    bool activationQuotaAvailable;
    bool sourceQuotaAvailable;

    AiSkillActionContext()
        : activationQuotaAvailable(false), sourceQuotaAvailable(false) {}

    bool isValid() const { return activationRef.isValid() && sourceRef.isValid(); }
    QString getActivationOwner() const { return activationRef.ownerObjectName; }
    QString getActivationSkillName() const { return activationRef.key.skillName; }
    int getActivationInstanceId() const { return activationRef.key.instanceID; }
    QString getSourceOwner() const { return sourceRef.ownerObjectName; }
    QString getSourceSkillName() const { return sourceRef.key.skillName; }
    int getSourceInstanceID() const { return sourceRef.key.instanceID; }
    bool isActivationQuotaAvailable() const { return activationQuotaAvailable; }
    bool isSourceQuotaAvailable() const { return sourceQuotaAvailable; }
};

struct AIRequest {
    enum DecisionKind { Activate, UseCard };

    DecisionKind kind;
    quint64 decisionId;
    quint64 stateRevision;
    QString viewerObjectName;
    CardUseStruct::CardUseReason reason;
    QString pattern;
    QString prompt;
    Card::HandlingMethod handlingMethod;
    AIWorldView worldView;
    bool hasSkillActionContext;
    AiSkillActionContext skillActionContext;

    AIRequest()
        : kind(UseCard), decisionId(0), stateRevision(0),
          reason(CardUseStruct::CARD_USE_REASON_UNKNOWN), handlingMethod(Card::MethodUse),
          hasSkillActionContext(false) {}

    bool isValid() const { return !viewerObjectName.isEmpty(); }
    QString getDecisionId() const { return QString::number(decisionId); }
    QString getStateRevision() const { return QString::number(stateRevision); }
    int getDecisionKind() const { return int(kind); }
    CardUseStruct::CardUseReason getReason() const { return reason; }
    QString getPattern() const { return pattern; }
    QString getPrompt() const { return prompt; }
    Card::HandlingMethod getHandlingMethod() const { return handlingMethod; }
    bool hasSkillAction() const { return hasSkillActionContext; }
    QString getActivationOwner() const { return skillActionContext.getActivationOwner(); }
    QString getActivationSkillName() const { return skillActionContext.getActivationSkillName(); }
    int getActivationInstanceId() const { return skillActionContext.getActivationInstanceId(); }
    QString getSourceOwner() const { return skillActionContext.getSourceOwner(); }
    QString getSourceSkillName() const { return skillActionContext.getSourceSkillName(); }
    int getSourceInstanceID() const { return skillActionContext.getSourceInstanceID(); }
    bool isActivationQuotaAvailable() const {
        return hasSkillActionContext && skillActionContext.isActivationQuotaAvailable();
    }
    bool isSourceQuotaAvailable() const {
        return hasSkillActionContext && skillActionContext.isSourceQuotaAvailable();
    }
};

struct AiLegacyRequestView {
    AIRequest request;
    ServerPlayer *initiator;

    AiLegacyRequestView() : initiator(nullptr) {}
    AiLegacyRequestView(const AIRequest &request, ServerPlayer *initiator)
        : request(request), initiator(initiator) {}

    bool isValid() const { return request.isValid() && initiator; }
    QString getDecisionId() const { return request.getDecisionId(); }
    QString getStateRevision() const { return request.getStateRevision(); }
    int getDecisionKind() const { return request.getDecisionKind(); }
    CardUseStruct::CardUseReason getReason() const { return request.getReason(); }
    QString getPattern() const { return request.getPattern(); }
    QString getPrompt() const { return request.getPrompt(); }
    Card::HandlingMethod getHandlingMethod() const { return request.getHandlingMethod(); }
    ServerPlayer *getInitiator() const { return initiator; }
    QString getActivationOwner() const { return request.getActivationOwner(); }
    QString getActivationSkillName() const { return request.getActivationSkillName(); }
    int getActivationInstanceId() const { return request.getActivationInstanceId(); }
    QString getSourceOwner() const { return request.getSourceOwner(); }
    QString getSourceSkillName() const { return request.getSourceSkillName(); }
    int getSourceInstanceID() const { return request.getSourceInstanceID(); }
    bool isActivationQuotaAvailable() const { return request.isActivationQuotaAvailable(); }
    bool isSourceQuotaAvailable() const { return request.isSourceQuotaAvailable(); }
};

struct CardActionSpec {
    QString legacyCardString;
    QList<int> selectedCardIds;
    QStringList selectedTargetNames;
    QString userString;
    bool hasSkillActionContext;
    AiSkillActionContext skillActionContext;

    CardActionSpec() : hasSkillActionContext(false) {}
};

struct AIResult {
    enum ActionKind { Pass, UseCard };

    ActionKind kind;
    bool handled;
    quint64 decisionId;
    quint64 stateRevision;
    CardActionSpec action;
    QString errorCode;

    AIResult() : kind(Pass), handled(false), decisionId(0), stateRevision(0) {}
};

class AI : public QObject
{
    Q_OBJECT
    Q_ENUMS(Relation)

public:
    AI(ServerPlayer *player);

    enum Relation
    {
        Friend, Enemy, Neutrality
    };
    static Relation GetRelation3v3(const ServerPlayer *a, const ServerPlayer *b);
    static Relation GetRelationHegemony(const ServerPlayer *a, const ServerPlayer *b);
    static Relation GetRelation(const ServerPlayer *a, const ServerPlayer *b);
    Relation relationTo(const ServerPlayer *other) const;
    bool isFriend(const ServerPlayer *other) const;
    bool isEnemy(const ServerPlayer *other) const;

    QList<ServerPlayer *> getEnemies() const;
    QList<ServerPlayer *> getFriends() const;

    virtual AIResult decide(const AIRequest &request);
    virtual void activate(CardUseStruct &card_use) = 0;
    virtual Card::Suit askForSuit(const QString &reason) = 0;
    virtual QString askForKingdom(QStringList kingdoms) = 0;
    virtual bool askForSkillInvoke(const QString &skill_name, const QVariant &data) = 0;
    virtual QString askForChoice(const QString &skill_name, const QString &choices, const QVariant &data) = 0;
    virtual QString askForTriggerOrder(const QString &reason, QMap<ServerPlayer*, QStringList> &skills,
                                      bool optional, const QVariant &data) = 0;
    virtual QList<int> askForDiscard(const QString &reason, int discard_num, int min_num, bool optional, bool include_equip, const QString &pattern = ".") = 0;
    virtual const Card *askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to, bool positive) = 0;
    virtual int askForCardChosen(ServerPlayer *who, const QString &flags, const QString &reason, Card::HandlingMethod method) = 0;
    virtual const Card *askForCard(const QString &pattern, const QString &prompt, const QVariant &data, const Card::HandlingMethod method) = 0;
    virtual QString askForUseCard(const QString &pattern, const QString &prompt, const Card::HandlingMethod method) = 0;
    virtual int askForAG(const QList<int> &card_ids, bool refusable, const QString &reason) = 0;
    virtual const Card *askForCardShow(ServerPlayer *requestor, const QString &reason) = 0;
    virtual const Card *askForPindian(ServerPlayer *requestor, const QString &reason) = 0;
    virtual ServerPlayer *askForPlayerChosen(const QList<ServerPlayer *> &targets, const QString &reason) = 0;
    virtual QList<ServerPlayer *> askForPlayersChosen(const QList<ServerPlayer *> &targets, const QString &reason, int max_num, int min_num) = 0;
    virtual const Card *askForSinglePeach(ServerPlayer *dying) = 0;
    virtual ServerPlayer *askForYiji(const QList<int> &cards, const QString &reason, int &card_id) = 0;
    virtual void askForGuanxing(const QList<int> &cards, QList<int> &up, QList<int> &bottom, int guanxing_type) = 0;
    virtual QString askForGeneral(const QStringList &generals, const QString &default_choice = QString(), const QString &reason = QString()) = 0;
    virtual void filterEvent(TriggerEvent triggerEvent, ServerPlayer *player, const QVariant &data);

protected:
    Room *room;
    ServerPlayer *self;
};

class TrustAI : public AI
{
    Q_OBJECT

public:
    TrustAI(ServerPlayer *player);

    virtual void activate(CardUseStruct &card_use);
    virtual Card::Suit askForSuit(const QString &);
    virtual QString askForKingdom(QStringList kingdoms);
    virtual bool askForSkillInvoke(const QString &skill_name, const QVariant &data);
    virtual QString askForChoice(const QString &skill_name, const QString &choices, const QVariant &data);
    virtual QString askForTriggerOrder(const QString &reason, QMap<ServerPlayer*, QStringList> &skills,
                                      bool optional, const QVariant &data);
    virtual QList<int> askForDiscard(const QString &reason, int discard_num, int min_num, bool optional, bool include_equip, const QString &pattern = ".");
    virtual const Card *askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to, bool positive);
    virtual int askForCardChosen(ServerPlayer *who, const QString &flags, const QString &reason, Card::HandlingMethod method);
    virtual const Card *askForCard(const QString &pattern, const QString &prompt, const QVariant &data, const Card::HandlingMethod method);
    virtual QString askForUseCard(const QString &pattern, const QString &prompt, const Card::HandlingMethod method);
    virtual int askForAG(const QList<int> &card_ids, bool refusable, const QString &reason);
    virtual const Card *askForCardShow(ServerPlayer *requestor, const QString &reason);
    virtual const Card *askForPindian(ServerPlayer *requestor, const QString &reason);
    virtual ServerPlayer *askForPlayerChosen(const QList<ServerPlayer *> &targets, const QString &reason);
    virtual QList<ServerPlayer *> askForPlayersChosen(const QList<ServerPlayer *> &targets, const QString &reason, int max_num, int min_num);
    virtual const Card *askForSinglePeach(ServerPlayer *dying);
    virtual ServerPlayer *askForYiji(const QList<int> &cards, const QString &reason, int &card_id);
    virtual void askForGuanxing(const QList<int> &cards, QList<int> &up, QList<int> &bottom, int guanxing_type);
    virtual QString askForGeneral(const QStringList &generals, const QString &default_choice = QString(), const QString &reason = QString());

    virtual bool useCard(const Card *card);

private:
    ResponseSkill *response_skill;
};

class LuaAI : public TrustAI
{
    Q_OBJECT

public:
    LuaAI(ServerPlayer *player);

    virtual AIResult decide(const AIRequest &request);
    virtual const Card *askForCardShow(ServerPlayer *requestor, const QString &reason);
    virtual bool askForSkillInvoke(const QString &skill_name, const QVariant &data);
    virtual void activate(CardUseStruct &card_use);
    virtual QString askForUseCard(const QString &pattern, const QString &prompt, const Card::HandlingMethod method);
    virtual QList<int> askForDiscard(const QString &reason, int discard_num, int min_num, bool optional, bool include_equip, const QString &pattern = ".");
    virtual const Card *askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to, bool positive);
    virtual QString askForChoice(const QString &skill_name, const QString &choices, const QVariant &data);
    virtual int askForCardChosen(ServerPlayer *who, const QString &flags, const QString &reason, Card::HandlingMethod method);
    virtual const Card *askForCard(const QString &pattern, const QString &prompt, const QVariant &data, const Card::HandlingMethod method);
    virtual ServerPlayer *askForPlayerChosen(const QList<ServerPlayer *> &targets, const QString &reason);
    virtual QList<ServerPlayer *> askForPlayersChosen(const QList<ServerPlayer *> &targets, const QString &reason, int max_num, int min_num);
    virtual int askForAG(const QList<int> &card_ids, bool refusable, const QString &reason);
    virtual const Card *askForSinglePeach(ServerPlayer *dying);
    virtual const Card *askForPindian(ServerPlayer *requestor, const QString &reason);
    virtual Card::Suit askForSuit(const QString &reason);

    virtual ServerPlayer *askForYiji(const QList<int> &cards, const QString &reason, int &card_id);
    virtual void askForGuanxing(const QList<int> &cards, QList<int> &up, QList<int> &bottom, int guanxing_type);
    virtual QString askForGeneral(const QStringList &generals, const QString &default_choice = QString(), const QString &reason = QString());

    virtual void filterEvent(TriggerEvent triggerEvent, ServerPlayer *player, const QVariant &data);

    LuaFunction callback;

private:
    void pushCallback(lua_State *L, const char *function_name);
    void pushQIntList(lua_State *L, const QList<int> &list);
    bool getTable(lua_State *L, QList<int> &table);
};

#endif
