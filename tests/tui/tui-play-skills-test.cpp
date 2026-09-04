#include "engine-bootstrap.h"
#include "card.h"
#include "client-game-state.h"
#include "engine.h"
#include "interaction-model.h"
#include "standard.h"
#include "tui-client-player.h"
#include "tui-play-skills.h"
#include "tui-room-context.h"
#include "tui-skill-dialog.h"

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

const TuiSkillDeclaration *findDeclaration(const QList<TuiSkillDeclaration> &declarations,
                                           const char *name)
{
    for (const TuiSkillDeclaration &declaration : declarations) {
        if (declaration.name == QLatin1String(name))
            return &declaration;
    }
    return nullptr;
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

    // ---- Declaration dialogs -------------------------------------------
    // The desktop pops a GuhuoDialog before a Guhuo-shaped skill can build its
    // card; without one the text client could type the skill's number and get
    // nothing back. NosGuhuo is the plain case: basics and non-delayed tricks,
    // play only.
    check(tuiSkillDeclarations(QStringLiteral("wusheng"), {}).isEmpty(),
          "a skill with no dialog asks for no declaration");
    check(!tuiSkillNeedsDeclaration(QStringLiteral("wusheng"), {}),
          "a skill with no dialog needs no declaration");

    if (Sanguosha->getSkill(QStringLiteral("nosguhuo")) == nullptr) {
        std::printf("[SKIP] nosguhuo is not in this build; dialog checks skipped\n");
    } else {
        room.setCardUseContext(CardUseStruct::CARD_USE_REASON_PLAY, QString());
        const QList<TuiSkillDeclaration> declarations
            = tuiSkillDeclarations(QStringLiteral("nosguhuo"), {});
        check(!declarations.isEmpty(), "a guhuo dialog offers the cards it can declare");
        check(findDeclaration(declarations, "slash") != nullptr && findDeclaration(declarations, "jink") != nullptr,
              "the basic half of the dialog is on offer");
        check(findDeclaration(declarations, "dismantlement") != nullptr,
              "the trick half of the dialog is on offer");
        check(findDeclaration(declarations, "indulgence") == nullptr,
              "a delayed trick stays out of a dialog that did not ask for one");
        check(findDeclaration(declarations, "peach") != nullptr && !findDeclaration(declarations, "peach")->enabled,
              "an unplayable declaration is listed and marked, never dropped");
        check(tuiSkillNeedsDeclaration(QStringLiteral("nosguhuo"), {}),
              "a skill whose dialog is open has to be told what to declare");

        // Banned packages are the server's, and the text client has to pass
        // them in: nothing ever fills the engine-wide ServerInfo here. The one
        // card is spread over several packages, so ban every package holding
        // it rather than assume which one that is.
        QStringList holders;
        for (const BasicCard *engineCard : Sanguosha->findChildren<const BasicCard *>()) {
            if (engineCard->objectName() == QLatin1String("thunder_slash")
                && !holders.contains(engineCard->getPackage()))
                holders << engineCard->getPackage();
        }
        check(!holders.isEmpty(), "the thunder slash comes from somewhere");
        const QList<TuiSkillDeclaration> trimmed
            = tuiSkillDeclarations(QStringLiteral("nosguhuo"), holders);
        check(findDeclaration(trimmed, "thunder_slash") == nullptr,
              "a banned package's cards are not on offer");
        check(trimmed.size() < declarations.size(),
              "banning a package narrows the dialog rather than emptying it");

        QString error;
        check(!tuiApplySkillDeclaration(QStringLiteral("nosguhuo"), QString(), {}, &error)
                  && !error.isEmpty(),
              "activating without a declaration is refused, with the listing to fix it");
        check(error.contains(QStringLiteral("slash")),
              "the refusal says what can be declared");
        error.clear();
        check(!tuiApplySkillDeclaration(QStringLiteral("nosguhuo"),
                  QStringLiteral("no_such_card"), {}, &error) && !error.isEmpty(),
              "a declaration the dialog never offered is refused");
        check(QSanEngine::Self->getTag(QStringLiteral("nosguhuo")).isNull(),
              "a refused declaration leaves nothing behind for viewAs() to find");

        error.clear();
        check(tuiApplySkillDeclaration(QStringLiteral("nosguhuo"), QStringLiteral("slash"),
                  {}, &error) && error.isEmpty(),
              "a declaration the dialog offers is accepted");
        const Card *declared
            = QSanEngine::Self->getTag(QStringLiteral("nosguhuo")).value<const Card *>();
        check(declared != nullptr && declared->objectName() == QLatin1String("slash"),
              "the declared card lands where the skill's viewAs() reads it");
        check(declared != nullptr
                  && declared->getSkillName() == QLatin1String("nosguhuo"),
              "the declared card carries the skill that named it");

        // What the dialog is for: NosGuhuo::viewAs() hands back nothing at all
        // until the declaration is sitting in the tag, so the text client used
        // to be unable to build the card no matter what it typed.
        QSanEngine::Self->removeTag(QStringLiteral("nosguhuo"));
        QString buildError;
        check(tuiResolveSkillCardWireText(QStringLiteral("sgs1"),
                  QStringLiteral("nosguhuo"), 0, {1}, &buildError).isEmpty(),
              "an undeclared guhuo builds no card, which is why the dialog exists");
        buildError.clear();
        check(tuiApplySkillDeclaration(QStringLiteral("nosguhuo"),
                  QStringLiteral("dismantlement"), {}, &buildError) && buildError.isEmpty(),
              "a trick out of the dialog's right half can be declared too");
        const QString wire = tuiResolveSkillCardWireText(QStringLiteral("sgs1"),
            QStringLiteral("nosguhuo"), 0, {1}, &buildError);
        check(!wire.isEmpty() && wire.contains(QStringLiteral("dismantlement")),
              "a declared guhuo builds the card it named, and the wire says which");

        // The same skill outside the play phase: GuhuoDialog::shouldPopup() is
        // false, so the desktop never asks and neither does this.
        room.setCardUseContext(CardUseStruct::CARD_USE_REASON_RESPONSE,
                               QStringLiteral("slash"));
        check(tuiSkillDeclarations(QStringLiteral("nosguhuo"), {}).isEmpty(),
              "a play-only dialog stays shut when the prompt is not a play");
        check(!tuiSkillNeedsDeclaration(QStringLiteral("nosguhuo"), {}),
              "a dialog that stays shut does not hold the answer up");
        error.clear();
        check(tuiApplySkillDeclaration(QStringLiteral("nosguhuo"), QString(), {}, &error),
              "no declaration is needed while the dialog is shut");
        check(QSanEngine::Self->getTag(QStringLiteral("nosguhuo")).isNull(),
              "the previous declaration is cleared, not carried into the next answer");
        room.setCardUseContext(CardUseStruct::CARD_USE_REASON_UNKNOWN, QString());
    }

    players.clear();
    room.leaveGame();

    std::printf("[AUTOTEST] TUI_PLAY_SKILLS_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
