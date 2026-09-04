#include "engine-bootstrap.h"
#include "card.h"
#include "client-game-state.h"
#include "engine.h"
#include "interaction-model.h"
#include "tui-client-player.h"
#include "tui-play-skills.h"
#include "tui-room-context.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

void addPlayer(ClientGameState *state, const QString &name, int seat, const QString &general,
               const QStringList &skills = {})
{
    state->addPlayer(name);
    state->setPlayerValue(name, QStringLiteral("object_name"), name);
    state->setPlayerValue(name, QStringLiteral("seat"), seat);
    state->setPlayerValue(name, QStringLiteral("general"), general);
    state->setPlayerValue(name, QStringLiteral("hp"), 4);
    state->setPlayerValue(name, QStringLiteral("max_hp"), 4);
    state->setPlayerValue(name, QStringLiteral("role"), QStringLiteral("loyalist"));
    state->setPlayerValue(name, QStringLiteral("skills"), skills);
    state->setPlayerAlive(name, true);
}

bool offers(const CardInteractionPayload &payload, const char *skillName)
{
    for (const SkillActivationCandidate &candidate : payload.skillCandidates) {
        if (candidate.skillName == QLatin1String(skillName))
            return true;
    }
    return false;
}

CardInteractionPayload fill(const ClientGameState &state, const QString &pattern)
{
    CardInteractionPayload payload;
    tuiFillSkillCandidates(state, pattern, &payload);
    return payload;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    // A pattern that names one skill is the server saying "only this skill's
    // card answers". Same shape RoomScene matches at Client::Responding.
    int instanceId = -1;
    check(tuiPatternSkillName(QStringLiteral("@@tuxi"), &instanceId) == QLatin1String("tuxi")
              && instanceId == 0,
          "@@name is the skill the prompt asks for");
    check(tuiPatternSkillName(QStringLiteral("@rende")) == QLatin1String("rende"),
          "a single @ names a skill too");
    check(tuiPatternSkillName(QStringLiteral("@@guhuo3"), &instanceId) == QLatin1String("guhuo")
              && instanceId == 3,
          "trailing digits are the activation instance, not part of the name");
    check(tuiPatternSkillName(QStringLiteral("@@tuxi!")) == QLatin1String("tuxi"),
          "a forced prompt still names its skill");
    check(tuiPatternSkillName(QStringLiteral("slash")).isEmpty(),
          "a card pattern names no skill");
    check(tuiPatternSkillName(QStringLiteral(".|red|.|hand")).isEmpty(),
          "a card-property pattern names no skill");
    check(tuiPatternSkillName(QStringLiteral("@@")).isEmpty(),
          "an empty name is not a skill");

    // Which prompts offer skills at all, and under which use reason. The
    // desktop reads this off Client::Status; the same splits have to survive.
    check(tuiSkillPromptReason(InteractionType::PlayCard, Card::MethodUse, QString())
              == CardUseStruct::CARD_USE_REASON_PLAY,
          "the play phase activates skills as a play");
    check(tuiSkillPromptReason(InteractionType::ResponseCard, Card::MethodResponse,
              QStringLiteral("slash")) == CardUseStruct::CARD_USE_REASON_RESPONSE,
          "answering with a card activates skills as a response");
    check(tuiSkillPromptReason(InteractionType::ResponseCard, Card::MethodUse,
              QStringLiteral("slash")) == CardUseStruct::CARD_USE_REASON_RESPONSE_USE,
          "being asked to use a card activates skills as a response-use");
    check(tuiSkillPromptReason(InteractionType::ResponseCard, Card::MethodPlay, QString())
              == CardUseStruct::CARD_USE_REASON_PLAY,
          "a play-method response is the play phase by another name");
    // RespondingForDiscard and RespondingNonTrigger leave RoomScene's reason
    // UNKNOWN unless the pattern names a skill.
    check(tuiSkillPromptReason(InteractionType::ResponseCard, Card::MethodDiscard,
              QStringLiteral(".|black")) == CardUseStruct::CARD_USE_REASON_UNKNOWN,
          "being asked to discard a card activates no skill on its own");
    check(tuiSkillPromptReason(InteractionType::ResponseCard, Card::MethodDiscard,
              QStringLiteral("@@tuxi")) == CardUseStruct::CARD_USE_REASON_RESPONSE,
          "a named prompt activates its skill even when the method is a discard");
    // The wire carries no handling method for these two; the desktop presents
    // both as RespondingUse all the same.
    check(tuiSkillPromptReason(InteractionType::AskPeach, -1,
              QStringLiteral("peach+analeptic")) == CardUseStruct::CARD_USE_REASON_RESPONSE_USE,
          "saving a dying player activates skills as a response-use");
    check(tuiSkillPromptReason(InteractionType::Nullification, -1,
              QStringLiteral("nullification")) == CardUseStruct::CARD_USE_REASON_RESPONSE_USE,
          "nullifying activates skills as a response-use");
    check(tuiSkillPromptReason(InteractionType::ChooseCard, Card::MethodNone, QString())
              == CardUseStruct::CARD_USE_REASON_UNKNOWN,
          "picking a card off a player is not a skill activation");
    check(tuiSkillPromptReason(InteractionType::Pindian, Card::MethodResponse, QString())
              == CardUseStruct::CARD_USE_REASON_UNKNOWN,
          "pindian offers no skills, the way the desktop's AskForShowOrPindian does not");

    ClientGameState state;
    state.setSetup(QVariantMap{{QStringLiteral("mode"), QStringLiteral("03_1v2")}});
    state.setSelfName(QStringLiteral("sgs1"));
    addPlayer(&state, QStringLiteral("sgs1"), 1, QStringLiteral("guanyu"),
              {QStringLiteral("wusheng"), QStringLiteral("tuxi")});
    addPlayer(&state, QStringLiteral("sgs2"), 2, QStringLiteral("liubei"));
    state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2")});

    TuiRoomContext room(&state);
    TuiPlayerModel players(&state);
    room.setOwnerResolver([&players](int cardId) { return players.cardOwner(cardId); });
    room.enterGame();
    players.sync();
    check(players.self() != nullptr && players.self()->hasSkill(QStringLiteral("wusheng")),
          "the engine-backed self carries the skills the server sent");

    // The gap this suite exists for: a response prompt used to offer no skills
    // at all, so Wusheng could never answer a slash.
    const CardInteractionPayload responding
        = fill(state, QStringLiteral("slash"));
    check(offers(responding, "wusheng"), "a response prompt offers the player's view-as skills");

    // A named prompt is exclusive: the desktop force-enters that skill's
    // pending mode and no other button can be pressed.
    const CardInteractionPayload named
        = fill(state, QStringLiteral("@@tuxi"));
    check(named.skillCandidates.size() == 1 && offers(named, "tuxi"),
          "a prompt that names a skill offers that skill alone");

    // The server only asks for a skill the player can actually activate, which
    // includes skills held through an effect mark rather than the skill list.
    ClientGameState borrowed;
    borrowed.setSelfName(QStringLiteral("sgs1"));
    addPlayer(&borrowed, QStringLiteral("sgs1"), 1, QStringLiteral("guanyu"),
              {QStringLiteral("wusheng")});
    borrowed.setPlayerNames({QStringLiteral("sgs1")});
    const CardInteractionPayload lent
        = fill(borrowed, QStringLiteral("@@tuxi"));
    check(lent.skillCandidates.size() == 1 && offers(lent, "tuxi"),
          "a named skill is offered even when it is not in the player's skill list");

    // Not every @-shaped pattern is a skill. One that resolves to nothing must
    // not take the player's own skills away with it.
    const CardInteractionPayload stranger = fill(state, QStringLiteral("@not_a_skill_at_all"));
    check(offers(stranger, "wusheng") && offers(stranger, "tuxi"),
          "a pattern naming no known skill leaves the skill list alone");

    // Marking, never filtering: an unusable skill is still listed and still
    // selectable, it just says so.
    check(tuiSkillActivationHint(QStringLiteral("wusheng"), 0,
              CardUseStruct::CARD_USE_REASON_RESPONSE, QStringLiteral("slash")).isEmpty(),
          "Wusheng answers a slash");
    check(!tuiSkillActivationHint(QStringLiteral("wusheng"), 0,
              CardUseStruct::CARD_USE_REASON_RESPONSE, QStringLiteral("jink")).isEmpty(),
          "Wusheng does not answer a jink");
    check(tuiSkillActivationHint(QStringLiteral("tuxi"), 0,
              CardUseStruct::CARD_USE_REASON_RESPONSE, QStringLiteral("@@tuxi")).isEmpty(),
          "a skill answers its own named prompt");
    check(!tuiSkillActivationHint(QStringLiteral("tuxi"), 0,
              CardUseStruct::CARD_USE_REASON_PLAY, QString()).isEmpty(),
          "a response-only skill is not playable in the play phase");

    players.clear();
    room.leaveGame();

    std::printf("[AUTOTEST] TUI_PLAY_SKILLS_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
