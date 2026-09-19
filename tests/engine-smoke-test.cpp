#include "engine-bootstrap.h"
#include "engine.h"
#include "aux-skills.h"
#include "basicai.h"
#include "card.h"
#include "game-rng.h"
#include "game-snapshot.h"
#include "general.h"
#include "json.h"
#include "lua-runtime.h"
#include "player.h"
#include "player-ui-state-builder.h"
#include "protocol.h"
#include "protocol/protocol-runtime.h"
#include "record-buffer.h"
#include "replay/replay-codec.h"
#include "room.h"
#include "room-runtime.h"
#include "roomthread.h"
#include "server-info.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill.h"
#include "skill-instance-utils.h"
#include "skill-registry.h"

#include <QDebug>
#include <QJsonDocument>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QTemporaryDir>

class TestPlayer : public Player
{
public:
    TestPlayer()
        : Player(nullptr)
    {
    }

    int aliveCount(bool = false) const override { return 1; }
    QString getGameMode() const override { return QStringLiteral("test"); }
    Player *getNextAlive(int = 1) const override { return const_cast<TestPlayer *>(this); }
    Player *getLastAlive(int = 1) const override { return const_cast<TestPlayer *>(this); }
    void cacheSkillValidity(const QString &name, bool valid) { m_skillValidityCache[name] = valid; }
    void retainInnateDescription(const QString &name) { skills << name; }
};

class TooltipTestSkill : public Skill
{
public:
    explicit TooltipTestSkill(const QString &name) : Skill(name) {}
    void setRelated(const QString &name) { waked_skills = name; }
};

static bool skillDescriptionAuthorityTests();

int runSkillDescriptionTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << error;
        return 1;
    }
    TooltipTestSkill skill("test-tooltip-instance"), related("test-tooltip-related");
    skill.setRelated(related.objectName());
    Sanguosha->addSkills(QList<const Skill *>() << &skill << &related);
    Sanguosha->addTranslationEntry(":" + skill.objectName(), "[NoAutoRep]Original body");
    Sanguosha->addTranslationEntry(":" + skill.objectName() + "x", "[NoAutoRep]Suffix body");
    Sanguosha->addTranslationEntry("^" + skill.objectName(), "Oracle sentinel");
    Sanguosha->addTranslationEntry(":" + related.objectName(), "[NoAutoRep]Related body");

    TestPlayer owner, observer;
    owner.setObjectName("tooltip-owner");
    // Even an object with the same name must not gain access to another replica's state.
    observer.setObjectName(owner.objectName());
    const QString name = skill.objectName();
    const QByteArray globalKey = ("changeTranslation" + name).toUtf8();
    const QByteArray firstKey = ("changeTranslation" + name + "#1").toUtf8();
    SkillInstance first;
    first.skillName = name;
    first.instanceID = 1;
    first.source = SourceInnate;
    first.bindHead = 1;
    owner.upsertSkillInstance(first);
    SkillInstance second = first;
    second.instanceID = 2;
    second.source = SourceAttached;
    second.bindHead = 2;
    second.parentRef = SkillInstanceRef("source-owner", SkillInstanceKey(name, 1));
    owner.upsertSkillInstance(second);
    const auto check = [](bool ok, const char *message) {
        if (!ok) qCritical() << "skill-description:" << message;
        return ok;
    };
    if (!check(skill.getDescription(&owner, 1) == "Original body", "original fallback")) return 2;
    // Empty summaries and passive skills do not add placeholder rows; dual-general
    // labels depend on the player's actual second general, not an instance binding.
    QString compact = owner.getSkillDescription(&owner);
    if (!check(!compact.contains(Player::tr("Usage: %1").arg(""))
        && !compact.contains(Player::tr("Skill state: %1: %2").arg("", ""))
        && !compact.contains(Player::tr(" / Head general"))
        && !compact.contains(Player::tr(" / Deputy general")), "empty single-general tooltip")) return 2;
    owner.setSkillDescriptionState({{name + "#1", QVariantMap{{"scope", "none"}}}}, {}, {});
    compact = owner.getSkillDescription(&owner);
    if (!check(!compact.contains("usage:") && !compact.contains(Player::tr("Usage: %1").arg("")),
               "None usage omitted from both summary and technical details")) return 2;
    owner.setGeneral2Name("caocao");
    compact = owner.getSkillDescription(&owner);
    if (!check(compact.contains(Player::tr(" / Head general"))
        && compact.contains(Player::tr(" / Deputy general")), "dual-general labels retained")) return 2;
    owner.setGeneral2Name(QString());

    owner.setProperty(globalKey.constData(), "[NoAutoRep]Shared body {draw} {target}");
    owner.setSkillDescriptionSwap(name, "{draw}", "two");
    owner.setSkillDescriptionSwap(name, "{target}", "everyone");
    owner.setSkillDescriptionSwap(name, "{draw}", "three", 1);
    if (!check(skill.getDescription(&owner, 1) == "Shared body three everyone"
               && skill.getDescription(&owner, 2) == "Shared body two everyone"
               && skill.getDescription(&owner) == "Shared body two everyone",
               "global swaps inherited, instance key wins before substitution")) return 3;
    owner.setProperty(firstKey.constData(), "x");
    if (!check(skill.getDescription(&owner, 1) == "Suffix body"
               && skill.getDescription(&owner, 2).startsWith("Shared body"), "instance suffix override")) return 4;
    owner.setProperty(firstKey.constData(), "[NoAutoRep]Different body");
    QString tooltip = owner.getSkillDescription(&owner);
    if (!check(tooltip.contains("Different body") && tooltip.count("Shared body") == 1,
               "distinct instance bodies")) return 5;
    owner.setProperty(firstKey.constData(), QVariant());
    owner.setProperty(globalKey.constData(), "[NoAutoRep]Shared body");
    owner.setSkillInstanceStateValue(name, 1, "secret", "<private-one>");
    owner.setSkillInstanceStateValue(name, 2, "targets", QStringList{"private-two"});
    owner.setSkillInstanceCorrectStateValue(name, 2, "offset", -2);
    owner.setSkillInstanceAmountOverride(name, 2, 3);
    tooltip = owner.getSkillDescription(&owner);
    const int firstPosition = tooltip.indexOf("<b>#1</b>");
    const int secondPosition = tooltip.indexOf("<b>#2</b>");
    if (!check(tooltip.count("Shared body") == 1 && tooltip.count("Oracle sentinel") == 1
               && firstPosition >= 0 && secondPosition > firstPosition
               && tooltip.indexOf("&lt;private-one&gt;") > firstPosition
               && tooltip.indexOf("&lt;private-one&gt;") < secondPosition
               && tooltip.indexOf("private-two") > secondPosition
               && !tooltip.contains("<private-one>") && tooltip.contains("amountOverride: 3")
               && tooltip.contains("source-owner") && tooltip.contains("correctState"),
               "grouped body, isolated details, escaped raw data and absolute override")) return 6;
    if (!check(!owner.getSkillDescription().contains("private-")
               && !owner.getSkillDescription(&observer).contains("private-")
               && owner.getSkillDescription(&observer).contains("correctState"),
               "private state requires exact viewer; public corrections remain visible")) return 7;
    observer.upsertSkillInstance(first);
    observer.setSkillInstanceStateValue(name, 1, "secret", "other-holder-secret");
    if (!check(observer.getSkillDescription(&observer).contains("other-holder-secret")
               && !owner.getSkillDescription(&owner).contains("other-holder-secret"),
               "same skill and instance ID on different holders stay isolated")) return 7;
    const QString relatedSection = tooltip.mid(tooltip.indexOf("#01A5AF"));
    if (!check(relatedSection.contains("Related body") && !relatedSection.contains("instanceID:"),
               "related text is not ownership")) return 8;

    int changes = 0;
    QObject::connect(&owner, &Player::skill_state_changed, &owner, [&changes]() { ++changes; });
    owner.removeSkillInstanceState(name, 1);
    owner.resetSkillInstanceAmountOverride(name, 2);
    owner.clearSkillInstanceCorrectState(name, 2);
    owner.setSkillDescriptionSwap(name, "unused", "updated", 1);
    owner.setProperty(firstKey.constData(), "[NoAutoRep]Transient body");
    if (!check(changes == 5 && !owner.getSkillDescription(&owner).contains("private-one")
               && !owner.getSkillDescription(&owner).contains("amountOverride"), "reset and refresh signals")) return 9;
    owner.removeSkillInstance(name, 1);
    if (!check(!owner.getSkillDescription(&owner).contains("<b>#1</b>")
               && owner.getSkillDescriptionSwap(name, 1).isEmpty()
               && !owner.property(firstKey.constData()).isValid(), "removed instance has no residual text")) return 10;
    owner.cacheSkillValidity(name, false);
    if (!check(owner.getSkillDescription().contains("<font color=\"#bab8ba\">Shared body</font>"),
               "cached invalidity retained without evaluating rules")) return 11;
    // Snapshot reconstruction may follow property synchronization: preserve its text,
    // while private instance state must come only from the replacement snapshot.
    owner.setProperty(("changeTranslation" + name + "#2").toUtf8().constData(), "[NoAutoRep]Current snapshot");
    owner.clearSkillInstances();
    if (!check(!owner.getSkillDescription(&owner).contains("instanceID:"), "empty snapshot has no instances")) return 12;
    owner.upsertSkillInstance(second);
    tooltip = owner.getSkillDescription(&owner);
    if (!check(tooltip.contains("Current snapshot") && !tooltip.contains("private-two"),
               "snapshot replaces private state while retaining separately synced text")) return 12;
    second.visible = false;
    owner.upsertSkillInstance(second);
    if (!check(!owner.getSkillDescription(&owner).contains("Current snapshot"), "hidden instance is omitted")) return 13;
    owner.clearSkillInstances();
    owner.retainInnateDescription(name);
    owner.setAlive(false);
    tooltip = owner.getSkillDescription(&owner);
    if (!check(tooltip.contains("Shared body") && tooltip.contains("Oracle sentinel")
               && !tooltip.contains("instanceID:"), "death fallback remains descriptive, not ownership")) return 14;
    if (!skillDescriptionAuthorityTests()) return 15;
    qInfo() << "skill-description passed";
    return 0;
}

struct RoomTestAccess
{
    static void prepareDescriptionRules(Room &room, ServerPlayer *current)
    {
        room.thread = new RoomThread(&room);
        room.thread->setParent(&room);
        room.current = current;
        // Rule events require an AI event receiver even in a non-playing fixture.
        // No AI decision/gameplay is exercised by this focused test.
        for (ServerPlayer *player : room.getAllPlayers(true)) {
            auto *events = new BasicAI(player);
            events->setParent(player);
            player->setAI(events);
        }
    }
    static bool reserveUsage(Room &room, const ViewAsSkillV2 *skill, const SkillContext &context)
    { return room.reserveActiveSkillUsage(skill, context); }
    static void releaseUsage(Room &room, const ViewAsSkillV2 *skill, const SkillContext &context)
    { room.releaseActiveSkillUsage(skill, context); }
    static void commitUsage(Room &room, const ViewAsSkillV2 *skill, const SkillContext &context)
    { room.commitActiveSkillUsage(skill, context); }
    static ServerPlayer *addPlayer(Room &room, const QString &objectName)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        room.addPlayerToRoster(player);
        return player;
    }

    static bool resolveCardSkillInstance(Room &room, CardUseStruct &use)
    {
        return room.resolveCardSkillInstance(use);
    }

    static void assignRoles(Room &room)
    {
        room.assignRoles();
    }

    static void startVirtualGame(Room &room)
    {
        room._virtual = true;
        room.startGame();
    }
};

class TooltipQuotaSkill : public ViewAsSkillV2
{
public:
    TooltipQuotaSkill() : ViewAsSkillV2("test-tooltip-quota") { m_baseAmount = 2; }
    LimitScope scope = Limit_Turn;
    SkillInstanceRef sharedRef;
    mutable int ruleQueries = 0;
    LimitScope getLimitScope() const override { return scope; }
    SkillInstanceRef getUsageRef(const SkillContext &context) const override
    { ++ruleQueries; return sharedRef.isValid() ? sharedRef : context.activationRef; }
    int getMaxUsageLimit(const SkillContext &context) const override
    { ++ruleQueries; return context.invoker->getMark("test-tooltip-cap"); }
};

static bool skillDescriptionAuthorityTests()
{
    const auto check = [](bool ok, const char *message) {
        if (!ok) qCritical() << "skill-description authority:" << message;
        return ok;
    };
    TooltipQuotaSkill skill;
    TooltipTestSkill root("test-tooltip-quota-root");
    Sanguosha->addSkills(QList<const Skill *>() << &skill << &root);
    Sanguosha->addTranslationEntry(":" + skill.objectName(), "[NoAutoRep]Quota body");
    Sanguosha->addTranslationEntry("@" + skill.objectName() + ".state.targets", "記錄目標");
    Sanguosha->addTranslationEntry("@" + skill.objectName() + ".state.targets.type", "players");
    Sanguosha->addTranslationEntry("@" + skill.objectName() + ".amount", "摸牌張數");
    Room room(nullptr, "02_1v1");
    ServerPlayer *owner = RoomTestAccess::addPlayer(room, "quota-owner");
    ServerPlayer *holder = RoomTestAccess::addPlayer(room, "quota-holder");
    RoomTestAccess::prepareDescriptionRules(room, owner);
    owner->setMark("test-tooltip-cap", 2);
    SkillInstance instance;
    instance.skillName = skill.objectName();
    instance.instanceID = 1;
    owner->upsertSkillInstance(instance);
    SkillInstance second = instance;
    second.instanceID = 2;
    owner->upsertSkillInstance(second);
    SkillInstance rootInstance = instance;
    rootInstance.skillName = root.objectName();
    holder->upsertSkillInstance(rootInstance);
    skill.sharedRef = SkillInstanceRef(holder->objectName(), rootInstance.key());
    SkillContext context;
    context.owner = context.invoker = context.initiator = owner;
    context.skill_name = skill.objectName();
    context.instanceID = 1;
    context.activationRef = SkillInstanceRef(owner->objectName(), instance.key());
    context.sourceRef = skill.sharedRef;
    const QString key = SkillInstanceUtils::formatName(instance.skillName, 1);
    const QString key2 = SkillInstanceUtils::formatName(instance.skillName, 2);
    QVariantMap usage = room.describeSkillUsage(owner, instance);
    const QString mark = usage.value("mark").toString();
    holder->setMark(mark, 1);
    if (!check(RoomTestAccess::reserveUsage(room, &skill, context), "reserve actual shared quota")) return false;
    PlayerUIState state;
    PlayerUIStateBuilder::buildSkillDescriptions(state, *owner, room);
    usage = state.skillUsage.value(key).toMap();
    if (!check(usage.value("holder") == holder->objectName() && usage.value("used").toInt() == 1
        && usage.value("limit").toInt() == 2 && usage.value("reserved").toInt() == 1
        && usage.value("shared").toBool()
        && usage.value("counter") == state.skillUsage.value(key2).toMap().value("counter"),
        "cross-player usage holder, shared identity and reservation")) return false;
    if (!check(!RoomTestAccess::reserveUsage(room, &skill, context), "reservation consumes remaining availability")) return false;
    RoomTestAccess::releaseUsage(room, &skill, context);
    if (!check(room.describeSkillUsage(owner, instance).value("reserved").toInt() == 0
        && holder->getMark(mark) == 1, "cancel releases without committing")) return false;
    if (!RoomTestAccess::reserveUsage(room, &skill, context)) return false;
    RoomTestAccess::commitUsage(room, &skill, context);
    if (!check(holder->getMark(mark) == 2 && room.describeSkillUsage(owner, instance).value("reserved").toInt() == 0,
               "commit transfers reservation to the authoritative mark")) return false;
    skill.resetUsage(context);
    owner->setMark("test-tooltip-cap", 3);
    usage = room.describeSkillUsage(owner, instance);
    if (!check(usage.value("used").toInt() == 0 && usage.value("limit").toInt() == 3,
               "reset and dynamic limit use current rules")) return false;
    for (Skill::LimitScope scope : {Skill::Limit_Round, Skill::Limit_Turn, Skill::Limit_Phase, Skill::Limit_Game}) {
        skill.scope = scope;
        skill.setPhaseName(scope == Skill::Limit_Phase ? "play" : "");
        usage = room.describeSkillUsage(owner, instance);
        const QString counterMark = usage.value("mark").toString();
        holder->setMark(counterMark, 2);
        skill.resetUsage(context);
        if (!check(room.describeSkillUsage(owner, instance).value("used").toInt() == 0,
                   "scope-specific reset matches the rule mark")) return false;
    }
    skill.scope = Skill::Limit_Custom;
    usage = room.describeSkillUsage(owner, instance);
    if (!check(!usage.contains("used") && !usage.contains("limit"), "custom usage is not a fabricated generic quota")) return false;
    skill.setProperty("DescriptionUsageMark", "custom-counter");
    skill.setProperty("DescriptionUsageScope", "自訂週期");
    skill.setProperty("DescriptionUsageLimit", 4);
    owner->setMark("custom-counter", 2);
    if (!check(room.describeSkillUsage(owner, instance).value("used").toInt() == 2,
               "explicit custom adapter reads the declared mark")) return false;
    skill.setProperty("DescriptionUsageMark", QVariant());
    skill.scope = Skill::Limit_None;
    if (!check(room.describeSkillUsage(owner, instance).value("scope") == "none", "no generic limit is explicit")) return false;
    skill.scope = Skill::Limit_Turn;
    if (!check(room.setSkillInstanceAmount(holder, context.activationRef, 3, "test-source"), "amount change")) return false;
    const QVariantMap provenance = owner->getSkillInstanceStateValue(instance.skillName, 1, "__description_amount").toMap();
    if (!check(provenance.value("source_player") == holder->objectName() && provenance.value("value").toInt() == 3,
               "amount source is recorded at mutation, not guessed")) return false;
    owner->setSkillInstanceStateValue(instance.skillName, 1, "targets", QStringList{"<private-target>"});
    owner->setTag("SkillInvalidityRecords", QStringList{key + "|quota-holder|recorded-reason"});
    owner->setCardLimitation("use", "BasicCard", "test-source", true);
    owner->setMark("description-effect-Clear", 1);
    if (!check(room.setSkillEffectDescription(owner, "test-effect", "<effect>", skill.sharedRef,
               "至本回合結束", "description-effect-Clear"), "declared external effect")) return false;
    PlayerUIStateBuilder::buildSkillDescriptions(state, *owner, room);
    if (!check(!state.skillValidity.value(key).toBool() && state.skillValidity.value(key2).toBool()
        && state.skillEffects.size() == 3, "per-instance invalidity and separate effects")) return false;

    // Exercise the actual JSON wire representation and backward-compatible replacement.
    PlayerUIState parsed;
    const QVariant wire = QJsonDocument::fromJson(QJsonDocument::fromVariant(state.toVariant()).toJson()).toVariant();
    if (!check(parsed.tryParse(wire) && parsed == state, "UI state wire round trip")) return false;
    const PlayerUIState publicState = state.forObserver();
    if (!check(publicState.skillUsage.isEmpty() && publicState.skillEffects.isEmpty()
        && publicState.skillValidity == state.skillValidity, "observer payload redacts private data")) return false;
    PlayerUIState withPublicEffect = state;
    withPublicEffect.skillEffects << QVariantMap{{"kind", "declared"}, {"text", "public effect"}, {"public", true}};
    if (!check(withPublicEffect.forObserver().skillEffects.size() == 1,
               "only explicitly public effect summaries survive redaction")) return false;
    TestPlayer replica, observer;
    replica.setObjectName(owner->objectName());
    observer.setObjectName(owner->objectName());
    replica.upsertSkillInstance(*owner->findSkillInstance(instance.skillName, 1));
    replica.upsertSkillInstance(second);
    replica.setSkillInstanceState(instance.skillName, 1, owner->getSkillInstanceState(instance.skillName, 1));
    replica.setSkillDescriptionState(parsed.skillUsage, parsed.skillValidity, parsed.skillEffects);
    const int queries = skill.ruleQueries;
    QString tooltip = replica.getSkillDescription(&replica);
    if (!check(tooltip.contains(Player::tr("Skill state: %1: %2").arg("記錄目標", "&lt;private-target&gt;")) && tooltip.contains("2 → 3")
        && tooltip.contains(Player::tr("Current effects")) && tooltip.contains("&lt;effect&gt;")
        && tooltip.contains(Player::tr("%1 shared count, see %2").arg(Player::tr("This turn"), skill.objectName() + " #1")) && skill.ruleQueries == queries,
        "readable state, absolute delta, shared quota and escaped effects with no UI rule evaluation")) return false;
    if (!check(!replica.getSkillDescription(&observer).contains("private-target")
        && !replica.getSkillDescription(&observer).contains(Player::tr("Current effects"))
        && !replica.getSkillDescription(&observer).contains("usage:"), "viewer pointer privacy guard")) return false;
    replica.setSkillInstanceCorrectStateValue(instance.skillName, 1, "offset", 2);
    replica.setSkillInstanceStateValue(instance.skillName, 1, "__description_correct",
        QVariantMap{{"offset", QVariantMap{{"source_player", "old-source"}, {"value", 2}}}});
    replica.clearSkillInstanceCorrectState(instance.skillName, 1);
    replica.setSkillInstanceCorrectStateValue(instance.skillName, 1, "offset", 2);
    if (!check(!replica.getSkillInstanceStateValue(instance.skillName, 1, "__description_correct").isValid(),
               "direct correction reset cannot revive stale provenance")) return false;
    owner->setTag("SkillInvalidityRecords", QStringList());
    owner->clearCardLimitation(true);
    owner->setMark("description-effect-Clear", 0);
    PlayerUIStateBuilder::buildSkillDescriptions(state, *owner, room);
    if (!check(state.skillEffects.isEmpty() && state.skillValidity.value(key).toBool(), "effects disappear with authoritative reset")) return false;
    owner->setMark("description-effect-Clear", 1);
    PlayerUIStateBuilder::buildSkillDescriptions(state, *owner, room);
    if (!check(state.skillEffects.isEmpty(), "reused mark does not resurrect retired effect provenance")) return false;
    room.removeSkillEffectDescription(owner, "test-effect");
    if (!check(owner->getTag("SkillEffectDescriptions").toMap().isEmpty(), "effect explicit removal")) return false;
    room.resetSkillInstanceAmount(holder, context.activationRef);
    if (!check(!owner->getSkillInstanceStateValue(instance.skillName, 1, "__description_amount").isValid(),
               "reset removes amount provenance")) return false;
    owner->removeSkillInstance(instance.skillName, 1);
    PlayerUIStateBuilder::buildSkillDescriptions(state, *owner, room);
    if (!check(!state.skillUsage.contains(key) && state.skillUsage.contains(key2), "lost instance removes its cache entry")) return false;
    holder->removeSkillInstance(rootInstance.skillName, 1);
    if (!check(room.describeSkillUsage(owner, second).value("unavailable").toBool(), "lost shared root fails closed")) return false;
    QVariantMap legacy = parsed.toVariant().toMap();
    legacy.remove("skillUsage"); legacy.remove("skillValidity"); legacy.remove("skillEffects");
    if (!check(parsed.tryParse(legacy) && parsed.skillUsage.isEmpty() && parsed.skillEffects.isEmpty(),
               "legacy/snapshot replacement clears stale summaries")) return false;
    owner->setAlive(false);
    PlayerUIStateBuilder::buildSkillDescriptions(state, *owner, room);
    return check(state.skillUsage.isEmpty() && state.skillEffects.isEmpty(), "death clears live summaries");
}

class TestPhysicalViewAsSkill : public OneCardViewAsSkill
{
public:
    TestPhysicalViewAsSkill()
        : OneCardViewAsSkill(QStringLiteral("test-physical-view-as"))
    {
    }

    bool viewFilter(const Card *) const override { return true; }
    const Card *viewAs(const Card *originalCard) const override { return originalCard; }
};

class TestPlayerUIStateFixedSkill : public MaxCardsSkill
{
public:
    TestPlayerUIStateFixedSkill()
        : MaxCardsSkill(QStringLiteral("test-player-ui-state-fixed"))
    {
    }

    int getFixed(const Player *) const override { return 9; }
};

class TestPlayerUIStateExtraSkill : public MaxCardsSkill
{
public:
    TestPlayerUIStateExtraSkill()
        : MaxCardsSkill(QStringLiteral("test-player-ui-state-extra"))
    {
    }

    int getExtra(const Player *) const override { return 2; }
};

class TestPlayerUIStateDistanceSkill : public DistanceSkill
{
public:
    TestPlayerUIStateDistanceSkill()
        : DistanceSkill(QStringLiteral("test-player-ui-state-distance"))
    {
    }

    int getCorrect(const Player *from, const Player *to) const override
    {
        if (from && from->objectName() == QStringLiteral("ui-state-owner"))
            return -1;
        if (to && to->objectName() == QStringLiteral("ui-state-owner"))
            return 2;
        return 0;
    }
};

class TestPlayerUIStateEquipSkill : public ViewAsEquipSkill
{
public:
    TestPlayerUIStateEquipSkill()
        : ViewAsEquipSkill(QStringLiteral("test-player-ui-state-equip"))
    {
    }

    QString viewAsEquip(const Player *) const override
    {
        return QStringLiteral("crossbow,silver_lion");
    }
};

class TestRegistryTriggerSkill : public TriggerSkill
{
public:
    TestRegistryTriggerSkill()
        : TriggerSkill(QStringLiteral("test-registry-trigger"))
    {
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return false; }
};

static bool skillRegistryPreservesLegacyBehavior()
{
    SkillRegistry registry;
    DistanceSkill distance(QStringLiteral("test-registry-shared"));
    MaxCardsSkill maxCards(QStringLiteral("test-registry-shared"));
    TestRegistryTriggerSkill trigger;

    if (registry.add(&distance)
        || registry.find(QStringLiteral("test-registry-shared#7")) != &distance
        || !registry.distanceSkills().contains(&distance))
        return false;

    if (registry.add(&trigger)
        || registry.triggerSkill(QStringLiteral("test-registry-trigger#3")) != &trigger)
        return false;

    if (!registry.add(&maxCards)
        || registry.find(QStringLiteral("test-registry-shared")) != &maxCards
        || registry.distanceSkills().contains(&distance)
        || !registry.maxCardsSkills().contains(&maxCards))
        return false;

    return registry.names().size() == 2 && registry.allSkills().size() == 2;
}

static bool engineLegacySkillApisDelegateToRegistry()
{
    DistanceSkill distance(QStringLiteral("test-engine-registry-distance"));
    MaxCardsSkill maxCards(QStringLiteral("test-engine-registry-max-cards"));
    TestRegistryTriggerSkill trigger;
    Sanguosha->addSkills(QList<const Skill *>() << &distance << &maxCards << &trigger);

    return Sanguosha->getSkill(QStringLiteral("test-engine-registry-distance#2")) == &distance
        && Sanguosha->getTriggerSkill(QStringLiteral("test-registry-trigger#3")) == &trigger
        && Sanguosha->getDistanceSkills().contains(&distance)
        && Sanguosha->getMaxCardsSkills().contains(&maxCards);
}

static bool discardSkillSelectsCardsForExplicitClientPlayerContext()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return false;
    }

    TestPlayer player;
    DummyCard card;
    DiscardSkill skill;
    skill.setNum(1);
    skill.setMinNum(1);
    skill.setIncludeEquip(false);
    skill.setIsDiscard(true);
    skill.setPattern(QStringLiteral("."));
    skill.setPlayer(&player);

    const bool selectable = skill.viewFilter(QList<const Card *>(), &card);
    if (!selectable)
        qCritical() << "DiscardSkill rejected a card for its explicit client player context";
    return selectable;
}

static int physicalResponseIgnoresStaleHelperActivation()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *player = RoomTestAccess::addPlayer(room, QStringLiteral("human"));

    DummyCard physicalCard;
    physicalCard.setId(42);
    if (physicalCard.isVirtualCard()) {
        qCritical() << "physical response fixture is not a real engine card";
        return 1;
    }

    physicalCard.setActivationSkill(QStringLiteral("response-skill"), 7);
    CardUseStruct use(&physicalCard, player);
    use.hasSkillActivationRequest = true;

    const bool accepted = RoomTestAccess::resolveCardSkillInstance(room, use);
    physicalCard.setActivationSkill(QString(), 0);
    if (!accepted)
        qCritical() << "physical response was rejected because of stale helper activation metadata";
    if (!accepted)
        return 2;
    if (use.activationRef.isValid() || use.sourceRef.isValid())
        return 3;

    TestPhysicalViewAsSkill registeredSkill;
    Sanguosha->addSkills(QList<const Skill *>() << &registeredSkill);
    const int instanceId = player->createSkillInstance(registeredSkill.objectName(), SourceAcquired, true);
    physicalCard.setActivationSkill(registeredSkill.objectName(), instanceId);
    CardUseStruct registeredUse(&physicalCard, player);
    registeredUse.hasSkillActivationRequest = true;
    const bool registeredAccepted = RoomTestAccess::resolveCardSkillInstance(room, registeredUse);
    physicalCard.setSkillInstanceId(0);
    physicalCard.setSourceSkill(QString(), 0);
    physicalCard.setActivationSkill(QString(), 0);
    if (!registeredAccepted || !registeredUse.activationRef.isValid()
        || registeredUse.activationRef.key.skillName != registeredSkill.objectName()
        || registeredUse.activationRef.key.instanceID != instanceId)
        return 4;
    return 0;
}

static bool playerUIStateBuilderAggregatesPresentationState()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *owner = RoomTestAccess::addPlayer(room, QStringLiteral("ui-state-owner"));
    ServerPlayer *sibling = RoomTestAccess::addPlayer(room, QStringLiteral("ui-state-sibling"));
    owner->setAlive(true);
    sibling->setAlive(true);

    TestPlayerUIStateFixedSkill fixedSkill;
    TestPlayerUIStateExtraSkill extraSkill;
    TestPlayerUIStateDistanceSkill distanceSkill;
    TestPlayerUIStateEquipSkill equipSkill;
    Sanguosha->addSkills(QList<const Skill *>()
                         << &fixedSkill << &extraSkill << &distanceSkill << &equipSkill);
    owner->Player::addSkill(fixedSkill.objectName());
    owner->Player::addSkill(extraSkill.objectName());
    owner->Player::addSkill(distanceSkill.objectName());
    owner->Player::addSkill(equipSkill.objectName());

    const PlayerUIState state = PlayerUIStateBuilder::build(*owner, room);
    const QString fixedEntry = QStringLiteral("test-player-ui-state-fixed^F9^ui-state-owner");
    const QString extraEntry = QStringLiteral("test-player-ui-state-extra^2^ui-state-owner");
    const QStringList equipEntries = QStringList()
        << QStringLiteral("crossbow^test-player-ui-state-equip")
        << QStringLiteral("silver_lion^test-player-ui-state-equip");

    const bool valid = state.handMax == owner->getMaxCards()
        && state.maxCardsSkills.contains(fixedEntry)
        && state.maxCardsSkills.contains(extraEntry)
        && state.offensiveDistance == -1
        && state.defensiveDistance == 2
        && state.offensiveSkills == QStringList(distanceSkill.objectName())
        && state.defensiveSkills == QStringList(distanceSkill.objectName())
        && state.viewAsEquipSkills == equipEntries;
    if (!valid)
        qCritical() << "PlayerUIStateBuilder returned unexpected presentation state"
                    << state.toVariant();
    return valid;
}

static bool gameStartupConstructsRoomThreadBeforePreparingViewAsEquipSkills()
{
    const General *wolong = Sanguosha->getGeneral(QStringLiteral("wolong"));
    const General *caocao = Sanguosha->getGeneral(QStringLiteral("caocao"));
    if (!wolong || !caocao) {
        qCritical() << "startup regression fixture generals are unavailable";
        return false;
    }

    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *first = RoomTestAccess::addPlayer(room, QStringLiteral("first"));
    ServerPlayer *second = RoomTestAccess::addPlayer(room, QStringLiteral("second"));
    first->setState(QStringLiteral("robot"));
    second->setState(QStringLiteral("robot"));
    first->setGeneral(wolong);
    second->setGeneral(caocao);

    RoomTestAccess::startVirtualGame(room);
    if (!room.getThread()) {
        qCritical() << "RoomThread was not constructed before preparing ViewAsEquipSkill mappings";
        return false;
    }
    return true;
}

static bool roomSnapshotFacadePersistsAndRetrievesSnapshot()
{
    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) {
        qCritical() << "Unable to create snapshot regression directory";
        return false;
    }

    const bool oldEnableAI = Config.EnableAI;
    const bool oldDisableLua = Config.DisableLua;
    Config.EnableAI = true;
    Config.DisableLua = false;

    Room room(nullptr, QStringLiteral("02p"));
    ServerPlayer *first = RoomTestAccess::addPlayer(room, QStringLiteral("snapshot-first"));
    ServerPlayer *second = RoomTestAccess::addPlayer(room, QStringLiteral("snapshot-second"));
    first->setState(QStringLiteral("robot"));
    second->setState(QStringLiteral("robot"));
    first->setGeneral(Sanguosha->getGeneral(QStringLiteral("caocao")));
    second->setGeneral(Sanguosha->getGeneral(QStringLiteral("guanyu")));
    first->setRole(QStringLiteral("lord"));
    second->setRole(QStringLiteral("rebel"));
    first->setSeat(1);
    first->setPlayerSeat(1);
    second->setSeat(2);
    second->setPlayerSeat(2);
    RoomTestAccess::startVirtualGame(room);
    room.setCurrent(first);
    first->setPhase(Player::NotActive);

    const QString replayPath = temporaryDir.filePath(QStringLiteral("snapshot-facade.replay.txt"));
    room.setReplayPath(replayPath);
    room.setTag(QStringLiteral("TurnLengthCount"), 7);

    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    room.saveSnapshot(QStringLiteral("turn"));

    GameSnapshot *snapshot = room.getSnapshotBySerial(1);
    const QString snapshotDir = GameSnapshot::getSnapshotDir(replayPath);
    const QString snapshotPath = snapshotDir + QStringLiteral("/")
        + GameSnapshot::generateSnapshotFilename(1, QStringLiteral("turn"));
    const bool valid = room.getReplayPath() == replayPath
        && room.getSnapshotDir() == snapshotDir
        && QFileInfo::exists(snapshotPath)
        && snapshot
        && snapshot->getTurnCount() == 7
        && snapshot->getTurnSerial() == 1
        && snapshot->getReplayPath() == replayPath
        && snapshot->getSnapshotType() == QStringLiteral("turn")
        && snapshot->getDescription() == QStringLiteral("Turn 1");
    if (!valid)
        qCritical() << "Room snapshot facade did not preserve persisted snapshot state";
    Config.EnableAI = oldEnableAI;
    Config.DisableLua = oldDisableLua;
    return valid;
}

// Four packages used to define the same global class name twice (Zhijie,
// Jiejie, Xingluan, Juanjia). Those constructors are implicitly inline, so the
// linker silently kept one definition and both generals ended up sharing it --
// taoheng got ol_cuiyuan's "zhijie" instead of its own "th_zhijie", and
// mobilebs_xinxianying got HayatePackage's "hayate_jiejie" instead of "jiejie".
// Nothing diagnoses that at build time, so pin the affected generals to their
// skills. This catches a swap only where the two classes register different
// skill IDs; Xingluan and Juanjia took their ID from the caller, so their
// collisions swapped the implementation silently and only the rename fixes them.
static bool collidingSkillNamesStayWithTheirOwnGenerals()
{
    // HayatePackage is compiled in but absent from lua/config.lua's
    // package_names, so its general only has to hold up when it is loaded.
    static const struct {
        const char *general;
        const char *skill;
        bool required;
    } expectations[] = {
        {"taoheng", "th_zhijie", true},
        {"ol_cuiyuan", "zhijie", true},
        {"hayate_hakaze", "hayate_jiejie", false},
        {"mobilebs_xinxianying", "jiejie", true},
        {"ol_fanchou", "olxingluan", true},
        {"fanchou", "xingluan", true},
        {"shendianwei", "juanjia", true},
    };

    bool ok = true;
    for (const auto &expectation : expectations) {
        const QString generalName = QString::fromLatin1(expectation.general);
        const QString skillName = QString::fromLatin1(expectation.skill);
        const General *general = Sanguosha->getGeneral(generalName);
        if (!general) {
            if (expectation.required) {
                qCritical() << "general not loaded:" << generalName;
                ok = false;
            }
            continue;
        }
        if (!general->hasSkill(skillName)) {
            QStringList actual;
            foreach (const Skill *skill, general->getSkillList())
                actual << skill->objectName();
            qCritical() << generalName << "lost" << skillName << "- it owns" << actual;
            ok = false;
        }
    }
    return ok;
}

int runLargeRoomModeTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "large-room mode initialization failed:" << error;
        return 1;
    }
    const QString modeId = QStringLiteral("50p");
    const GameModeStruct mode = Sanguosha->getGameMode(modeId);
    const QStringList roles = Sanguosha->getRoleList(modeId);
    if (!mode.isValid() || Sanguosha->getPlayerCount(modeId) != 50
        || !Sanguosha->getAvailableModes().contains(modeId)
        || !Sanguosha->getGroupModes(QStringLiteral("身份模式")).contains(modeId)
        || roles.size() != 50 || roles.count(QStringLiteral("lord")) != 1
        || roles.count(QStringLiteral("loyalist")) != 23
        || roles.count(QStringLiteral("rebel")) != 25
        || roles.count(QStringLiteral("renegade")) != 1
        || mode.reward_policy != QLatin1String("identity")
        || mode.win_policy != QLatin1String("identity")
        || !mode.lord_welfare || !mode.shuffle_seats) {
        qCritical() << "50p must be selectable with the agreed identity configuration";
        return 2;
    }

    // Exercise actual room admission/assignment without starting Lua AI or gameplay.
    // Fifty seats must all receive a role; non-lord identities remain hidden.
    const QScopedValueRollback<bool> hegemony(Config.EnableHegemony, false);
    const QScopedValueRollback<bool> serverHegemony(ServerInfo.EnableHegemony, false);
    Room room(nullptr, modeId, GameSessionConfig(), Room::RuntimeInitializationPolicy::Deferred);
    for (int seat = 0; seat < 49; ++seat)
        RoomTestAccess::addPlayer(room, QStringLiteral("large-seat-%1").arg(seat));
    if (room.isFull() || room.getLack() != 1) {
        qCritical() << "50p room filled before the fiftieth seat";
        return 3;
    }
    RoomTestAccess::addPlayer(room, QStringLiteral("large-seat-49"));
    if (!room.isFull() || room.getLack() != 0) {
        qCritical() << "50p room did not fill at fifty seats";
        return 4;
    }
    RoomTestAccess::assignRoles(room);
    QStringList assigned;
    for (const ServerPlayer *player : room.getPlayers()) {
        assigned << player->getRole();
        if (player->hasShownRole() != player->isLord()) {
            qCritical() << "50p must expose only the lord's identity";
            return 5;
        }
    }
    assigned.sort();
    QStringList expected = roles;
    expected.sort();
    if (assigned != expected) {
        qCritical() << "50p assignment lost or changed an identity";
        return 6;
    }
    qInfo() << "LARGE_ROOM_MODE_OK seats=50 lord=1 loyalist=23 rebel=25 renegade=1";
    return 0;
}

int runEngineSmokeTests()
{
    ServerInfoStruct info;
    const QString setup = QString::fromLatin1("U2VydmVy:02_1v1_standard:15:3:standard:RC");
    if (!info.parse(setup) || info.GameMode != QStringLiteral("02_1v1")
        || info.GameRuleMode != QStringLiteral("_standard"))
        return 1;

    if (info.getCommandTimeout(QSanProtocol::S_COMMAND_CHOOSE_GENERAL,
                               QSanProtocol::S_CLIENT_INSTANCE) != 22500)
        return 2;

    SkillInstanceUtils::SkillActivationRequest request;
    if (!SkillInstanceUtils::decodeActivationRequest(
            JsonArray() << QStringLiteral("slash") << 1 << 3,
            QStringLiteral("slash"), request)
        || !request.supplied || request.instanceID != 3)
        return 3;

    QSanProtocol::ProtocolMessage replayMessage;
    replayMessage.type = QSanProtocol::ProtocolMessageType::Notification;
    replayMessage.source = QSanProtocol::ProtocolEndpoint::Room;
    replayMessage.destination = QSanProtocol::ProtocolEndpoint::Client;
    replayMessage.messageId = 1;
    replayMessage.command = QSanProtocol::S_COMMAND_ADD_PLAYER;
    replayMessage.hasPayload = true;
    replayMessage.payload = QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("player_name"), QStringLiteral("p1")},
        {QStringLiteral("screen_name"), QStringLiteral("UGxheWVy")},
        {QStringLiteral("avatar"), QStringLiteral("caocao")}};
    QString wireError;
    if (QSanProtocol::ProtocolCodecRouter().encode(replayMessage, &wireError).isEmpty())
        return 4;

    RecordBuffer recordBuffer;
    QString replayError;
    if (!recordBuffer.recordMessage(replayMessage, &replayError))
        return 5;
    const QList<QByteArray> records = recordBuffer.getRecords();
    const QSanReplay::ReplayLoadResult replay = QSanReplay::ReplayReader().read(
        recordBuffer.rawReplayData());
    if (records.size() != 1 || !replay.success
        || replay.header.protocolVersion != QSanProtocol::ProtocolVersion::V2)
        return 5;

    if (!discardSkillSelectsCardsForExplicitClientPlayerContext())
        return 6;

    const int physicalResponseResult = physicalResponseIgnoresStaleHelperActivation();
    if (physicalResponseResult != 0)
        return 70 + physicalResponseResult;

    if (!playerUIStateBuilderAggregatesPresentationState())
        return 80;

    if (!skillRegistryPreservesLegacyBehavior())
        return 90;

    if (!engineLegacySkillApisDelegateToRegistry())
        return 100;

    if (!gameStartupConstructsRoomThreadBeforePreparingViewAsEquipSkills())
        return 110;

    if (!roomSnapshotFacadePersistsAndRetrievesSnapshot())
        return 120;

    if (!collidingSkillNamesStayWithTheirOwnGenerals())
        return 130;

    qInfo() << "qsanguosha_engine smoke passed";
    return 0;
}
