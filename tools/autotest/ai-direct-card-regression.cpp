#include "ai-decision-coordinator.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "runtime-paths.h"
#include "serverplayer.h"
#include "skill.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cstdio>
#include <cstdlib>

// The existing friend grants fixture access without adding a production API.
struct RoomTestAccess
{
    static AiDecisionCoordinator &decisions(Room &room) { return *room.m_aiDecisions; }
    static void addPlayer(Room &room, ServerPlayer *player) { room.addPlayerToRoster(player); }
};

namespace {

class EquipmentConversion final : public ViewAsSkillV2
{
public:
    EquipmentConversion() : ViewAsSkillV2(QStringLiteral("direct_card_regression_conversion"), 1)
    {
        setResponseOrUse(true);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        ++activations;
        return request.initiator != nullptr;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        ++selections;
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && request.initiator->hasEquip(card);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        ++creations;
        if (request.selectedCardIds.size() != 1) return nullptr;
        Card *card = Sanguosha->cloneCard(QStringLiteral("slash"), Card::NoSuit, 0);
        if (!card) return nullptr;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        card->deleteLater();
        return card;
    }

    mutable int activations = 0;
    mutable int selections = 0;
    mutable int creations = 0;
};

int failures = 0;
QJsonArray checks;

void check(const char *name, bool passed)
{
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    checks.append(QJsonObject{{QStringLiteral("name"), QString::fromLatin1(name)},
                              {QStringLiteral("passed"), passed}});
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

AIResult answerFor(const AIRequest &request)
{
    AIResult answer;
    answer.handled = true;
    answer.kind = AIResult::UseCard;
    answer.decisionId = request.decisionId;
    answer.stateRevision = request.stateRevision;
    return answer;
}

bool directCard(Room &room, ServerPlayer *player, AIRequest::DecisionKind kind, int id)
{
    AIRequest request = requestFor(room, player, kind);
    AICardCandidateView offered;
    offered.candidateId = 1;
    offered.cardId = id;
    offered.available = true;
    offered.targetFixed = true;
    offered.feasibleWithNoTarget = true;
    offered.completeCoverage = true;
    request.cardCandidates << offered;
    AIResult answer = answerFor(request);
    answer.action.candidateId = offered.candidateId;
    answer.action.useCardId = id;
    CardUseStruct use;
    const bool accepted = RoomTestAccess::decisions(room).applyResult(player, request, answer, use);
    return accepted && use.card && use.card->getEffectiveId() == id;
}

bool convertedEquipment(Room &room, ServerPlayer *player, const EquipmentConversion *skill,
                        int instanceId, int equipmentId, AIRequest::DecisionKind kind)
{
    AIRequest request = requestFor(room, player, kind);
    ActiveSkillRequest activation;
    activation.initiator = player;
    activation.reason = request.reason;
    activation.selectedCardIds << equipmentId;
    const Card *issued = skill->createCard(activation);
    if (!issued) return false;
    AICardConversionView ticket;
    ticket.conversionId = 1;
    ticket.name = issued->objectName();
    ticket.suit = int(issued->getSuit());
    ticket.number = issued->getNumber();
    ticket.activationRef = SkillInstanceRef(player->objectName(),
        SkillInstanceKey(skill->objectName(), instanceId));
    ticket.sourceRef = ticket.activationRef;
    ticket.subcardIds << equipmentId;
    ticket.available = ticket.activationQuotaAvailable = ticket.sourceQuotaAvailable = true;
    request.cardConversions << ticket;
    AIResult answer = answerFor(request);
    answer.action.hasCardSpec = true;
    answer.action.cardSpec.name = ticket.name;
    answer.action.cardSpec.skillName = skill->objectName();
    answer.action.cardSpec.conversionId = ticket.conversionId;
    answer.action.cardSpec.subcardIds = ticket.subcardIds;
    CardUseStruct use;
    const bool accepted = RoomTestAccess::decisions(room).applyResult(player, request, answer, use);
    if (!accepted)
        std::fprintf(stderr, "Conversion fixture rejected: kind=%d activate=%d select=%d create=%d instance=%d registered=%d\n",
            int(kind), skill->activations, skill->selections, skill->creations, instanceId,
            Sanguosha->getViewAsSkill(skill->objectName()) == skill);
    return accepted
        && use.card && use.card->isKindOf("Slash") && use.hasSkillActivationRequest
        && use.card->getSubcards() == ticket.subcardIds
        && use.activationRef == ticket.activationRef;
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
    auto *player = new ServerPlayer(room);
    player->setObjectName(QStringLiteral("sgs1"));
    player->setAlive(true);
    player->setPhase(Player::Play);
    RoomTestAccess::addPlayer(*room, player);

    const int equipmentId = findCard(QStringLiteral("guding_blade"));
    const int handId = findCard(QStringLiteral("fan"));
    const int pileId = findCard(QStringLiteral("crossbow"));
    player->setEquip(Sanguosha->getCard(equipmentId));
    room->setCardMapping(equipmentId, player, Player::PlaceEquip);
    player->Player::addCard(handId, Player::PlaceHand);
    room->setCardMapping(handId, player, Player::PlaceHand);
    player->setPileCardPresent(QStringLiteral("wooden_ox"), pileId, true);
    room->setCardMapping(pileId, player, Player::PlaceSpecial);

    check("fixture_has_equipped_guding_blade", player->hasEquip(Sanguosha->getCard(equipmentId)));
    check("play_rejects_offered_equipped_card",
          !directCard(*room, player, AIRequest::Activate, equipmentId));
    check("play_accepts_hand_card", directCard(*room, player, AIRequest::Activate, handId));
    check("play_accepts_hand_pile_card", directCard(*room, player, AIRequest::Activate, pileId));
    check("response_preserves_equipped_card_path",
          directCard(*room, player, AIRequest::RespondCard, equipmentId));

    auto *conversion = new EquipmentConversion;
    engine->addSkills({conversion});
    const int instanceId = player->acquireSkill(conversion->objectName());
    check("fixture_has_conversion_instance", instanceId > 0);
    check("play_accepts_equipment_conversion",
          convertedEquipment(*room, player, conversion, instanceId, equipmentId, AIRequest::Activate));
    check("response_accepts_equipment_conversion",
          convertedEquipment(*room, player, conversion, instanceId, equipmentId, AIRequest::RespondCard));

    const QStringList args = app.arguments();
    const int reportIndex = args.indexOf(QStringLiteral("--report"));
    if (reportIndex >= 0 && reportIndex + 1 < args.size()) {
        QSaveFile report(args.at(reportIndex + 1));
        const QJsonObject result{{QStringLiteral("failures"), failures},
                                 {QStringLiteral("checks"), checks},
                                 {QStringLiteral("equipment_card_id"), equipmentId}};
        if (!report.open(QIODevice::WriteOnly)
            || report.write(QJsonDocument(result).toJson()) < 0 || !report.commit()) {
            std::fprintf(stderr, "Cannot write regression report\n");
            return 2;
        }
    }
    std::printf("RESULT checks=%lld failures=%d\n", static_cast<long long>(checks.size()), failures);
    std::fflush(stdout);
    std::fflush(stderr);
    // This fixture never starts a game worker or initializes a room Lua runtime.
    // Like h50p_distance_probe, do not run production worker teardown on that fixture.
    std::_Exit(failures == 0 ? 0 : 1);
}
