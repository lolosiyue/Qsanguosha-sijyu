#include "excel/excel-interaction.h"

#include "client/core/client-core.h"
#include "protocol.h"
#include "protocol/protocol-message.h"

#include <QtTest>
#include <limits>

class ExcelInteractionTest final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsWithoutCore()
    {
        ExcelInteractionAdapter adapter(nullptr);
        QString error;
        QVERIFY(!adapter.beginRequest({}, &error));
        QCOMPARE(error, QStringLiteral("client_core_unavailable"));
    }

    void rejectsResponseWithoutRequest()
    {
        ClientCore core;
        ExcelInteractionAdapter adapter(&core);
        InteractionResponse response;
        QString error;
        QVERIFY(!adapter.makeResponse(QJsonObject(), &response, &error));
        QCOMPARE(error, QStringLiteral("no_active_request"));
    }

    void requestIdIsPresentedAsString()
    {
        ClientCore core;
        ExcelInteractionAdapter adapter(&core);
        QString error;
        QCOMPARE(adapter.requestJson(&error), QJsonObject());
        QCOMPARE(error, QStringLiteral("no_active_request"));
    }

    void mapsTypedShapesAndRejectsMalformedInput()
    {
        ClientCore core;
        core.state()->setCardIdSpace(8);
        ExcelInteractionAdapter adapter(&core);
        QString error;
        auto begin = [&](InteractionResponseShape shape, const InteractionPayload &payload) {
            InteractionRequest request;
            request.type = InteractionType::Choice;
            request.responseSchema = shape;
            request.command = shape == InteractionResponseShape::Option
                ? QSanProtocol::S_COMMAND_MULTIPLE_CHOICE
                : shape == InteractionResponseShape::Players ? QSanProtocol::S_COMMAND_CHOOSE_PLAYER
                : shape == InteractionResponseShape::Cards ? QSanProtocol::S_COMMAND_RESPONSE_CARD
                : shape == InteractionResponseShape::Assignment ? QSanProtocol::S_COMMAND_CHOOSE_ROLE
                : shape == InteractionResponseShape::Rearrangement ? QSanProtocol::S_COMMAND_SKILL_GUANXING
                : shape == InteractionResponseShape::Distribution ? QSanProtocol::S_COMMAND_SKILL_YIJI
                : shape == InteractionResponseShape::GeneralArrangement ? QSanProtocol::S_COMMAND_ARRANGE_GENERAL
                : QSanProtocol::S_COMMAND_QML_INTERACT;
            request.payload = payload;
            QVERIFY(core.beginRequest(request) != 0);
        };
        InteractionResponse response;

        OptionInteractionPayload option;
        option.options.append(InteractionOption(QStringLiteral("yes")));
        begin(InteractionResponseShape::Option, option);
        QVERIFY(adapter.makeResponse(QJsonObject{{QStringLiteral("option"), QStringLiteral("yes")}}, &response, &error));
        QVERIFY(core.validate(response).accepted());

        PlayerInteractionPayload players;
        players.selection.selectablePlayers << QStringLiteral("A");
        players.selection.minSelection = players.selection.maxSelection = 1;
        begin(InteractionResponseShape::Players, players);
        QVERIFY(adapter.makeResponse(QJsonObject{{QStringLiteral("targets"), QJsonArray{QStringLiteral("A")}}}, &response, &error));
        QVERIFY(core.validate(response).accepted());

        CardInteractionPayload cards;
        cards.selection.minSelection = cards.selection.maxSelection = 1;
        cards.selection.selectableCards << 2;
        cards.selection.enumerated = true;
        begin(InteractionResponseShape::Cards, cards);
        QVERIFY(adapter.makeResponse(QJsonObject{{QStringLiteral("card_ids"), QJsonArray{2}}}, &response, &error));
        QVERIFY(core.validate(response).accepted());
        QVERIFY(!adapter.makeResponse(QJsonObject{{QStringLiteral("card_ids"), QJsonArray{99}}}, &response, &error));

        begin(InteractionResponseShape::Assignment, RoleAssignmentInteractionPayload{});
        QVERIFY(adapter.makeResponse(QJsonObject{{QStringLiteral("names"), QJsonArray{QStringLiteral("A")}},
                                                 {QStringLiteral("values"), QJsonArray{QStringLiteral("lord")}}}, &response, &error));
        RearrangeCardsInteractionPayload rearrange;
        rearrange.cardIds = {1, 2};
        rearrange.maxTop = rearrange.maxBottom = 2;
        begin(InteractionResponseShape::Rearrangement, rearrange);
        QVERIFY(adapter.makeResponse(QJsonObject{{QStringLiteral("top"), QJsonArray{1}},
                                                 {QStringLiteral("bottom"), QJsonArray{2}}}, &response, &error));
        YijiInteractionPayload distribution;
        distribution.cardIds = {1, 2};
        distribution.targetPlayers = {QStringLiteral("A")};
        distribution.minCards = 1;
        distribution.maxCards = 2;
        begin(InteractionResponseShape::Distribution, distribution);
        QVERIFY(adapter.makeResponse(QJsonObject{{QStringLiteral("cards"), QJsonArray{1}},
                                                 {QStringLiteral("target"), QStringLiteral("A")}}, &response, &error));
        ArrangeGeneralsInteractionPayload generals;
        generals.generalNames = {QStringLiteral("caocao")};
        generals.slotCount = 1;
        begin(InteractionResponseShape::GeneralArrangement, generals);
        QVERIFY(adapter.makeResponse(QJsonObject{{QStringLiteral("order"), QJsonArray{QStringLiteral("caocao")}}}, &response, &error));
        CustomInteractionPayload custom;
        custom.typeName = QStringLiteral("qsanguosha.qml");
        custom.schemaVersion = 1;
        custom.responseSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("json")}};
        begin(InteractionResponseShape::Custom, custom);
        QVERIFY(!adapter.makeResponse(QJsonObject{{QStringLiteral("schema_version"), 1},
                                                 {QStringLiteral("type"), QStringLiteral("qsanguosha.qml")},
                                                 {QStringLiteral("value"), QStringLiteral("x")}}, &response, &error));
        QCOMPARE(error, QStringLiteral("unsupported_custom_presenter"));
        QVERIFY(!adapter.makeResponse(QJsonObject{{QStringLiteral("schema_version"), 2},
                                                  {QStringLiteral("type"), QStringLiteral("qsanguosha.qml")},
                                                  {QStringLiteral("value"), QStringLiteral("x")}}, &response, &error));
    }

    void preservesFullQuint64RequestId()
    {
        ClientCore core;
        ExcelInteractionAdapter adapter(&core);
        InteractionRequest request;
        request.requestId = std::numeric_limits<quint64>::max();
        request.type = InteractionType::Choice;
        request.responseSchema = InteractionResponseShape::Option;
        OptionInteractionPayload payload;
        payload.options.append(InteractionOption(QStringLiteral("ok")));
        request.payload = payload;
        QVERIFY(core.beginRequest(request) == request.requestId);
        QString error;
        const QJsonObject json = adapter.requestJson(&error);
        QCOMPARE(json.value(QStringLiteral("request_id")).toString(),
                 QString::number(std::numeric_limits<quint64>::max()));
    }
};

QTEST_APPLESS_MAIN(ExcelInteractionTest)
#include "interaction-test.moc"
