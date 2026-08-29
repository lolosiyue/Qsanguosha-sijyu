#include "legacy-v1-interaction-reply-adapter.h"
#include "interaction-reply-coordinator.h"

#include <QCoreApplication>

#include <cstdio>

using namespace QSanProtocol;

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        ++failures;
}

InteractionRequest requestFor(CommandType command)
{
    InteractionRequest request;
    request.command = command;
    return request;
}

void testScalarEncoders()
{
    const InteractionRequest choice = requestFor(S_COMMAND_MULTIPLE_CHOICE);
    LegacyV1InteractionReply reply = LegacyV1InteractionReplyAdapter::optionString(
        choice, InteractionResponse::makeOption(1, QStringLiteral("yes")));
    check(reply.command == S_COMMAND_MULTIPLE_CHOICE
            && reply.argument.toString() == QLatin1String("yes"),
        "option string preserves command and value");

    reply = LegacyV1InteractionReplyAdapter::optionBool(choice,
        InteractionResponse::makeOption(1, QStringLiteral("yes")));
    check(reply.argument.userType() == QMetaType::Bool && reply.argument.toBool(),
        "boolean option preserves the V1 bool wire type");

    reply = LegacyV1InteractionReplyAdapter::optionInt(choice,
        InteractionResponse::makeOption(1, QStringLiteral("2")));
    check(reply.argument.userType() == QMetaType::Int && reply.argument.toInt() == 2,
        "integer option preserves the V1 integer wire type");

    reply = LegacyV1InteractionReplyAdapter::playersJoined(
        requestFor(S_COMMAND_CHOOSE_PLAYER),
        InteractionResponse::makePlayers(1,
            QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs2")));
    check(reply.argument.toString() == QLatin1String("sgs1+sgs2"),
        "player selection preserves the joined V1 string");
}

void testCardEncoders()
{
    LegacyV1InteractionReply reply = LegacyV1InteractionReplyAdapter::discardCards(
        requestFor(S_COMMAND_EXCHANGE_CARD),
        InteractionResponse::makeCards(1, QList<int>() << 3 << 4));
    check(reply.command == S_COMMAND_DISCARD_CARD
            && reply.argument.toList() == (QVariantList() << 3 << 4),
        "exchange preserves the historical DISCARD_CARD reply command");

    reply = LegacyV1InteractionReplyAdapter::cardId(
        requestFor(S_COMMAND_CHOOSE_CARD),
        InteractionResponse::makeCards(1, QList<int>() << 7));
    check(reply.argument.toInt() == 7,
        "single-card replies preserve the V1 integer");

    reply = LegacyV1InteractionReplyAdapter::amazingGraceCardId(
        requestFor(S_COMMAND_AMAZING_GRACE), InteractionResponse::makeCancel(1));
    check(reply.argument.userType() == QMetaType::Int && reply.argument.toInt() == -1,
        "Amazing Grace cancel preserves the historical -1 reply");

    InteractionResponse card = InteractionResponse::makeCards(1, QList<int>(),
        QStringLiteral("slash:skill[spade:7]=12"));
    InteractionResponse::CardSelectionData &cardData
        = std::get<InteractionResponse::CardSelectionData>(card.payload);
    cardData.targets << QStringLiteral("sgs2");
    cardData.activationSkillName = QStringLiteral("skill");
    cardData.activationSkillInstanceId = 3;
    reply = LegacyV1InteractionReplyAdapter::cardResponse(
        requestFor(S_COMMAND_PLAY_CARD), card);
    const QVariantList wire = reply.argument.toList();
    check(reply.command == S_COMMAND_RESPONSE_CARD && wire.size() == 4
            && wire.at(0).toString() == cardData.cardText
            && wire.at(1).toList() == (QVariantList() << QStringLiteral("sgs2"))
            && wire.at(2).toString() == QLatin1String("skill")
            && wire.at(3).toInt() == 3,
        "card response preserves the four-field V1 payload");
}

void testStructuredEncoders()
{
    LegacyV1InteractionReply reply = LegacyV1InteractionReplyAdapter::assignment(
        requestFor(S_COMMAND_CHOOSE_ROLE),
        InteractionResponse::makeAssignment(1,
            QStringList() << QStringLiteral("sgs1"),
            QStringList() << QStringLiteral("lord")));
    const QVariantList assignment = reply.argument.toList();
    check(assignment.size() == 2
            && assignment.at(0).toList() == (QVariantList() << QStringLiteral("sgs1"))
            && assignment.at(1).toList() == (QVariantList() << QStringLiteral("lord")),
        "assignment preserves nested V1 arrays");

    reply = LegacyV1InteractionReplyAdapter::rearrangement(
        requestFor(S_COMMAND_SKILL_GUANXING),
        InteractionResponse::makeRearrangement(1,
            QList<int>() << 1 << 2, QList<int>() << 3));
    const QVariantList rearrangement = reply.argument.toList();
    check(rearrangement.size() == 2
            && rearrangement.at(0).toList() == (QVariantList() << 1 << 2)
            && rearrangement.at(1).toList() == (QVariantList() << 3),
        "rearrangement preserves top and bottom V1 arrays");

    reply = LegacyV1InteractionReplyAdapter::distribution(
        requestFor(S_COMMAND_SKILL_YIJI),
        InteractionResponse::makeDistribution(1,
            QList<int>() << 4, QStringLiteral("sgs2")));
    const QVariantList distribution = reply.argument.toList();
    check(distribution.size() == 2
            && distribution.at(0).toList() == (QVariantList() << 4)
            && distribution.at(1).toString() == QLatin1String("sgs2"),
        "distribution preserves the V1 card-array and target tuple");

    reply = LegacyV1InteractionReplyAdapter::generalArrangement(
        requestFor(S_COMMAND_ARRANGE_GENERAL),
        InteractionResponse::makeGeneralArrangement(1,
            QStringList() << QStringLiteral("caocao") << QStringLiteral("liubei")));
    check(reply.argument.toList()
            == (QVariantList() << QStringLiteral("caocao") << QStringLiteral("liubei")),
        "general arrangement preserves the V1 string array");

    reply = LegacyV1InteractionReplyAdapter::custom(
        requestFor(S_COMMAND_QML_INTERACT),
        InteractionResponse::makeCustom(1, 1, QStringLiteral("legacy.qml"), 17));
    check(reply.argument.userType() == QMetaType::Int && reply.argument.toInt() == 17,
        "legacy QML adapter preserves arbitrary QVariant replies");
}

void testUnifiedReplyBoundary()
{
    auto choiceRequest = []() {
        InteractionRequest request;
        request.type = InteractionType::Choice;
        request.command = S_COMMAND_MULTIPLE_CHOICE;
        request.responseSchema = InteractionResponseShape::Option;
        OptionInteractionPayload payload;
        payload.options << InteractionOption(QStringLiteral("yes"))
                        << InteractionOption(QStringLiteral("no"));
        request.payload = payload;
        return request;
    };

    int wireReplies = 0;
    LegacyV1InteractionReply lastReply;
    const InteractionReplyCoordinator::Sender sender
        = [&wireReplies, &lastReply](const LegacyV1InteractionReply &reply) {
            ++wireReplies;
            lastReply = reply;
        };

    ClientCore validCore;
    validCore.beginRequest(choiceRequest());
    check(InteractionReplyCoordinator::submit(&validCore,
            &LegacyV1InteractionReplyAdapter::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("yes")), sender)
            && wireReplies == 1
            && lastReply.command == S_COMMAND_MULTIPLE_CHOICE,
        "a valid response emits exactly one wire reply");
    check(!InteractionReplyCoordinator::submit(&validCore,
            &LegacyV1InteractionReplyAdapter::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("yes")), sender)
            && wireReplies == 1,
        "a duplicate response emits no second wire reply");

    ClientCore invalidCore;
    invalidCore.beginRequest(choiceRequest());
    wireReplies = 0;
    check(!InteractionReplyCoordinator::submit(&invalidCore,
            &LegacyV1InteractionReplyAdapter::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("maybe")), sender)
            && wireReplies == 0 && invalidCore.hasActiveRequest(),
        "an invalid response emits zero wire replies");

    ClientCore staleCore;
    const quint64 staleId = staleCore.beginRequest(choiceRequest());
    staleCore.beginRequest(choiceRequest());
    wireReplies = 0;
    check(!InteractionReplyCoordinator::submit(&staleCore,
            &LegacyV1InteractionReplyAdapter::optionString,
            InteractionResponse::makeOption(staleId, QStringLiteral("yes")), sender)
            && wireReplies == 0,
        "a stale response emits zero wire replies");

    qint64 clock = 0;
    ClientCore expiredCore;
    expiredCore.setClock([&clock]() { return clock; });
    InteractionRequest expiring = choiceRequest();
    expiring.timeoutMs = 5;
    expiredCore.beginRequest(expiring);
    clock = 6;
    wireReplies = 0;
    check(!InteractionReplyCoordinator::submit(&expiredCore,
            &LegacyV1InteractionReplyAdapter::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("yes")), sender)
            && wireReplies == 0,
        "an expired response emits zero wire replies");

    ClientCore cancelledCore;
    cancelledCore.beginRequest(choiceRequest());
    cancelledCore.cancelActiveRequest(InteractionCancelReason::Abandoned);
    wireReplies = 0;
    check(!InteractionReplyCoordinator::submit(&cancelledCore,
            &LegacyV1InteractionReplyAdapter::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("yes")), sender)
            && wireReplies == 0,
        "a cancelled response emits zero wire replies");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    testScalarEncoders();
    testCardEncoders();
    testStructuredEncoders();
    testUnifiedReplyBoundary();
    return failures == 0 ? 0 : 1;
}
