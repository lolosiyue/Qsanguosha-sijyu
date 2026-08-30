#include "json.h"
#include "protocol.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"
#include "protocol/protocol-runtime.h"

#include <QTextStream>

using namespace QSanProtocol;

namespace
{
int testCaseCount = 0;

bool expect(bool condition, const QString &label)
{
    ++testCaseCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

QVariantMap object(std::initializer_list<std::pair<QString, QVariant>> fields = {})
{
    QVariantMap result{{QStringLiteral("schema_version"), 1}};
    for (const auto &field : fields)
        result.insert(field.first, field.second);
    return result;
}

ProtocolMessage request(int command, quint64 messageId,
                        const QVariant &payload = QVariant(), bool hasPayload = true)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.messageId = messageId;
    message.command = command;
    message.hasPayload = hasPayload;
    message.payload = payload;
    return message;
}

ProtocolMessage reply(int command, quint64 messageId, quint64 replyTo,
                      const QVariant &payload = QVariant(), bool hasPayload = true)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Reply;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Room;
    message.messageId = messageId;
    message.replyTo = replyTo;
    message.command = command;
    message.hasPayload = hasPayload;
    message.payload = payload;
    return message;
}

struct FlowCase
{
    QString name;
    ProtocolMessage logical;
    QVariantMap typed;
};

QList<FlowCase> requestCases()
{
    const QVariantList triggerOptions{
        QVariantMap{
            {QStringLiteral("skill"), QStringLiteral("jizhi")},
            {QStringLiteral("instanceID"), 2},
            {QStringLiteral("invoker"), QStringLiteral("p1")},
            {QStringLiteral("owner"), QStringLiteral("p1")},
            {QStringLiteral("preferredtarget"), QStringLiteral("p2")},
            {QStringLiteral("preferredtargetseat"), 3},
            {QStringLiteral("trigger_count"), 0},
            {QStringLiteral("multiplier"), 1}
        }
    };
    const QVariantMap qmlParameters{{QStringLiteral("title"), QStringLiteral("選擇")}};

    return {
        {QStringLiteral("choose role"), request(S_COMMAND_CHOOSE_ROLE, 101, {}, false), object()},
        {QStringLiteral("choose general"),
         request(S_COMMAND_CHOOSE_GENERAL, 102,
                 QVariantList{QStringLiteral("caocao"), QStringLiteral("劉備")}),
         object({{QStringLiteral("candidates"),
                  QVariantList{QStringLiteral("caocao"), QStringLiteral("劉備")}}})},
        {QStringLiteral("choose direction"),
         request(S_COMMAND_CHOOSE_DIRECTION, 103, {}, false), object()},
        {QStringLiteral("exchange card"),
         request(S_COMMAND_EXCHANGE_CARD, 104,
                 QVariantList{3, 1, true, QStringLiteral("prompt"), false,
                              QStringLiteral("hand")}),
         object({{QStringLiteral("max_cards"), 3},
                 {QStringLiteral("min_cards"), 1},
                 {QStringLiteral("include_equip"), true},
                 {QStringLiteral("prompt"), QStringLiteral("prompt")},
                 {QStringLiteral("optional"), false},
                 {QStringLiteral("pattern"), QStringLiteral("hand")}})},
        {QStringLiteral("ask peach"),
         request(S_COMMAND_ASK_PEACH, 105,
                 QVariantList{QStringLiteral("dying"), 2}),
         object({{QStringLiteral("dying_player"), QStringLiteral("dying")},
                 {QStringLiteral("peach_count"), 2}})},
        {QStringLiteral("guanxing"),
         request(S_COMMAND_SKILL_GUANXING, 106,
                 QVariantList{QVariantList{1, 2, 3}, -1}),
         object({{QStringLiteral("card_ids"), QVariantList{1, 2, 3}},
                 {QStringLiteral("mode"), QStringLiteral("down_only")}})},
        {QStringLiteral("gongxin"),
         request(S_COMMAND_SKILL_GONGXIN, 107,
                 QVariantList{QStringLiteral("p2"), true,
                              QVariantList{1, 2}, QVariantList{2}}),
         object({{QStringLiteral("player"), QStringLiteral("p2")},
                 {QStringLiteral("enable_heart"), true},
                 {QStringLiteral("card_ids"), QVariantList{1, 2}},
                 {QStringLiteral("enabled_card_ids"), QVariantList{2}}})},
        {QStringLiteral("yiji"),
         request(S_COMMAND_SKILL_YIJI, 108,
                 QVariantList{QVariantList{1, 2}, true, 2,
                              QVariantList{QStringLiteral("p2")},
                              QStringLiteral("prompt")}),
         object({{QStringLiteral("card_ids"), QVariantList{1, 2}},
                 {QStringLiteral("optional"), true},
                 {QStringLiteral("max_cards"), 2},
                 {QStringLiteral("players"), QVariantList{QStringLiteral("p2")}},
                 {QStringLiteral("prompt"), QStringLiteral("prompt")}})},
        {QStringLiteral("play card"),
         request(S_COMMAND_PLAY_CARD, 109, QStringLiteral("p1")),
         object({{QStringLiteral("player"), QStringLiteral("p1")}})},
        {QStringLiteral("response card"),
         request(S_COMMAND_RESPONSE_CARD, 110,
                 QVariantList{QStringLiteral("jink"), QStringLiteral("@jink"), 2, 7}),
         object({{QStringLiteral("pattern"), QStringLiteral("jink")},
                 {QStringLiteral("prompt"), QStringLiteral("@jink")},
                 {QStringLiteral("handling_method"), 2},
                 {QStringLiteral("notice_index"), 7}})},
        {QStringLiteral("discard card"),
         request(S_COMMAND_DISCARD_CARD, 111,
                 QVariantList{2, 1, true, false,
                              QStringLiteral("discard"), QStringLiteral("hand")}),
         object({{QStringLiteral("max_cards"), 2},
                 {QStringLiteral("min_cards"), 1},
                 {QStringLiteral("optional"), true},
                 {QStringLiteral("include_equip"), false},
                 {QStringLiteral("prompt"), QStringLiteral("discard")},
                 {QStringLiteral("pattern"), QStringLiteral("hand")}})},
        {QStringLiteral("multiple choice"),
         request(S_COMMAND_MULTIPLE_CHOICE, 112,
                 QVariantList{QStringLiteral("skill"), QStringLiteral("a+b"),
                              QStringLiteral("b"), QStringLiteral("tip")}),
         object({{QStringLiteral("skill_name"), QStringLiteral("skill")},
                 {QStringLiteral("options"),
                  QVariantList{QStringLiteral("a"), QStringLiteral("b")}},
                 {QStringLiteral("disabled_options"), QVariantList{QStringLiteral("b")}},
                 {QStringLiteral("tip"), QStringLiteral("tip")}})},
        {QStringLiteral("choose suit"), request(S_COMMAND_CHOOSE_SUIT, 113, {}, false), object()},
        {QStringLiteral("choose kingdom"),
         request(S_COMMAND_CHOOSE_KINGDOM, 114,
                 QVariantList{QStringLiteral("wei+shu")}),
         object({{QStringLiteral("kingdoms"),
                  QVariantList{QStringLiteral("wei"), QStringLiteral("shu")}}})},
        {QStringLiteral("choose player"),
         request(S_COMMAND_CHOOSE_PLAYER, 115,
                 QVariantList{QVariantList{QStringLiteral("p2"), QStringLiteral("p3")},
                              QStringLiteral("skill"), QStringLiteral("prompt"), 2, 1}),
         object({{QStringLiteral("players"),
                  QVariantList{QStringLiteral("p2"), QStringLiteral("p3")}},
                 {QStringLiteral("skill_name"), QStringLiteral("skill")},
                 {QStringLiteral("prompt"), QStringLiteral("prompt")},
                 {QStringLiteral("max_players"), 2},
                 {QStringLiteral("min_players"), 1}})},
        {QStringLiteral("invoke skill"),
         request(S_COMMAND_INVOKE_SKILL, 116,
                 QVariantList{QStringLiteral("skill"), QStringLiteral("playerdata:p1")}),
         object({{QStringLiteral("skill_name"), QStringLiteral("skill")},
                 {QStringLiteral("data"), QStringLiteral("playerdata:p1")}})},
        {QStringLiteral("trigger order"),
         request(S_COMMAND_TRIGGER_ORDER, 117,
                 QVariantList{triggerOptions, true}),
         object({{QStringLiteral("options"), triggerOptions},
                 {QStringLiteral("optional"), true}})},
        {QStringLiteral("nullification"),
         request(S_COMMAND_NULLIFICATION, 118,
                 QVariantList{QStringLiteral("duel"), QStringLiteral("p1"),
                              QStringLiteral("p2")}),
         object({{QStringLiteral("trick_name"), QStringLiteral("duel")},
                 {QStringLiteral("source_player"), QStringLiteral("p1")},
                 {QStringLiteral("target_player"), QStringLiteral("p2")}})},
        {QStringLiteral("show card"),
         request(S_COMMAND_SHOW_CARD, 119, QStringLiteral("p2")),
         object({{QStringLiteral("requestor"), QStringLiteral("p2")}})},
        {QStringLiteral("amazing grace"),
         request(S_COMMAND_AMAZING_GRACE, 120,
                 QVariantList{true, QStringLiteral("reason"), QStringLiteral("prompt")}),
         object({{QStringLiteral("refusable"), true},
                 {QStringLiteral("reason"), QStringLiteral("reason")},
                 {QStringLiteral("prompt"), QStringLiteral("prompt")}})},
        {QStringLiteral("pindian"),
         request(S_COMMAND_PINDIAN, 121,
                 QVariantList{QStringLiteral("p1"), QStringLiteral("p2")}),
         object({{QStringLiteral("requestor"), QStringLiteral("p1")},
                 {QStringLiteral("player"), QStringLiteral("p2")}})},
        {QStringLiteral("choose card"),
         request(S_COMMAND_CHOOSE_CARD, 122,
                 QVariantList{QStringLiteral("p2"), QStringLiteral("he"),
                              QStringLiteral("reason"), true, 3,
                              QVariantList{9, 10}, false}),
         object({{QStringLiteral("player"), QStringLiteral("p2")},
                 {QStringLiteral("zone_flags"), QStringLiteral("he")},
                 {QStringLiteral("reason"), QStringLiteral("reason")},
                 {QStringLiteral("hand_cards_visible"), true},
                 {QStringLiteral("handling_method"), 3},
                 {QStringLiteral("disabled_card_ids"), QVariantList{9, 10}},
                 {QStringLiteral("can_cancel"), false}})},
        {QStringLiteral("choose order"),
         request(S_COMMAND_CHOOSE_ORDER, 123,
                 static_cast<int>(S_REASON_CHOOSE_ORDER_SELECT)),
         object({{QStringLiteral("reason"), QStringLiteral("select")}})},
        {QStringLiteral("choose role 3v3"),
         request(S_COMMAND_CHOOSE_ROLE_3V3, 124,
                 QVariantList{QStringLiteral("scheme"),
                              QVariantList{QStringLiteral("lord"), QStringLiteral("guard")}}),
         object({{QStringLiteral("scheme"), QStringLiteral("scheme")},
                 {QStringLiteral("roles"),
                  QVariantList{QStringLiteral("lord"), QStringLiteral("guard")}}})},
        {QStringLiteral("surrender"),
         request(S_COMMAND_SURRENDER, 125, QStringLiteral("caocao")),
         object({{QStringLiteral("initiator_general"), QStringLiteral("caocao")}})},
        {QStringLiteral("luck card"), request(S_COMMAND_LUCK_CARD, 126, {}, false), object()},
        {QStringLiteral("ask general"), request(S_COMMAND_ASK_GENERAL, 127, {}, false), object()},
        {QStringLiteral("arrange general"),
         request(S_COMMAND_ARRANGE_GENERAL, 128,
                 QVariantList{QStringLiteral("g1"), QStringLiteral("g2"), QStringLiteral("g3")}),
         object({{QStringLiteral("generals"),
                  QVariantList{QStringLiteral("g1"), QStringLiteral("g2"),
                               QStringLiteral("g3")}}})},
        {QStringLiteral("qml interact"),
         request(S_COMMAND_QML_INTERACT, 129,
                 QVariantList{QStringLiteral("qml/Choose.qml"), qmlParameters}),
         object({{QStringLiteral("kind"), QStringLiteral("legacy_qml")},
                 {QStringLiteral("qml_path"), QStringLiteral("qml/Choose.qml")},
                 {QStringLiteral("parameters"), qmlParameters}})}
    };
}

QVariantMap responseCardObject()
{
    return object({
        {QStringLiteral("cancelled"), false},
        {QStringLiteral("card_text"), QStringLiteral("slash:1")},
        {QStringLiteral("targets"), QVariantList{QStringLiteral("p2")}},
        {QStringLiteral("activation_skill_name"), QStringLiteral("wusheng")},
        {QStringLiteral("activation_skill_instance_id"), 2}
    });
}

QList<FlowCase> replyCases()
{
    const QVariantList cardResponse{
        QStringLiteral("slash:1"), QVariantList{QStringLiteral("p2")},
        QStringLiteral("wusheng"), 2};
    const QVariantList selectedCards{1, 2};
    const QVariantMap customValue{{QStringLiteral("accepted"), true}};

    const FlowCase role{QStringLiteral("choose role"),
        reply(S_COMMAND_CHOOSE_ROLE, 201, 101,
              QVariantList{QVariantList{QStringLiteral("p1")},
                           QVariantList{QStringLiteral("lord")}}),
        object({{QStringLiteral("cancelled"), false},
                {QStringLiteral("players"), QVariantList{QStringLiteral("p1")}},
                {QStringLiteral("roles"), QVariantList{QStringLiteral("lord")}}})};
    const FlowCase general{QStringLiteral("choose general"),
        reply(S_COMMAND_CHOOSE_GENERAL, 202, 102, QStringLiteral("caocao")),
        object({{QStringLiteral("general"), QStringLiteral("caocao")}})};
    const FlowCase direction{QStringLiteral("choose direction"),
        reply(S_COMMAND_CHOOSE_DIRECTION, 203, 103, QStringLiteral("cw")),
        object({{QStringLiteral("direction"), QStringLiteral("cw")}})};
    const FlowCase cardIds{QStringLiteral("card ids"),
        reply(S_COMMAND_DISCARD_CARD, 204, 104, selectedCards),
        object({{QStringLiteral("cancelled"), false},
                {QStringLiteral("card_ids"), selectedCards}})};
    const FlowCase response{QStringLiteral("response card"),
        reply(S_COMMAND_RESPONSE_CARD, 205, 105, cardResponse), responseCardObject()};
    const FlowCase guanxing{QStringLiteral("guanxing"),
        reply(S_COMMAND_SKILL_GUANXING, 206, 106,
              QVariantList{QVariantList{1, 2}, QVariantList{3}}),
        object({{QStringLiteral("top_card_ids"), QVariantList{1, 2}},
                {QStringLiteral("bottom_card_ids"), QVariantList{3}}})};
    const FlowCase cardId{QStringLiteral("optional card id"),
        reply(S_COMMAND_SKILL_GONGXIN, 207, 107, 9),
        object({{QStringLiteral("cancelled"), false},
                {QStringLiteral("card_id"), 9}})};
    const FlowCase yiji{QStringLiteral("yiji"),
        reply(S_COMMAND_SKILL_YIJI, 208, 108,
              QVariantList{QVariantList{1, 2}, QStringLiteral("p2")}),
        object({{QStringLiteral("cancelled"), false},
                {QStringLiteral("card_ids"), QVariantList{1, 2}},
                {QStringLiteral("target_player"), QStringLiteral("p2")}})};
    const FlowCase choice{QStringLiteral("multiple choice"),
        reply(S_COMMAND_MULTIPLE_CHOICE, 209, 109, QStringLiteral("a")),
        object({{QStringLiteral("choice"), QStringLiteral("a")}})};
    const FlowCase suit{QStringLiteral("choose suit"),
        reply(S_COMMAND_CHOOSE_SUIT, 210, 110, QStringLiteral("spade")),
        object({{QStringLiteral("suit"), QStringLiteral("spade")}})};
    const FlowCase kingdom{QStringLiteral("choose kingdom"),
        reply(S_COMMAND_CHOOSE_KINGDOM, 211, 111, QStringLiteral("wei")),
        object({{QStringLiteral("kingdom"), QStringLiteral("wei")}})};
    const FlowCase players{QStringLiteral("choose player"),
        reply(S_COMMAND_CHOOSE_PLAYER, 212, 112, QStringLiteral("p2+p3")),
        object({{QStringLiteral("cancelled"), false},
                {QStringLiteral("players"),
                 QVariantList{QStringLiteral("p2"), QStringLiteral("p3")}}})};
    const FlowCase invoke{QStringLiteral("invoke skill"),
        reply(S_COMMAND_INVOKE_SKILL, 213, 113, true),
        object({{QStringLiteral("invoke"), true}})};
    const FlowCase trigger{QStringLiteral("trigger order"),
        reply(S_COMMAND_TRIGGER_ORDER, 214, 114, QStringLiteral("jizhi#2:p1:p1")),
        object({{QStringLiteral("trigger"), QStringLiteral("jizhi#2:p1:p1")}})};
    const FlowCase amazingGrace{QStringLiteral("amazing grace"),
        reply(S_COMMAND_AMAZING_GRACE, 215, 115, 12),
        object({{QStringLiteral("card_id"), 12}})};
    const FlowCase chooseCard{QStringLiteral("choose card"),
        reply(S_COMMAND_CHOOSE_CARD, 216, 116, 13),
        object({{QStringLiteral("cancelled"), false},
                {QStringLiteral("card_id"), 13}})};
    const FlowCase order{QStringLiteral("choose order"),
        reply(S_COMMAND_CHOOSE_ORDER, 217, 117, static_cast<int>(S_CAMP_COOL)),
        object({{QStringLiteral("camp"), QStringLiteral("cool")}})};
    const FlowCase role3v3{QStringLiteral("choose role 3v3"),
        reply(S_COMMAND_CHOOSE_ROLE_3V3, 218, 118, QStringLiteral("guard")),
        object({{QStringLiteral("role"), QStringLiteral("guard")}})};
    const FlowCase surrender{QStringLiteral("surrender"),
        reply(S_COMMAND_SURRENDER, 219, 119, false),
        object({{QStringLiteral("surrender"), false}})};
    const FlowCase luck{QStringLiteral("luck card"),
        reply(S_COMMAND_LUCK_CARD, 220, 120, true),
        object({{QStringLiteral("use_luck_card"), true}})};
    const FlowCase askGeneral{QStringLiteral("ask general"),
        reply(S_COMMAND_ASK_GENERAL, 221, 121, QStringLiteral("g1")),
        object({{QStringLiteral("general"), QStringLiteral("g1")}})};
    const FlowCase arrange{QStringLiteral("arrange general"),
        reply(S_COMMAND_ARRANGE_GENERAL, 222, 122,
              QVariantList{QStringLiteral("g1"), QStringLiteral("g2"),
                           QStringLiteral("g3")}),
        object({{QStringLiteral("cancelled"), false},
                {QStringLiteral("generals"),
                 QVariantList{QStringLiteral("g1"), QStringLiteral("g2"),
                              QStringLiteral("g3")}}})};
    const FlowCase qml{QStringLiteral("qml interact"),
        reply(S_COMMAND_QML_INTERACT, 223, 123, customValue),
        object({{QStringLiteral("has_value"), true},
                {QStringLiteral("value"), customValue}})};

    // Entries intentionally repeat shared reply commands. This is the production
    // request-to-reply inventory, not merely the unique wire command inventory.
    return {
        role, general, direction, cardIds, response, guanxing, cardId, yiji,
        response, response, cardIds, choice, suit, kingdom, players, invoke,
        trigger, response, response, amazingGrace, response, chooseCard, order,
        role3v3, surrender, luck, askGeneral, arrange, qml
    };
}

bool verifyRoundTrip(const FlowCase &flow)
{
    const ProtocolCodecRouter router;
    QString error;
    ProtocolMessage wire;
    if (!expect(ProtocolGameplayPayloadRegistry::isMigratedFlow(flow.logical),
                flow.name + QStringLiteral(" flow registered"))
        || !expect(ProtocolGameplayPayloadRegistry::encodeForWire(
                       ProtocolVersion::V2, flow.logical, &wire, &error),
                   flow.name + QStringLiteral(" V2 encode"))
        || !expect(wire.hasPayload && wire.payload.toMap() == flow.typed,
                   flow.name + QStringLiteral(" exact typed object"))) {
        return false;
    }

    ProtocolMessage decoded;
    if (!expect(ProtocolGameplayPayloadRegistry::decodeFromWire(
                    ProtocolVersion::V2, wire, &decoded, &error),
                flow.name + QStringLiteral(" V2 decode"))
        || !expect(decoded.hasPayload == flow.logical.hasPayload
                       && decoded.payload == flow.logical.payload,
                   flow.name + QStringLiteral(" logical parity"))) {
        return false;
    }

    const QByteArray v2Bytes = router.encode(ProtocolVersion::V2, flow.logical, &error);
    ProtocolMessage codecDecoded;
    if (!expect(!v2Bytes.isEmpty()
                    && router.decode(ProtocolVersion::V2, v2Bytes, &codecDecoded).success
                    && codecDecoded.hasPayload == flow.logical.hasPayload
                    && codecDecoded.payload == flow.logical.payload,
                flow.name + QStringLiteral(" codec boundary"))
        || !expect(router.encode(ProtocolVersion::V1, flow.logical, &error)
                       == router.encodeReplayV1(codecDecoded, &error),
                   flow.name + QStringLiteral(" replay exact V1"))) {
        return false;
    }

    ProtocolMessage extendedWire = wire;
    QVariantMap extended = extendedWire.payload.toMap();
    extended.insert(QStringLiteral("future_extension"), QStringLiteral("ignored"));
    extendedWire.payload = extended;
    ProtocolMessage extensionDecoded;
    if (!expect(ProtocolGameplayPayloadRegistry::decodeFromWire(
                    ProtocolVersion::V2, extendedWire, &extensionDecoded, &error)
                    && extensionDecoded.hasPayload == flow.logical.hasPayload
                    && extensionDecoded.payload == flow.logical.payload,
                flow.name + QStringLiteral(" ignores unknown fields"))) {
        return false;
    }

    ProtocolMessage wrongSchema = wire;
    QVariantMap malformed = wrongSchema.payload.toMap();
    malformed.insert(QStringLiteral("schema_version"), 2);
    wrongSchema.payload = malformed;
    ProtocolMessage sentinel = reply(S_COMMAND_SPEAK, 999, 998,
                                     QStringLiteral("unchanged"));
    if (!expect(!ProtocolGameplayPayloadRegistry::decodeFromWire(
                    ProtocolVersion::V2, wrongSchema, &sentinel, &error)
                    && sentinel.payload == QStringLiteral("unchanged"),
                flow.name + QStringLiteral(" rejects schema transactionally"))) {
        return false;
    }

    ProtocolMessage legacyShape = flow.logical;
    ProtocolMessage legacySentinel;
    return expect(!ProtocolGameplayPayloadRegistry::decodeFromWire(
                      ProtocolVersion::V2, legacyShape, &legacySentinel, &error),
                  flow.name + QStringLiteral(" rejects legacy V2 shape"));
}

bool allRequestAndReplyFlows()
{
    const QList<FlowCase> requests = requestCases();
    if (!expect(requests.size() == 29, QStringLiteral("29 request cases"))
        || !expect(ProtocolGameplayPayloadRegistry::migratedCommandCount() == 29,
                   QStringLiteral("migration count 29"))) {
        return false;
    }
    for (const FlowCase &flow : requests) {
        if (!expect(ProtocolGameplayPayloadRegistry::isMigratedCommand(
                        flow.logical.command),
                    flow.name + QStringLiteral(" command registered"))
            || !verifyRoundTrip(flow)) {
            return false;
        }
    }

    const QList<FlowCase> replies = replyCases();
    if (!expect(replies.size() == 29, QStringLiteral("29 reply mappings")))
        return false;
    for (const FlowCase &flow : replies) {
        if (!verifyRoundTrip(flow))
            return false;
    }
    return true;
}

bool cancellationAndOptionalShapes()
{
    const QList<FlowCase> cancellations{
        {QStringLiteral("choose role cancel"), reply(S_COMMAND_CHOOSE_ROLE, 301, 201, {}, false),
         object({{QStringLiteral("cancelled"), true}})},
        {QStringLiteral("discard cancel"), reply(S_COMMAND_DISCARD_CARD, 302, 202, {}, false),
         object({{QStringLiteral("cancelled"), true}})},
        {QStringLiteral("response cancel"), reply(S_COMMAND_RESPONSE_CARD, 303, 203, {}, false),
         object({{QStringLiteral("cancelled"), true}})},
        {QStringLiteral("gongxin cancel"), reply(S_COMMAND_SKILL_GONGXIN, 304, 204, {}, false),
         object({{QStringLiteral("cancelled"), true}})},
        {QStringLiteral("yiji cancel"), reply(S_COMMAND_SKILL_YIJI, 305, 205, {}, false),
         object({{QStringLiteral("cancelled"), true}})},
        {QStringLiteral("choose player cancel"), reply(S_COMMAND_CHOOSE_PLAYER, 306, 206, {}, false),
         object({{QStringLiteral("cancelled"), true}})},
        {QStringLiteral("choose card cancel"), reply(S_COMMAND_CHOOSE_CARD, 307, 207, {}, false),
         object({{QStringLiteral("cancelled"), true}})},
        {QStringLiteral("arrange cancel"), reply(S_COMMAND_ARRANGE_GENERAL, 308, 208, {}, false),
         object({{QStringLiteral("cancelled"), true}})},
        {QStringLiteral("qml cancel"), reply(S_COMMAND_QML_INTERACT, 309, 209, {}, false),
         object({{QStringLiteral("has_value"), false}})}
    };
    for (const FlowCase &flow : cancellations) {
        if (!verifyRoundTrip(flow))
            return false;
    }

    const FlowCase responseWithoutOptional{
        QStringLiteral("response request optional fields absent"),
        request(S_COMMAND_RESPONSE_CARD, 310,
                QVariantList{QStringLiteral("jink"), QStringLiteral("prompt")}),
        object({{QStringLiteral("pattern"), QStringLiteral("jink")},
                {QStringLiteral("prompt"), QStringLiteral("prompt")}})};
    const FlowCase arrangeWithoutPayload{
        QStringLiteral("arrange request uses filled generals"),
        request(S_COMMAND_ARRANGE_GENERAL, 311, {}, false), object()};
    const QVariantMap structuredInteraction{
        {QStringLiteral("schema_version"), 3},
        {QStringLiteral("type"), QStringLiteral("custom.test")},
        {QStringLiteral("payload"), QVariantMap{{QStringLiteral("x"), 1}}}
    };
    const FlowCase structuredQml{
        QStringLiteral("structured QML request"),
        request(S_COMMAND_QML_INTERACT, 312, structuredInteraction),
        object({{QStringLiteral("kind"), QStringLiteral("structured")},
                {QStringLiteral("interaction"), structuredInteraction}})};
    return verifyRoundTrip(responseWithoutOptional)
        && verifyRoundTrip(arrangeWithoutPayload)
        && verifyRoundTrip(structuredQml);
}

bool flowAwareIdentityExceptions()
{
    ProtocolMessage invokeNotification;
    invokeNotification.type = ProtocolMessageType::Notification;
    invokeNotification.source = ProtocolEndpoint::Room;
    invokeNotification.destination = ProtocolEndpoint::Client;
    invokeNotification.messageId = 401;
    invokeNotification.command = S_COMMAND_INVOKE_SKILL;
    invokeNotification.hasPayload = true;
    invokeNotification.payload = QVariantList{
        QStringLiteral("skill"), QStringLiteral("player")};

    ProtocolMessage surrenderInitiation;
    surrenderInitiation.type = ProtocolMessageType::Request;
    surrenderInitiation.source = ProtocolEndpoint::Client;
    surrenderInitiation.destination = ProtocolEndpoint::Room;
    surrenderInitiation.messageId = 402;
    surrenderInitiation.command = S_COMMAND_SURRENDER;
    surrenderInitiation.hasPayload = false;

    ProtocolMessage unrelated = request(S_COMMAND_SPEAK, 403, QStringLiteral("hello"));
    const QList<ProtocolMessage> identities{
        invokeNotification, surrenderInitiation, unrelated};
    for (qsizetype index = 0; index < identities.size(); ++index) {
        ProtocolMessage wire;
        QString error;
        if (!expect(!ProtocolGameplayPayloadRegistry::isMigratedFlow(identities.at(index)),
                    QStringLiteral("identity flow %1 classification").arg(index))
            || !expect(ProtocolGameplayPayloadRegistry::encodeForWire(
                           ProtocolVersion::V2, identities.at(index), &wire, &error)
                           && wire.hasPayload == identities.at(index).hasPayload
                           && wire.payload == identities.at(index).payload,
                       QStringLiteral("identity flow %1 unchanged").arg(index))) {
            return false;
        }
    }
    return true;
}
}

int main()
{
    const bool success = allRequestAndReplyFlows()
        && cancellationAndOptionalShapes()
        && flowAwareIdentityExceptions();
    QTextStream(stdout) << "[AUTOTEST] PROTOCOL_ALL_INTERACTION_PAYLOAD_RESULT status="
                        << (success ? "PASS" : "FAIL")
                        << " cases=" << testCaseCount << "\n";
    return success ? 0 : 1;
}
