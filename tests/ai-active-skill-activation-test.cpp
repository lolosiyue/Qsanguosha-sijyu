#include "engine-bootstrap.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room-test-access.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"

#include <QCoreApplication>
#include <QDebug>

#include <memory>

// Regression: a Lua AI that picks a ViewAsSkillV2 active skill in the play phase
// (sgs.ai_fill_skill returning sgs.ActiveSkillCard() with its activation instance)
// never got the skill used. AIRequest::Activate goes through AiRouteLegacyAdapted,
// AI::decide serialised the card with SkillCard::toString() ("@ActiveSkillCard[...]=."),
// and applyResult re-parsed that into an ActiveSkillCard with no skill and no
// instance, so the play phase ended instead. Seen with s4_tiaoxin after syncing
// the upstream scarlet extension (2026-09-15).
namespace {

const char *const kSkillName = "test-ai-active-activation";

bool expect(bool condition, const char *context)
{
    if (!condition)
        qCritical() << "ai-active-skill-activation:" << context;
    return condition;
}

class TestActiveSkill : public ViewAsSkillV2
{
public:
    TestActiveSkill()
        : ViewAsSkillV2(QString::fromLatin1(kSkillName))
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
};

// Mirrors what SmartAI:fillSkillCards/insertAIFillCards hand back for a V2 skill.
class ActiveSkillCardAI : public TrustAI
{
public:
    ActiveSkillCardAI(ServerPlayer *player, int instanceId)
        : TrustAI(player), m_instanceId(instanceId)
    {
    }

    void activate(CardUseStruct &card_use) override
    {
        m_card.reset(new ActiveSkillCard);
        m_card->setSkillName(QString::fromLatin1(kSkillName));
        m_card->setActivationSkill(QString::fromLatin1(kSkillName), m_instanceId);
        m_card->setSourceSkill(QString::fromLatin1(kSkillName), m_instanceId);
        card_use.card = m_card.get();
    }

private:
    int m_instanceId;
    std::unique_ptr<ActiveSkillCard> m_card;
};

void installAI(ServerPlayer *player, int instanceId)
{
    player->setState(QStringLiteral("robot"));
    ActiveSkillCardAI *ai = new ActiveSkillCardAI(player, instanceId);
    ai->setParent(player);
    player->setAI(ai);
}

bool playPhaseActivationKeepsTheSkill()
{
    TestActiveSkill skill;
    Sanguosha->addSkills(QList<const Skill *>() << &skill);

    Room room(nullptr, QStringLiteral("02_1v1"));
    EngineRuntimeContextScope scope(*Sanguosha, &room);
    RoomTestAccess::attachThread(room);
    ServerPlayer *owner = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("owner"));
    ServerPlayer *other = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("other"));
    room.setCurrent(owner);
    owner->setPhase(Player::Play);

    const int instanceId = room.acquireSkill(owner, skill.objectName(), false, false, false);
    if (!expect(instanceId > 0, "fixture: the owner acquires the V2 skill"))
        return false;

    installAI(owner, instanceId);
    const AIRequest request = RoomTestAccess::makePlayActivateRequest(room, owner);
    CardUseStruct use;
    bool ok = expect(RoomTestAccess::decideAiAction(room, owner, request, use),
                     "the activate decision is accepted");
    const ActiveSkillCard *card = qobject_cast<const ActiveSkillCard *>(use.card);
    ok &= expect(card != nullptr, "the decision yields an ActiveSkillCard");
    if (!card)
        return false;
    ok &= expect(card->getActiveSkill() == &skill, "the card resolves to the chosen V2 skill");
    ok &= expect(card->getSkillName() == skill.objectName(), "the card keeps the skill name");
    ok &= expect(card->getActivationSkillName() == skill.objectName()
                 && card->getActivationSkillInstanceId() == instanceId,
                 "the card keeps the activation instance");
    ok &= expect(use.hasSkillActivationRequest, "the use carries the activation request");
    ok &= expect(use.activationRef == SkillInstanceRef(owner->objectName(),
                     SkillInstanceKey(skill.objectName(), instanceId)),
                 "the use points at the owner's instance");

    // An instance the player does not own must not be activated.
    installAI(other, instanceId);
    const AIRequest foreign = RoomTestAccess::makePlayActivateRequest(room, other);
    CardUseStruct foreignUse;
    const bool foreignAccepted = RoomTestAccess::decideAiAction(room, other, foreign, foreignUse);
    ok &= expect(!foreignAccepted || foreignUse.card == nullptr,
                 "a claimed instance the player does not own is rejected");
    return ok;
}

} // namespace

int runAiActiveSkillActivationTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }
    if (!playPhaseActivationKeepsTheSkill())
        return 2;
    qInfo() << "ai-active-skill-activation regression passed";
    return 0;
}
