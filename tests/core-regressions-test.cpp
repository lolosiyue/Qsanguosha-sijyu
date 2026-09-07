#include "card.h"
#include "ai.h"
#include "engine-bootstrap.h"
#include "general.h"
#include "lua-wrapper.h"
#include "package.h"
#include "player.h"
#include "room.h"
#include "runtime/client-target-evaluator.h"
#include "skill.h"
#include "standard.h"
#include "structs.h"

#include <QDebug>
#include <QHash>
#include <QMetaEnum>
#include <QVariantMap>
#include <QtGlobal>

class Player;
// engine 側的 Self 放喺 QSanEngine namespace，避免同 GUI 的 `ClientPlayer *Self`
// 在 Itanium ABI（GCC／Clang）下撞名；詳見 src/client/clientplayer.h。
namespace QSanEngine {
extern Player *Self;
}
using QSanEngine::Self;
void setEngineSelf(Player *player);

namespace {

bool hasEnumKey(const QMetaObject &metaObject, const char *enumName, const char *key)
{
    const int enumIndex = metaObject.indexOfEnumerator(enumName);
    if (enumIndex < 0)
        return false;
    bool ok = false;
    metaObject.enumerator(enumIndex).keyToValue(key, &ok);
    return ok;
}

class TargetEvaluatorPlayer final : public Player
{
public:
    TargetEvaluatorPlayer(QObject *parent, const QString &name)
        : Player(parent)
    {
        setObjectName(name);
    }

    int aliveCount(bool includeRemoved = false) const override
    {
        Q_UNUSED(includeRemoved);
        return 3;
    }

    QString getGameMode() const override
    {
        return QStringLiteral("03p");
    }

    // The synthetic card only reads objectName(); it never traverses seats.
    Player *getNextAlive(int = 1) const override { return nullptr; }
    Player *getLastAlive(int = 1) const override { return nullptr; }
};

class MultiVoteTargetCard final : public Card
{
public:
    explicit MultiVoteTargetCard(bool fixed = false)
        : Card(Card::NoSuit, 0, fixed)
    {
        setObjectName(QStringLiteral("client_rules_target_test"));
    }

    QString getType() const override
    {
        return QStringLiteral("skill");
    }

    QString getSubtype() const override
    {
        return QString();
    }

    CardType getTypeId() const override
    {
        return TypeSkill;
    }

    bool targetsFeasible(const QList<const Player *> &targets,
                         const Player *self) const override
    {
        Q_UNUSED(self);
        return targets.size() == 2;
    }

    bool targetFilter(const QList<const Player *> &targets,
                      const Player *toSelect, const Player *self,
                      int &maxVotes) const override
    {
        Q_UNUSED(targets);
        Q_UNUSED(self);
        maxVotes = toSelect != nullptr
            && toSelect->objectName() == QLatin1String("sgs2") ? 2 : 1;
        // Deliberately false: Collateral-style cards communicate legality
        // through maxVotes, which is what the desktop target UI consumes.
        return false;
    }
};

}

// The Judge phase moves a delayed trick to the table with S_MASK_BASIC_REASON
// as the reason, a sentinel whose low nibble sits outside the 0x00-0x0A basic
// range so that no "on discard" / "on use" trigger fires for it. Both
// whitelists -- this one in structs.cpp and the wire one in
// protocol-payload-registry.cpp -- have to accept it, or the move never
// survives a round trip.
int runCardMoveReasonTests()
{
    const CardMoveReason sentinel(CardMoveReason::S_MASK_BASIC_REASON,
                                  QStringLiteral("sgs1"), QString(),
                                  QStringLiteral("delayed_effect"));
    if ((sentinel.m_reason & CardMoveReason::S_MASK_BASIC_REASON)
        <= CardMoveReason::S_REASON_PUT) {
        qCritical() << "S_MASK_BASIC_REASON collides with a basic reason"
                    << sentinel.m_reason;
        return 1;
    }

    CardMoveReason parsed;
    if (!parsed.tryParse(sentinel.toVariant())) {
        qCritical() << "CardMoveReason::tryParse rejected S_MASK_BASIC_REASON";
        return 2;
    }
    if (parsed.m_reason != sentinel.m_reason
        || parsed.m_eventName != sentinel.m_eventName) {
        qCritical() << "CardMoveReason round trip lost data" << parsed.m_reason
                    << parsed.m_eventName;
        return 3;
    }

    CardMoveReason bogus;
    QVariantMap bogusObject = sentinel.toVariant().toMap();
    bogusObject.insert(QStringLiteral("reason"), 0x7F);
    if (bogus.tryParse(bogusObject)) {
        qCritical() << "CardMoveReason::tryParse accepted an off-list reason";
        return 4;
    }

    qInfo() << "CardMoveReason S_MASK_BASIC_REASON regression passed";
    return 0;
}

int runClientTargetEvaluatorTests()
{
    QObject owner;
    TargetEvaluatorPlayer self(&owner, QStringLiteral("sgs1"));
    TargetEvaluatorPlayer second(&owner, QStringLiteral("sgs2"));
    TargetEvaluatorPlayer third(&owner, QStringLiteral("sgs3"));

    QHash<QString, const Player *> players{
        {self.objectName(), &self},
        {second.objectName(), &second},
        {third.objectName(), &third}
    };
    const ClientRules::PlayerLookup lookup
        = [&players](const QString &name) { return players.value(name, nullptr); };
    const QStringList pool{QStringLiteral("sgs2"), QStringLiteral("sgs3")};

    MultiVoteTargetCard card;
    const ClientRules::TargetStep first
        = ClientRules::targetStep(&card, {}, pool, lookup, &self);
    if (!first.known || first.fixed || first.feasible
        || first.maxVotes.value(QStringLiteral("sgs2")) != 2
        || first.maxVotes.value(QStringLiteral("sgs3")) != 1) {
        qCritical() << "client target evaluator lost initial maxVotes semantics";
        return 1;
    }

    const ClientRules::TargetStep repeated
        = ClientRules::targetStep(&card, {QStringLiteral("sgs2")}, pool, lookup, &self);
    if (!repeated.candidates.contains(QStringLiteral("sgs2"))
        || repeated.maxVotes.value(QStringLiteral("sgs2")) != 2) {
        qCritical() << "client target evaluator rejected a legal repeated vote";
        return 2;
    }

    const ClientRules::TargetValidation valid
        = ClientRules::validateTargets(
            &card,
            {QStringLiteral("sgs2"), QStringLiteral("sgs2")},
            lookup, &self);
    if (!valid.known || !valid.valid || valid.incomplete) {
        qCritical() << "client target evaluator rejected a legal two-vote target";
        return 3;
    }

    const ClientRules::TargetValidation overLimit
        = ClientRules::validateTargets(
            &card,
            {QStringLiteral("sgs3"), QStringLiteral("sgs3")},
            lookup, &self);
    if (!overLimit.known || overLimit.valid || overLimit.incomplete
        || overLimit.reason != ClientRules::TargetValidationReason::VoteLimitExceeded
        || overLimit.targetName != QLatin1String("sgs3")) {
        qCritical() << "client target evaluator did not report a vote overflow";
        return 4;
    }

    const ClientRules::TargetValidation incomplete
        = ClientRules::validateTargets(
            &card, {QStringLiteral("sgs2")}, lookup, &self);
    if (!incomplete.known || incomplete.valid || !incomplete.incomplete
        || incomplete.reason != ClientRules::TargetValidationReason::TargetCount) {
        qCritical() << "client target evaluator lost incomplete target state";
        return 5;
    }

    const ClientRules::TargetValidation unknown
        = ClientRules::validateTargets(
            &card, {QStringLiteral("missing")}, lookup, &self);
    if (unknown.known) {
        qCritical() << "client target evaluator guessed from missing player state";
        return 6;
    }

    MultiVoteTargetCard fixed(true);
    const ClientRules::TargetStep fixedStep
        = ClientRules::targetStep(&fixed, {}, pool, lookup, &self);
    const ClientRules::TargetValidation fixedValidation
        = ClientRules::validateTargets(
            &fixed, {QStringLiteral("missing")}, lookup, &self);
    if (!fixedStep.known || !fixedStep.fixed || !fixedStep.feasible
        || !fixedValidation.known || !fixedValidation.valid) {
        qCritical() << "client target evaluator broke target-fixed semantics";
        return 7;
    }

    qInfo() << "client target evaluator regression passed";
    return 0;
}

int runEnumReflectionTests()
{
    const bool reflected =
        hasEnumKey(Card::staticMetaObject, "Suit", "Spade")
        && hasEnumKey(Card::staticMetaObject, "CardType", "TypeBasic")
        && hasEnumKey(Card::staticMetaObject, "HandlingMethod", "MethodUse")
        && hasEnumKey(General::staticMetaObject, "Gender", "Male")
        && hasEnumKey(Player::staticMetaObject, "Phase", "Play")
        && hasEnumKey(Player::staticMetaObject, "Place", "PlaceHand")
        && hasEnumKey(Player::staticMetaObject, "Role", "Lord")
        && hasEnumKey(Skill::staticMetaObject, "Frequency", "Compulsory")
        && hasEnumKey(Skill::staticMetaObject, "LimitScope", "Limit_Game")
        && hasEnumKey(TargetModSkill::staticMetaObject, "ModType", "ExtraTarget")
        && hasEnumKey(AI::staticMetaObject, "Relation", "Friend")
        && hasEnumKey(Room::staticMetaObject, "GuanxingType", "GuanxingBothSides")
        && hasEnumKey(Package::staticMetaObject, "Type", "GeneralPack")
        && hasEnumKey(EquipCard::staticMetaObject, "Location", "WeaponLocation");
    if (!reflected) {
        qCritical() << "Qt enum reflection metadata is incomplete";
        return 1;
    }
    return 0;
}

int runEngineSelfBridgeTests()
{
    Player *expected = reinterpret_cast<Player *>(quintptr(1));
    setEngineSelf(expected);
    if (Self != expected) {
        qCritical() << "engine Self did not follow the GUI-selected player";
        return 1;
    }

    setEngineSelf(nullptr);
    if (Self != nullptr) {
        qCritical() << "engine Self did not clear with the GUI-selected player";
        return 2;
    }

    qInfo() << "engine Self bridge Axe regression passed";
    return 0;
}

int runCardParseTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    const Card *card = Card::Parse(QStringLiteral("@KurouCard=."));
    if (card == nullptr) {
        qCritical() << "Card::Parse(@KurouCard=.) returned nullptr";
        return 2;
    }

    // cloneSkillCard 以 metaObject className 識別卡牌，未必寫入 objectName
    if (card->getClassName() != QStringLiteral("KurouCard")) {
        qCritical() << "Card::Parse(@KurouCard=.) className" << card->getClassName();
        return 3;
    }

    qInfo() << "Card::Parse(@KurouCard=.) regression passed";

    // 無效 LuaSkillCard 字串不得對 nullptr 呼叫 deleteLater（宿敵自動用牌 client 閃退）
    if (Card::Parse(QStringLiteral("##notARealLuaSkillCard[no_suit:0]:.:")) != nullptr) {
        qCritical() << "Card::Parse(unknown ##LuaSkillCard) should return nullptr";
        return 4;
    }

    LuaSkillCard proto(QStringLiteral("#establishOECard"), QStringLiteral("establishOECard"));
    const QString encoded = proto.toString();
    if (encoded.startsWith(QStringLiteral("##"))) {
        qCritical() << "LuaSkillCard::toString double hash" << encoded;
        return 5;
    }
    const Card *oeCard = Card::Parse(encoded);
    if (oeCard == nullptr) {
        qCritical() << "Card::Parse(#establishOECard) failed" << encoded;
        return 6;
    }
    if (oeCard->objectName() != QStringLiteral("#establishOECard")) {
        qCritical() << "Card::Parse(#establishOECard) objectName" << oeCard->objectName();
        return 7;
    }

    const Card *legacyDoubleHash = Card::Parse(QStringLiteral("##establishOECard[no_suit:0]:.:"));
    if (legacyDoubleHash == nullptr) {
        qCritical() << "Card::Parse(##establishOECard legacy) failed";
        return 8;
    }

    qInfo() << "Card::Parse LuaSkillCard #objectName regression passed";
    return 0;
}
