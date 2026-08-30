#include "interaction-reply-encoder.h"
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
    InteractionWireReply reply = InteractionReplyEncoder::optionString(
        choice, InteractionResponse::makeOption(1, QStringLiteral("yes")));
    QVariantMap payload = reply.payload.toMap();
    check(reply.command == S_COMMAND_MULTIPLE_CHOICE
            && payload.value(QStringLiteral("schema_version")).toInt() == 1
            && payload.value(QStringLiteral("choice")).toString() == QLatin1String("yes"),
        "option string emits a typed choice reply");

    reply = InteractionReplyEncoder::optionBool(requestFor(S_COMMAND_INVOKE_SKILL),
        InteractionResponse::makeOption(1, QStringLiteral("yes")));
    payload = reply.payload.toMap();
    check(payload.value(QStringLiteral("invoke")).toBool(),
        "boolean option emits a typed invoke reply");

    reply = InteractionReplyEncoder::optionInt(requestFor(S_COMMAND_CHOOSE_ORDER),
        InteractionResponse::makeOption(1, QStringLiteral("1")));
    payload = reply.payload.toMap();
    check(payload.value(QStringLiteral("camp")).toString() == QLatin1String("cool"),
        "integer option emits a typed order reply");

    reply = InteractionReplyEncoder::playersJoined(
        requestFor(S_COMMAND_CHOOSE_PLAYER),
        InteractionResponse::makePlayers(1,
            QStringList() << QStringLiteral("sgs1") << QStringLiteral("sgs2")));
    payload = reply.payload.toMap();
    check(!payload.value(QStringLiteral("cancelled")).toBool()
            && payload.value(QStringLiteral("players")).toList()
                == (QVariantList() << QStringLiteral("sgs1") << QStringLiteral("sgs2")),
        "player selection emits a typed player array");
}

void testCardEncoders()
{
    InteractionWireReply reply = InteractionReplyEncoder::discardCards(
        requestFor(S_COMMAND_EXCHANGE_CARD),
        InteractionResponse::makeCards(1, QList<int>() << 3 << 4));
    QVariantMap payload = reply.payload.toMap();
    check(reply.command == S_COMMAND_DISCARD_CARD
            && !payload.value(QStringLiteral("cancelled")).toBool()
            && payload.value(QStringLiteral("card_ids")).toList()
                == (QVariantList() << 3 << 4),
        "exchange emits the registered typed DISCARD_CARD reply");

    reply = InteractionReplyEncoder::cardId(
        requestFor(S_COMMAND_CHOOSE_CARD),
        InteractionResponse::makeCards(1, QList<int>() << 7));
    payload = reply.payload.toMap();
    check(!payload.value(QStringLiteral("cancelled")).toBool()
            && payload.value(QStringLiteral("card_id")).toInt() == 7,
        "single-card reply uses an explicit card_id");

    reply = InteractionReplyEncoder::amazingGraceCardId(
        requestFor(S_COMMAND_AMAZING_GRACE), InteractionResponse::makeCancel(1));
    payload = reply.payload.toMap();
    check(payload.value(QStringLiteral("cancelled")).toBool()
            && !payload.contains(QStringLiteral("card_id")),
        "Amazing Grace cancel uses an explicit discriminator");

    InteractionResponse card = InteractionResponse::makeCards(1, QList<int>(),
        QStringLiteral("slash:skill[spade:7]=12"));
    InteractionResponse::CardSelectionData &cardData
        = std::get<InteractionResponse::CardSelectionData>(card.payload);
    cardData.targets << QStringLiteral("sgs2");
    cardData.activationSkillName = QStringLiteral("skill");
    cardData.activationSkillInstanceId = 3;
    reply = InteractionReplyEncoder::cardResponse(
        requestFor(S_COMMAND_PLAY_CARD), card);
    payload = reply.payload.toMap();
    check(reply.command == S_COMMAND_RESPONSE_CARD
            && !payload.value(QStringLiteral("cancelled")).toBool()
            && payload.value(QStringLiteral("card_text")).toString() == cardData.cardText
            && payload.value(QStringLiteral("targets")).toList()
                == (QVariantList() << QStringLiteral("sgs2"))
            && payload.value(QStringLiteral("activation_skill_name")).toString()
                == QLatin1String("skill")
            && payload.value(QStringLiteral("activation_skill_instance_id")).toInt() == 3,
        "card response emits named typed fields");
}

void testStructuredEncoders()
{
    InteractionWireReply reply = InteractionReplyEncoder::assignment(
        requestFor(S_COMMAND_CHOOSE_ROLE),
        InteractionResponse::makeAssignment(1,
            QStringList() << QStringLiteral("sgs1"),
            QStringList() << QStringLiteral("lord")));
    QVariantMap payload = reply.payload.toMap();
    check(!payload.value(QStringLiteral("cancelled")).toBool()
            && payload.value(QStringLiteral("players")).toList()
                == (QVariantList() << QStringLiteral("sgs1"))
            && payload.value(QStringLiteral("roles")).toList()
                == (QVariantList() << QStringLiteral("lord")),
        "assignment emits named typed arrays");

    reply = InteractionReplyEncoder::rearrangement(
        requestFor(S_COMMAND_SKILL_GUANXING),
        InteractionResponse::makeRearrangement(1,
            QList<int>() << 1 << 2, QList<int>() << 3));
    payload = reply.payload.toMap();
    check(payload.value(QStringLiteral("top_card_ids")).toList()
                == (QVariantList() << 1 << 2)
            && payload.value(QStringLiteral("bottom_card_ids")).toList()
                == (QVariantList() << 3),
        "rearrangement emits named top and bottom arrays");

    reply = InteractionReplyEncoder::distribution(
        requestFor(S_COMMAND_SKILL_YIJI),
        InteractionResponse::makeDistribution(1,
            QList<int>() << 4, QStringLiteral("sgs2")));
    payload = reply.payload.toMap();
    check(!payload.value(QStringLiteral("cancelled")).toBool()
            && payload.value(QStringLiteral("card_ids")).toList()
                == (QVariantList() << 4)
            && payload.value(QStringLiteral("target_player")).toString()
                == QLatin1String("sgs2"),
        "distribution emits named card and target fields");

    reply = InteractionReplyEncoder::generalArrangement(
        requestFor(S_COMMAND_ARRANGE_GENERAL),
        InteractionResponse::makeGeneralArrangement(1,
            QStringList() << QStringLiteral("caocao") << QStringLiteral("liubei")));
    payload = reply.payload.toMap();
    check(!payload.value(QStringLiteral("cancelled")).toBool()
            && payload.value(QStringLiteral("generals")).toList()
                == (QVariantList() << QStringLiteral("caocao") << QStringLiteral("liubei")),
        "general arrangement emits a typed general array");

    reply = InteractionReplyEncoder::custom(
        requestFor(S_COMMAND_QML_INTERACT),
        InteractionResponse::makeCustom(1, 1, QStringLiteral("custom.qml"), 17));
    payload = reply.payload.toMap();
    check(payload.value(QStringLiteral("has_value")).toBool()
            && payload.value(QStringLiteral("value")).toInt() == 17,
        "QML reply uses the registered typed outer schema");
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
    InteractionWireReply lastReply;
    const InteractionReplyCoordinator::Sender sender
        = [&wireReplies, &lastReply](const InteractionWireReply &reply) {
            ++wireReplies;
            lastReply = reply;
        };

    ClientCore validCore;
    validCore.beginRequest(choiceRequest());
    check(InteractionReplyCoordinator::submit(&validCore,
            &InteractionReplyEncoder::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("yes")), sender)
            && wireReplies == 1
            && lastReply.command == S_COMMAND_MULTIPLE_CHOICE,
        "a valid response emits exactly one wire reply");
    check(!InteractionReplyCoordinator::submit(&validCore,
            &InteractionReplyEncoder::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("yes")), sender)
            && wireReplies == 1,
        "a duplicate response emits no second wire reply");

    ClientCore invalidCore;
    invalidCore.beginRequest(choiceRequest());
    wireReplies = 0;
    check(!InteractionReplyCoordinator::submit(&invalidCore,
            &InteractionReplyEncoder::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("maybe")), sender)
            && wireReplies == 0 && invalidCore.hasActiveRequest(),
        "an invalid response emits zero wire replies");

    ClientCore staleCore;
    const quint64 staleId = staleCore.beginRequest(choiceRequest());
    staleCore.beginRequest(choiceRequest());
    wireReplies = 0;
    check(!InteractionReplyCoordinator::submit(&staleCore,
            &InteractionReplyEncoder::optionString,
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
            &InteractionReplyEncoder::optionString,
            InteractionResponse::makeOption(0, QStringLiteral("yes")), sender)
            && wireReplies == 0,
        "an expired response emits zero wire replies");

    ClientCore cancelledCore;
    cancelledCore.beginRequest(choiceRequest());
    cancelledCore.cancelActiveRequest(InteractionCancelReason::Abandoned);
    wireReplies = 0;
    check(!InteractionReplyCoordinator::submit(&cancelledCore,
            &InteractionReplyEncoder::optionString,
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
