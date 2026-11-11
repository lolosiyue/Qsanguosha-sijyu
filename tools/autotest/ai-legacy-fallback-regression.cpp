#include "ai-decision-coordinator.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "runtime-paths.h"
#include "serverplayer.h"

#include <QCoreApplication>

#include <cstdio>
#include <cstdlib>

// 07p (seed 20261150): the isolated AI hit AI_INSTRUCTION_LIMIT, decide() fell back to the
// live SmartAI, and its legacy card string "296" (GudingBlade) was accepted although the
// room did not map that card to sgs3's hand. Each "use" re-equipped it; the play loop asked
// again 2063 times. These checks drive applyResult's legacy-string branch directly.
struct RoomTestAccess
{
    static AiDecisionCoordinator &decisions(Room &room) { return *room.m_aiDecisions; }
    static void addPlayer(Room &room, ServerPlayer *player) { room.addPlayerToRoster(player); }
#ifdef QSAN_FALLBACK_CONTRACT
    static bool legal(Room &room, ServerPlayer *player, const AIRequest &request,
                      const CardUseStruct &use)
    {
        return room.m_aiDecisions->aiCardUseIsLegal(player, request, use);
    }
#endif
};

namespace {

int failures = 0;
int checks = 0;

void check(const char *name, bool passed)
{
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    ++checks;
    if (!passed) ++failures;
}

int findCard(const QString &name)
{
    for (int id = 0; id < Sanguosha->getCardCount(); ++id)
        if (Sanguosha->getEngineCard(id)->objectName() == name) return id;
    std::fprintf(stderr, "Missing native card fixture: %s\n", qPrintable(name));
    std::exit(2);
}

AIRequest requestFor(Room &room, const ServerPlayer *player, AIRequest::DecisionKind kind)
{
    AIRequest request;
    request.kind = kind;
    request.decisionId = room.roomRuntime()->nextDecisionId();
    request.stateRevision = room.roomRuntime()->stateRevision();
    request.viewerObjectName = player->objectName();
    request.reason = kind == AIRequest::Activate ? CardUseStruct::CARD_USE_REASON_PLAY
        : CardUseStruct::CARD_USE_REASON_RESPONSE;
    request.handlingMethod = kind == AIRequest::Activate ? Card::MethodUse : Card::MethodResponse;
    return request;
}

// A live SmartAI answer: a card string, as AI::decide() returns for Activate.
bool legacyAnswer(Room &room, ServerPlayer *player, AIRequest::DecisionKind kind, int id)
{
    const AIRequest request = requestFor(room, player, kind);
    AIResult answer;
    answer.handled = true;
    answer.kind = AIResult::UseCard;
    answer.decisionId = request.decisionId;
    answer.stateRevision = request.stateRevision;
    answer.action.legacyCardString = QString::number(id);
    CardUseStruct use;
    const bool accepted = RoomTestAccess::decisions(room).applyResult(player, request, answer, use);
    return accepted && use.card && use.card->getEffectiveId() == id;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    if (!QSanRuntimePaths::resolve(app.arguments(), &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 2;
    }
    auto *engine = new Engine;
    auto *room = new Room(nullptr, QStringLiteral("02p"), GameSessionConfig(20261150),
                          Room::RuntimeInitializationPolicy::Deferred);
    EngineRuntimeContextScope context(*engine, room);
    room->roomRuntime()->state().reset();
    auto *self = new ServerPlayer(room);
    self->setObjectName(QStringLiteral("sgs1"));
    self->setAlive(true);
    self->setPhase(Player::Play);
    RoomTestAccess::addPlayer(*room, self);
    auto *other = new ServerPlayer(room);
    other->setObjectName(QStringLiteral("sgs2"));
    other->setAlive(true);
    RoomTestAccess::addPlayer(*room, other);

    const int handId = findCard(QStringLiteral("fan"));
    const int pileId = findCard(QStringLiteral("crossbow"));
    const int equippedId = findCard(QStringLiteral("guding_blade"));
    const int foreignId = findCard(QStringLiteral("spear"));
    const int staleId = findCard(QStringLiteral("axe"));

    self->Player::addCard(handId, Player::PlaceHand);
    room->setCardMapping(handId, self, Player::PlaceHand);
    self->setPileCardPresent(QStringLiteral("wooden_ox"), pileId, true);
    room->setCardMapping(pileId, self, Player::PlaceSpecial);
    self->setEquip(Sanguosha->getCard(equippedId));
    room->setCardMapping(equippedId, self, Player::PlaceEquip);
    other->Player::addCard(foreignId, Player::PlaceHand);
    room->setCardMapping(foreignId, other, Player::PlaceHand);
    // The 07p shape: the player's hand list still names a card the room maps elsewhere.
    self->Player::addCard(staleId, Player::PlaceHand);
    room->setCardMapping(staleId, other, Player::PlaceEquip);

    check("fixture_stale_entry_in_hand_list", self->handCards().contains(staleId));
    check("legacy_play_accepts_hand_card",
          legacyAnswer(*room, self, AIRequest::Activate, handId));
    check("legacy_play_accepts_hand_pile_card",
          legacyAnswer(*room, self, AIRequest::Activate, pileId));
    check("legacy_play_rejects_other_players_card",
          !legacyAnswer(*room, self, AIRequest::Activate, foreignId));
    check("legacy_play_rejects_stale_hand_entry",
          !legacyAnswer(*room, self, AIRequest::Activate, staleId));
    check("legacy_play_rejects_own_equipped_card",
          !legacyAnswer(*room, self, AIRequest::Activate, equippedId));
    // Responses keep the pre-existing legacy-string behaviour (out of this fix's scope).
    check("legacy_response_unchanged_for_equipped_card",
          legacyAnswer(*room, self, AIRequest::RespondCard, equippedId));

#ifdef QSAN_FALLBACK_CONTRACT
    {
        // Fallback contract: the same checks decide() applies to external answers.
        const AIRequest play = requestFor(*room, self, AIRequest::Activate);
        CardUseStruct none;
        none.from = self;
        check("contract_accepts_pass", RoomTestAccess::legal(*room, self, play, none));
        CardUseStruct slashSelf;
        slashSelf.from = self;
        slashSelf.card = Sanguosha->getCard(findCard(QStringLiteral("slash")));
        slashSelf.to << self;
        check("contract_rejects_illegal_target",
              !RoomTestAccess::legal(*room, self, play, slashSelf));
        CardUseStruct nullTarget = slashSelf;
        nullTarget.to.clear();
        nullTarget.to << nullptr;
        check("contract_rejects_missing_target",
              !RoomTestAccess::legal(*room, self, play, nullTarget));
        CardUseStruct equip;
        equip.from = self;
        equip.card = Sanguosha->getCard(handId);
        check("contract_accepts_target_fixed_equip", RoomTestAccess::legal(*room, self, play, equip));
        // 07p rerun: "@FengshangCard=..." answering "@@fengshang" was rejected because
        // matchPattern has no entry for skill prompts. Skill prompts skip the pattern check.
        AIRequest skillPrompt = requestFor(*room, self, AIRequest::UseCard);
        skillPrompt.handlingMethod = Card::MethodUse;
        skillPrompt.pattern = QStringLiteral("@@direct_card_probe");
        check("contract_skill_prompt_skips_pattern",
              RoomTestAccess::legal(*room, self, skillPrompt, equip));
        AIRequest jinkPrompt = skillPrompt;
        jinkPrompt.pattern = QStringLiteral("jink");
        check("contract_card_pattern_still_checked",
              !RoomTestAccess::legal(*room, self, jinkPrompt, equip));
    }
#endif

    std::printf("RESULT checks=%d failures=%d\n", checks, failures);
    std::fflush(stdout);
    std::fflush(stderr);
    // Like the direct-card fixture: no game worker or room Lua runtime was started,
    // so production worker teardown is skipped.
    std::_Exit(failures == 0 ? 0 : 1);
}
