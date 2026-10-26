#ifndef _AI_H
#define _AI_H

class ResponseSkill;

struct lua_State;

#include "card.h"
#include "lua-runtime.h"
#include "structs.h"

#include <QJsonArray>
#include <QJsonObject>

struct AICardView {
    int cardId;
    int effectiveId;
    QString objectName;
    QString className;
    int suit;
    int number;
    QString skillName;
    // Card classification and structure. subcardIds is empty for a concrete card.
    int typeId;
    int equipSlot;
    int weaponRange = -1;
    int handlingMethod;
    bool virtualCard;
    bool targetFixed;
    bool damageCard;
    QList<int> subcardIds;
    bool red;
    bool black;
    QStringList kindOfNames;
    // Card::toString, which strategies also sort by. Only a physical card carries it:
    // a virtual card's string can name hidden costs.
    QString legacyStringKey;
    int nameLength = -1;
    QString sourceSkillName;
    bool canRecast = false;
    bool willThrow = false;
    bool singleTarget = false;
    // Owner-only state of the viewer's own physical cards; unknown otherwise.
    bool ownerStateKnown = false;
    QStringList skillNames;
    QString engineClassName;
    bool locked = false;
    bool jilei = false;
    bool usingCard = false;
    // Owner-only rules, projected for decision requests only (ownerRulesKnown):
    // isCardLimited per handling method (outside and inside the hand) and
    // Room::isProhibited per alive target.
    bool ownerRulesKnown = false;
    QMap<int, bool> limits;
    QMap<int, bool> limitsHand;
    QMap<QString, bool> prohibited;
    // Card flags are broadcast by Room::setCardFlag.
    QStringList flags;
    // Owner-only: the engine card is a gift and still shows its printed face.
    bool gift = false;
    // Owner-only: Card::isAvailable for the owner, and its TargetModSkill
    // corrections with no specific target.
    bool available = false;
    int targetModResidue = 0;
    int targetModDistance = 0;
    int targetModExtra = 0;
    // TargetModSkill::Residue per alive target, for the owner's own damage cards and
    // self-targeting basic cards (the cards whose use count is limited).
    QMap<QString, int> targetResidue;

    AICardView()
        : cardId(-1), effectiveId(-1), suit(int(Card::NoSuit)), number(0),
          typeId(int(Card::TypeSkill)), equipSlot(-1), handlingMethod(int(Card::MethodNone)),
          virtualCard(false), targetFixed(false), damageCard(false),
          red(false), black(false) {}
};

// A named pile. The count is what the viewer may know about its size; the ids only
// appear when the pile is open to this viewer. An open pile with no cards is a known
// empty pile, which is not the same as a closed one.
struct AICardPileView {
    QString name;
    int count;
    bool open;
    bool handPile;
    QList<int> cardIds;

    // Metadata follows the same viewer visibility as cardIds; never native cards.
    QList<AICardView> cards;

    AICardPileView() : count(0), open(false), handPile(false) {}
};

// What one card of the viewer may legally do in the request being asked. The lists are
// computed once by the authority: the isolated side never queries the Engine live.
// legalTargets holds the targets that pass targetFilter and the prohibition skills
// with nothing else selected yet; picking one may narrow the rest, which is why
// targetCombinations carries the ordered action space when completeCoverage is true.
struct AICardCandidateView {
    // Request-local authorization token. It is valid only for the decisionId and the
    // state revision that issued it, and an answer must name one this request actually
    // offered - holding a card is not by itself permission to play it in answer to
    // *this* question.
    int candidateId;
    int cardId;
    // Whether this card answers *this* question. In Play that is Card::isAvailable;
    // in a response it is the request pattern. The two are not interchangeable - a
    // Jink is never "available" in Play and is still the only legal answer to a Slash.
    bool available;
    bool limited;
    bool jilei;
    bool targetFixed;
    // How many times one target may be picked, exactly as Card::targetFilter reports it
    // per target. This is a vote count, not a target count: Fire Attack style cards
    // return 3 for the same player. Absent means one.
    QMap<QString, int> maxVotes;
    QStringList legalTargets;
    // Auto-target roster for standard AOE/global cards; independent of the explicit
    // selection sequence, which is {{}} for a target-fixed action.
    bool affectedTargetsKnown = false;
    QStringList affectedTargets;
    // Ordered feasible sequences; incomplete projections are never a truncated answer.
    QList<QStringList> targetCombinations;
    // targetsFeasible({}) - an action that is complete with no target at all, which is
    // a different answer from "no legal target was found".
    bool feasibleWithNoTarget;
    // True only after every ordered selection has been explored within the projection
    // budget (or for target-fixed cards). Missing combinations are unsupported.
    bool completeCoverage;
    // TargetModSkill::ExtraTarget corrections, overall and per alive target, and
    // Card::targetFilter with an empty prefix per alive target.
    bool extraTargetKnown = false;
    int extraTargetCount = 0;
    QMap<QString, int> extraTargetByPlayer;
    QMap<QString, bool> targetFilterEmpty;

    AICardCandidateView()
        : candidateId(-1), cardId(-1), available(false), limited(false), jilei(false),
          targetFixed(false), feasibleWithNoTarget(false), completeCoverage(false) {}
};

// One conversion this request authorizes: a card the authority itself built by asking
// a view-as skill the player actually holds, together with the exact cost it was built
// from. The AI never turns a name into a card - it names a conversionId from this list
// and the authority rebuilds the same card from its own record. A card name, a
// class_name or a skill name supplied by an author is a request, never a permission.
struct AICardConversionView {
    int conversionId;
    // The produced card, taken from the skill's own createCard() and that card's
    // meta-object chain - the controlled catalogue, not anything the AI supplied.
    QString name;
    QString className;
    QString skillName;
    QStringList kindOfNames;
    int suit;
    int number;
    // The whole instance identity. A bare skill name cannot address a skill that is
    // held more than once, and it says nothing about whose quota pays for the use.
    SkillInstanceRef activationRef;
    SkillInstanceRef sourceRef;
    // Equipment buttons have a native source receipt rather than a skill instance.
    bool equipmentActivation = false;
    PhysicalEquipSource physicalEquipSource;
    bool activationQuotaAvailable;
    bool sourceQuotaAvailable;
    // One ticket can describe choose(costCount) independent hand costs without subsets.
    // Zero costCount retains the exact-subcards contract.
    int costCount = 0;
    QList<int> eligibleSubcardIds;
    // The exact cost cards this conversion was enumerated with, in order.
    QList<int> subcardIds;
    // The same target description a physical candidate carries, so that one planning
    // algorithm serves both instead of a second one growing for converted cards.
    bool available;
    // A response or use question: the produced card matches the question's pattern.
    bool matchesResponsePattern = false;
    QString legacyStringKey; // The produced card's Card::toString().
    bool locked = false; // Player::isLocked of the produced card.
    bool targetFixed;
    bool feasibleWithNoTarget;
    bool completeCoverage;
    QStringList legalTargets;
    bool affectedTargetsKnown = false;
    QStringList affectedTargets;
    // Ordered feasible sequences; incomplete projections are never a truncated answer.
    QList<QStringList> targetCombinations;
    QMap<QString, int> maxVotes;
    // The same target corrections and empty-prefix filters as a candidate.
    bool extraTargetKnown = false;
    int extraTargetCount = 0;
    QMap<QString, int> extraTargetByPlayer;
    QMap<QString, bool> targetFilterEmpty;

    AICardConversionView()
        : conversionId(-1), suit(int(Card::SuitToBeDecided)), number(0),
          activationQuotaAvailable(false), sourceQuotaAvailable(false),
          available(false), targetFixed(false), feasibleWithNoTarget(false),
          completeCoverage(false) {}

    bool isValid() const { return conversionId >= 0 && !name.isEmpty(); }
};

struct AISkillView {
    QString skillName;
    int instanceId;
    int source;
    bool invalid;
    bool hasAmountOverride;
    int amount;
    bool hasPrivateState;
    bool hasViewAsSkill = false;
    // Public definition text, the limit mark of a limited skill and the trigger
    // events of a trigger skill.
    QString description;
    QString limitMark;
    QList<int> triggerEvents;
    // Owner-only: whether the view-as part is a V2 skill, ViewAsSkill::isEnabledAtPlay,
    // and isEnabledAtResponse for the basic response patterns SmartAI asks about.
    bool viewAsKnown = false;
    bool viewAsV2 = false;
    bool enabledAtPlay = false;
    QMap<QString, bool> enabledAtResponse;
    // ViewAsEquipSkill::viewAsEquip for the holder: the equipment names it grants.
    bool viewAsEquipKnown = false;
    QStringList viewAsEquipNames;
    // Skill classification: the native class chain from the leaf up, so a legacy
    // inherits("FilterSkill") check becomes a value comparison.
    QStringList skillClasses;
    int frequency; // Skill::Frequency; 0 when the skill has no instance data
    bool lordSkill;
    bool attachedLordSkill;
    bool lordSkillEffective;
    QJsonObject state;
    QJsonObject correctState;

    AISkillView()
        : instanceId(0), source(int(SourceInnate)), invalid(false),
          hasAmountOverride(false), amount(0), hasPrivateState(false),
          frequency(0), lordSkill(false),
          attachedLordSkill(false), lordSkillEffective(false) {}
};

struct AIPlayerView {
    QString objectName;
    int seat;
    int hp;
    int maxHp;
    int handcardCount;
    int phase;
    bool alive;
    bool dead;
    bool removed;
    bool inYinniState = false;
    // ServerPlayer turn lifecycle: Lua derives -Clear / -PlayClear epochs from these.
    bool turnOpen = false;
    bool playOpen = false;
    int attackRangeWithoutWeapon = 1;
    // Basic card prototypes (Slash, Jink, Peach, Analeptic, Nullification) for this
    // player: isCardLimited for use and response, and isAvailable.
    QJsonObject prototypeRules;
    // Public zone capabilities and change-skill states (shown in every client's UI).
    bool hasJudgeArea = true;
    QList<int> equipAreas;
    QMap<QString, int> changeSkillStates;
    bool kongcheng;
    bool wounded;
    bool faceUp;
    bool chained;
    QString kingdom;
    QString role;
    bool roleRevealed = false;
    bool roleVisible = false;
    QString controller;
    QString generalName;
    QString general2Name;
    QList<AICardView> equips;
    QList<AICardView> judgingArea;
    QMap<QString, int> publicMarks;
    // The viewer sees every mark it owns, so a missing mark is zero, not unknown.
    bool marksComplete = false;
    QList<AISkillView> skills;
    // False when hegemony hides some of this player's skills from the viewer.
    bool skillsComplete = true;
    QString playerState;
    // Hand cards of another player that this viewer may see. The viewer's own hand is
    // world.handCards; handVisible says the whole hand is open, so a card missing from
    // knownCards is known absent rather than unknown.
    QList<AICardView> knownCards;
    bool handVisible;
    // Flags are broadcast to every client, and so is every phase skip.
    QStringList privateFlags;
    bool privateFlagsVisible = false;
    QMap<int, bool> skippedPhases;
    QString activeArmorName;
    bool armorEffectKnown = false;
    // With a view-as equipment source: each armor name (physical or granted) for
    // which hasArmorEffect holds.
    QStringList armorEffectNames;
    bool armorEffectNamesKnown = false;
    QList<AICardPileView> piles;
    QList<int> displayCards;
    // Derived player values the shared entries ask for.
    int maxCards;
    int hujia;
    int attackRange;
    int gender;
    bool lord;
    // Equip slot index -> occupying card id; a free slot is simply absent.
    QMap<int, int> equipSlots;
    // The viewer's own usage history (usedTimes per history key); others are unknown.
    QMap<QString, int> usedHistory;
    bool usedHistoryVisible = false;
    // The viewer's own tags and dynamic properties as JSON-safe values; a value that
    // cannot be represented is left out. Other players' are unknown.
    QJsonObject tags;
    QJsonObject properties;
    bool ownStateVisible = false;
    bool canSlashWithoutCrossbow = false;
    // Player::canDiscard(to, flags) of the viewer, per player and zone string.
    QMap<QString, QMap<QString, bool>> canDiscard;

    AIPlayerView()
        : seat(0), hp(0), maxHp(0), handcardCount(0), phase(int(Player::NotActive)),
          alive(false), dead(true), removed(false), kongcheng(true), wounded(false),
          faceUp(true), chained(false), handVisible(false), maxCards(0), hujia(0),
          attackRange(1), gender(int(General::Sexless)), lord(false) {}
};

// One gameplay event, already turned into values. The sequence orders events within a
// Room and the revision says which board state they belong to. Card ids are split:
// cardIds are public, privateCardIds only reach privateViewer.
struct AIEventView {
    quint64 sequence;
    quint64 revision;
    int triggerEvent;
    QString kind;
    QString from;
    QString to;
    QStringList targets;
    QString cardName;
    QString cardClass;
    QString cardSkill;
    bool chain = false;
    bool transfer = false;
    bool byUser = true;
    bool intentionSuppressed = false;
    QJsonObject details;
    QString reason;
    QList<int> cardIds;
    QList<int> privateCardIds;
    QString privateViewer;
    // ChoiceMade can contain a private answer; unlike public events with a hidden
    // card list, the whole event is then visible only to privateViewer.
    bool privateEvent = false;
    int amount;
    int nature;
    int place;
    bool good;

    AIEventView()
        : sequence(0), revision(0), triggerEvent(0), amount(0), nature(0),
          place(int(Player::PlaceUnknown)), good(false) {}
};

struct AIWorldView {
    quint64 revision;
    QString modeId;
    bool customRoles = false;
    QJsonObject modePolicy;
    // Viewer-scoped national-war facts. Knowing one's own general is distinct
    // from having publicly revealed that general.
    QJsonObject hegemonyFacts;
    int turnCount = 0; // The public round counter (TurnLengthCount).
    int drawPileCount = 0;
    QString currentDying; // Empty when nobody is dying.
    // Card uses still in flight: Room tag "UseHistory" + card string, keyed by that string.
    QJsonObject useHistory;
    bool enableHegemony = false;
    bool aiHumanized = false;
    bool aiChatEnabled = false;
    // Roles still alive, from the mode's composition minus the visible dead. Unknown
    // when the mode has custom roles or a dead player's role is hidden from the viewer.
    QJsonObject roleCounts;
    bool roleCountsKnown = false;
    AIPlayerView self;
    QList<AIPlayerView> players;
    QList<AICardView> handCards;
    // Public zone: every id in it is known to everyone.
    QList<AICardView> discardPile;
    // Preserve roster order independently of the viewer-first value projection.
    QStringList playerOrder;
    QStringList alivePlayerOrder;
    QString currentPlayer;
    int currentPhase;
    // Board distances among the living, source player first. Computed by the authority
    // because distance depends on the source's own skills and equipment.
    QMap<QString, QMap<QString, int>> distances;
    QString distanceScope = QStringLiteral("full");
    // Recent events this viewer may know about, oldest first.
    QList<AIEventView> events;

    AIWorldView() : revision(0), currentPhase(int(Player::NotActive)) {}
};

struct AiSkillActionContext {
    SkillInstanceRef activationRef;
    SkillInstanceRef sourceRef;
    bool equipmentActivation = false;
    PhysicalEquipSource physicalEquipSource;
    bool activationQuotaAvailable;
    bool sourceQuotaAvailable;

    AiSkillActionContext()
        : activationQuotaAvailable(false), sourceQuotaAvailable(false) {}

    bool isValid() const {
        if (equipmentActivation)
            return !activationRef.ownerObjectName.isEmpty() && !activationRef.key.skillName.isEmpty()
                && activationRef.key.instanceID == 0
                && (physicalEquipSource.isValid() || sourceRef.isValid());
        return activationRef.isValid() && sourceRef.isValid();
    }
    QString getActivationOwner() const { return activationRef.ownerObjectName; }
    QString getActivationSkillName() const { return activationRef.key.skillName; }
    int getActivationInstanceId() const { return activationRef.key.instanceID; }
    QString getSourceOwner() const { return sourceRef.ownerObjectName; }
    QString getSourceSkillName() const { return sourceRef.key.skillName; }
    int getSourceInstanceID() const { return sourceRef.key.instanceID; }
    bool isActivationQuotaAvailable() const { return activationQuotaAvailable; }
    bool isSourceQuotaAvailable() const { return sourceQuotaAvailable; }
};

// A card evaluated as the viewer would hold and use it: owner rules on the card and
// its play candidate. Evaluation only; the candidate is never a ticket.
struct AIChoiceCardEvaluation {
    AICardView card;
    AICardCandidateView candidate;
};

// Value-typed candidates and limits for the non-card decisions: string choices,
// counts, cancelability and the authoritative default. No QVariant, Card * or
// ServerPlayer * crosses in here.
struct AIChoiceOptions {
    QString reason;
    // Which question is being asked, when one decision kind serves several of them.
    QString question;
    QStringList choices;
    // Candidate cards and players of a selection. Only ids and object names cross.
    QList<int> cardIds;
    // Only cards this question explicitly reveals. Never fill this from an opponent's
    // hidden hand merely because its count is known.
    QList<AICardView> cards;
    bool candidatesComplete = false;
    // AmazingGrace: each offered card as the viewer would take it (in-hand limits),
    // and the printed card of each offered name, for choices that name a card to use.
    QList<AIChoiceCardEvaluation> acquiredCards;
    QList<AIChoiceCardEvaluation> printedCards;
    QJsonObject context;
    QStringList playerNames;
    QString defaultChoice;
    bool hasDefaultChoice;
    bool optional;
    int minCount;
    int maxCount;

    AIChoiceOptions()
        : hasDefaultChoice(false), optional(false), minCount(1), maxCount(1) {}
};

struct AIRequest {
    enum DecisionKind {
        Activate, UseCard, SkillInvoke, Choice, Suit, Kingdom, General,
        Discard, AmazingGrace, CardChosen, Yiji, PlayerChosen, PlayersChosen,
        RespondCard, Guanxing, TriggerOrder
    };

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
    // Every skill instance this player could activate for this request. The single
    // skillActionContext above stays the one the request was built for, if any.
    QList<AiSkillActionContext> skillActions;
    bool equipmentActionsComplete = true;
    AIChoiceOptions choiceOptions;
    // Only the card decisions carry candidates; the other kinds leave this empty.
    QList<AICardCandidateView> cardCandidates;
    // Conversions this request authorizes, built by the authority from the skills the
    // player actually holds.
    QList<AICardConversionView> cardConversions;
    // Whether that list is the whole story. False means the authority could not
    // enumerate every conversion available here, which is a different statement from
    // "no conversion is available" - the planner must answer unsupported rather than
    // read an empty or partial list as an absence.
    bool conversionsEnumerated;
    // Stronger than conversionsEnumerated: skill actions have concrete zero/one-card
    // tickets with complete targets, no declarations, and no omitted skill proxies.
    bool stagedActionsComplete = false;
    // Response and use questions only: the classes of the viewer's own cards that
    // match the pattern, in SmartAI's askForCard (or askForUseCard fallback)
    // discovery order, before any limit filter. False completeness means a card in
    // that scan could not be resolved, or a use question that never reaches the scan.
    QStringList physicalClassOrder;
    bool physicalClassOrderComplete = false;
    // SmartAI's following catalogue pass: each engine card class whose printed
    // prototype matches the pattern, as {class_name, card_name}. Built only when the
    // request issued a conversion, the only kind of answer it can add.
    QJsonArray catalogClassOrder;
    bool catalogClassOrderComplete = false;

    AIRequest()
        : kind(UseCard), decisionId(0), stateRevision(0),
          reason(CardUseStruct::CARD_USE_REASON_UNKNOWN), handlingMethod(Card::MethodUse),
          hasSkillActionContext(false), conversionsEnumerated(false) {}

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

// A value description of a card the AI wants built: the name of an engine card, the
// suit and number to clone it with, the view-as skill behind it and the cards paid for
// it. The AI never constructs a Card; the authority does, after checking ownership.
struct AICardSpec {
    QString name;
    int suit;
    int number;
    QString skillName;
    QList<int> subcardIds;
    // The ticket. Everything above describes what the AI believes it is asking for;
    // this is the only field that decides whether it may have it, and the authority
    // compares the rest against its own record of that conversion.
    int conversionId;

    AICardSpec() : suit(int(Card::SuitToBeDecided)), number(0), conversionId(-1) {}
    bool isValid() const { return !name.isEmpty(); }
};

struct CardActionSpec {
    QString legacyCardString;
    // The candidate ticket this answer claims, or -1 when it claims none. An answer
    // that does name one must name a candidate this request actually offered.
    int candidateId;
    // One concrete card of the player, named by id. -1 when the answer names the card
    // some other way (a legacy string, a card spec or a skill action).
    int useCardId;
    QList<int> selectedCardIds;
    // The second ordered group of a two-pile answer: the guanxing bottom.
    QList<int> bottomCardIds;
    QStringList selectedTargetNames;
    QString userString;
    bool hasCardSpec;
    AICardSpec cardSpec;
    bool hasSkillActionContext;
    AiSkillActionContext skillActionContext;

    CardActionSpec()
        : candidateId(-1), useCardId(-1), hasCardSpec(false),
          hasSkillActionContext(false) {}
};

struct AIResult {
    // Answer carries the value-typed reply of every non-card decision kind.
    enum ActionKind { Pass, UseCard, Answer };

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
public:
    AI(ServerPlayer *player);

    enum Relation
    {
        Friend, Enemy, Neutrality
    };
    Q_ENUM(Relation)
    static Relation GetRelation3v3(const ServerPlayer *a, const ServerPlayer *b);
    static Relation GetRelationHegemony(const ServerPlayer *a, const ServerPlayer *b);
    static Relation GetRelation(const ServerPlayer *a, const ServerPlayer *b);
    Relation relationTo(const ServerPlayer *other) const;
    bool isFriend(const ServerPlayer *other) const;
    bool isEnemy(const ServerPlayer *other) const;

    QList<ServerPlayer *> getEnemies() const;
    QList<ServerPlayer *> getFriends() const;

    virtual AIResult decide(const AIRequest &request);
    virtual void activate(CardUseStruct &card_use);
    virtual Card::Suit askForSuit(const QString &reason);
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
    virtual void filterEvent(TriggerEvent triggerEvent, ServerPlayer *player, const QVariant &data);

    virtual bool useCard(const Card *card);

protected:
    Room *room;
    ServerPlayer *self;

private:
    ResponseSkill *response_skill;
};

class LuaAI : public AI
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
