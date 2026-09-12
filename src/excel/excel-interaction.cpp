#include "excel-interaction.h"

#include "client/core/client-core.h"
#include "client/interaction-command-registry.h"
#include "client/protocol-interaction-request-builder.h"
#include "client/runtime/client-rules-session.h"
#include "server-info.h"

#include <QJsonArray>

namespace {
struct ServerInfoScope
{
    const ServerInfoStruct saved = ServerInfo;
    ~ServerInfoScope() { ServerInfo = saved; }
};
bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

QList<int> ints(const QJsonValue &value, bool *ok)
{
    QList<int> result;
    if (!value.isArray()) {
        *ok = false;
        return result;
    }
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isDouble() || entry.toInt() != entry.toDouble()) {
            *ok = false;
            return {};
        }
        result.append(entry.toInt());
    }
    return result;
}

QStringList strings(const QJsonValue &value, bool *ok)
{
    QStringList result;
    if (!value.isArray()) {
        *ok = false;
        return result;
    }
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString()) {
            *ok = false;
            return {};
        }
        result.append(entry.toString());
    }
    return result;
}

bool hasOnly(const QJsonObject &object, const QStringList &allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        if (!allowed.contains(it.key()))
            return false;
    return true;
}

bool requiresNativeRules(const InteractionRequest &request)
{
    const auto *cards = request.payloadAs<CardInteractionPayload>();
    return cards && request.type != InteractionType::ChooseCard
        && !cards->selection.enumerated;
}

QJsonObject normaliseIds(const QJsonObject &object)
{
    QJsonObject result;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.value().isObject()) {
            result.insert(it.key(), normaliseIds(it.value().toObject()));
        } else if (it.value().isArray()) {
            QJsonArray array;
            for (const QJsonValue &value : it.value().toArray())
                array.append(value.isObject() ? normaliseIds(value.toObject()) : value);
            result.insert(it.key(), array);
        } else {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}

QJsonObject serializedRequest(const InteractionRequest &request)
{
    QJsonObject result = normaliseIds(request.toJson());
    result.insert(QStringLiteral("request_id"), QString::number(request.requestId));
    return result;
}

QJsonObject responseJson(const InteractionResponse &response)
{
    QJsonObject result = normaliseIds(response.toJson());
    result.insert(QStringLiteral("request_id"), QString::number(response.requestId));
    return result;
}

bool assignments(const QJsonValue &value, QStringList *names, QStringList *values)
{
    if (names == nullptr || values == nullptr)
        return false;
    names->clear();
    values->clear();
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (!it.value().isString())
                return false;
            names->append(it.key());
            values->append(it.value().toString());
        }
        return true;
    }
    if (!value.isArray())
        return false;
    for (const QJsonValue &entry : value.toArray()) {
        const QJsonObject pair = entry.toObject();
        if (!entry.isObject() || !pair.value(QStringLiteral("name")).isString()
            || !pair.value(QStringLiteral("value")).isString())
            return false;
        names->append(pair.value(QStringLiteral("name")).toString());
        values->append(pair.value(QStringLiteral("value")).toString());
    }
    return true;
}
}

ExcelInteractionAdapter::ExcelInteractionAdapter(ClientCore *core) : m_core(core) {}

bool ExcelInteractionAdapter::beginRequest(const QSanProtocol::ProtocolMessage &message,
    QString *error)
{
    if (error != nullptr)
        error->clear();
    if (m_core == nullptr)
        return fail(error, QStringLiteral("client_core_unavailable"));
    InteractionRequest request;
    if (!ProtocolInteractionRequestBuilder::build(message, *m_core->state(), &request, error))
        return false;
    if (const auto *custom = request.payloadAs<CustomInteractionPayload>()) {
        // The production QML contract carries an executable renderer path and
        // arbitrary JSON, not the invented editableUI schema. It requires a
        // reviewed worksheet mapping per renderer before it may be answered.
        return fail(error, QStringLiteral("unsupported_custom_presenter:")
            + custom->payload.value(QStringLiteral("qml_path")).toString());
    }
    const QJsonValue wire = QJsonValue::fromVariant(message.payload);
    if (!wire.isObject()) {
        return fail(error, QStringLiteral("interaction_payload_not_object"));
    }
    if (m_core->beginRequest(request) == 0)
        return fail(error, QStringLiteral("interaction_request_rejected"));
    m_wireRequestId = request.requestId;
    m_wirePayload = wire.toObject();
    return true;
}

QJsonObject ExcelInteractionAdapter::requestJson(QString *error) const
{
    if (error != nullptr)
        error->clear();
    if (m_core == nullptr || !m_core->hasActiveRequest()) {
        fail(error, QStringLiteral("no_active_request"));
        return {};
    }
    const InteractionRequest &request = m_core->activeRequest();
    QJsonObject result = serializedRequest(request);
    result.insert(QStringLiteral("command"), request.command);
    // Keep UI-facing candidate lists in the canonical payload.  Metadata is
    // display-only and never becomes a server-side constraint.
    if (const auto *cards = request.payloadAs<CardInteractionPayload>()) {
        QJsonArray candidateCards;
        for (int id : cards->selection.selectableCards)
            candidateCards.append(id);
        result.insert(QStringLiteral("candidate_cards"), candidateCards);
        QJsonArray skills;
        for (const SkillActivationCandidate &skill : cards->skillCandidates)
            skills.append(QJsonObject{{QStringLiteral("name"), skill.skillName},
                                       {QStringLiteral("instance_id"), skill.instanceId}});
        result.insert(QStringLiteral("skill_choices"), skills);
    }
    return result;
}

QJsonObject ExcelInteractionAdapter::evaluateDraft(const QJsonObject &draft,
    QString *error) const
{
    if (error != nullptr)
        error->clear();
    if (m_core == nullptr || !m_core->hasActiveRequest()) {
        fail(error, QStringLiteral("no_active_request"));
        return {};
    }
    if (!draft.value(QStringLiteral("request_id")).isUndefined()
        && draft.value(QStringLiteral("request_id")).toString()
            != QString::number(m_core->activeRequestId())) {
        fail(error, QStringLiteral("request_id_mismatch"));
        return {};
    }
    const quint64 requestId = m_core->activeRequestId();
    if (m_wireRequestId != requestId) {
        fail(error, QStringLiteral("wire_payload_unavailable"));
        return {};
    }
    const auto *descriptor = InteractionCommandRegistry::find(
        static_cast<QSanProtocol::CommandType>(m_core->activeRequest().command));
    if (descriptor != nullptr && !requiresNativeRules(m_core->activeRequest())) {
        InteractionResponse response;
        QString validationError;
        const bool valid = makeResponse(draft, &response, &validationError);
        return QJsonObject{
            {QStringLiteral("known"), true},
            {QStringLiteral("can_confirm"), valid},
            {QStringLiteral("reason"), valid ? QString() : validationError},
            {QStringLiteral("draft"), draft},
            {QStringLiteral("interaction"), serializedRequest(m_core->activeRequest())},
            {QStringLiteral("response"), valid ? responseJson(response) : QJsonObject()}};
    }
    // Option/player/assignment presenters are already fully constrained by
    // the typed request. They must not be routed through the native card rules
    // scene merely to render a menu.
    QJsonObject input;
    QJsonObject state = m_core->state()->toJson();
    state.insert(QStringLiteral("player_names"), QJsonArray::fromStringList(
        m_core->state()->playerNames()));
    input.insert(QStringLiteral("state"), state);
    input.insert(QStringLiteral("payload"), m_wirePayload);
    input.insert(QStringLiteral("schema_version"), 1);
    input.insert(QStringLiteral("generation"), 0);
    input.insert(QStringLiteral("revision"), 0);
    input.insert(QStringLiteral("command"), m_core->activeRequest().command);
    input.insert(QStringLiteral("request_id"), QString::number(m_core->activeRequestId()));
    input.insert(QStringLiteral("interaction"), serializedRequest(m_core->activeRequest()));
    QJsonObject selection = draft.value(QStringLiteral("draft")).isObject()
        ? draft.value(QStringLiteral("draft")).toObject() : draft;
    if (selection.contains(QStringLiteral("cards"))
        && !selection.contains(QStringLiteral("card_ids")))
        selection.insert(QStringLiteral("card_ids"), selection.value(QStringLiteral("cards")));
    // The worksheet names the declaration explicitly; native ViewAs skills
    // consume it through the existing user_string field.
    if (selection.contains(QStringLiteral("declaration"))) {
        const QJsonValue declaration = selection.value(QStringLiteral("declaration"));
        if (!declaration.isString()
            || (selection.contains(QStringLiteral("user_string"))
                && selection.value(QStringLiteral("user_string")) != declaration)) {
            fail(error, QStringLiteral("invalid_declaration"));
            return {};
        }
        selection.insert(QStringLiteral("user_string"), declaration);
    }
    input.insert(QStringLiteral("selection"), selection);
    ServerInfoScope serverInfoScope;
    const QJsonObject result = ClientRulesSession().evaluate(input);
    if (result.value(QStringLiteral("known")).isBool()
        && !result.value(QStringLiteral("known")).toBool()
        && error != nullptr)
        *error = result.value(QStringLiteral("reason")).toString();
    return result;
}

bool ExcelInteractionAdapter::makeResponse(const QJsonObject &draft,
    InteractionResponse *response, QString *error) const
{
    if (error != nullptr)
        error->clear();
    if (response == nullptr)
        return fail(error, QStringLiteral("response_output_null"));
    if (m_core == nullptr || !m_core->hasActiveRequest())
        return fail(error, QStringLiteral("no_active_request"));
    const InteractionRequest &request = m_core->activeRequest();
    const auto *descriptor = InteractionCommandRegistry::find(
        static_cast<QSanProtocol::CommandType>(request.command));
    if (descriptor == nullptr || descriptor->responseShape == InteractionResponseShape::None)
        return fail(error, QStringLiteral("unsupported_interaction"));
    const QString id = draft.value(QStringLiteral("request_id")).toString();
    if (!id.isEmpty() && id != QString::number(request.requestId))
        return fail(error, QStringLiteral("request_id_mismatch"));
    response->requestId = request.requestId;
    response->command = request.command;
    QJsonObject source = draft.value(QStringLiteral("draft")).isObject()
        ? draft.value(QStringLiteral("draft")).toObject() : draft;
    if (source.value(QStringLiteral("cancel")).toBool()) {
        if (!request.cancelable)
            return fail(error, QStringLiteral("not_cancelable"));
        *response = InteractionResponse::makeCancel(request.requestId);
        response->command = request.command;
    } else switch (descriptor->responseShape) {
    case InteractionResponseShape::Option: {
        if (!hasOnly(source, {QStringLiteral("request_id"), QStringLiteral("option"), QStringLiteral("value"), QStringLiteral("enabled")}))
            return fail(error, QStringLiteral("malformed_option"));
        const QString value = source.value(QStringLiteral("option")).isString()
            ? source.value(QStringLiteral("option")).toString() : source.value(QStringLiteral("value")).toString();
        if (value.isEmpty()) return fail(error, QStringLiteral("option_required"));
        *response = InteractionResponse::makeOption(request.requestId, value); break;
    }
    case InteractionResponseShape::Players: {
        bool ok = true; const QStringList names = strings(source.value(QStringLiteral("targets")), &ok);
        if (!ok) return fail(error, QStringLiteral("targets_required"));
        *response = InteractionResponse::makePlayers(request.requestId, names); break;
    }
    case InteractionResponseShape::Cards: {
        bool ok = true;
        QList<int> selected;
        if (source.contains(QStringLiteral("cards")))
            selected = ints(source.value(QStringLiteral("cards")), &ok);
        else if (source.contains(QStringLiteral("card_ids")))
            selected = ints(source.value(QStringLiteral("card_ids")), &ok);
        if (!ok || (!source.contains(QStringLiteral("cards"))
                    && !source.contains(QStringLiteral("card_ids"))))
            return fail(error, QStringLiteral("cards_required"));
        for (int id : selected) {
            if (id < 0 && !(request.type == InteractionType::ChooseCard && id == -1))
                return fail(error, QStringLiteral("invalid_card_id"));
            if (id >= 0 && m_core->state()->cardIdSpace() > 0
                && id >= m_core->state()->cardIdSpace())
                return fail(error, QStringLiteral("invalid_card_id"));
        }
        for (int i = 0; i < selected.size(); ++i)
            if (selected.indexOf(selected.at(i)) != i)
                return fail(error, QStringLiteral("duplicate_card_id"));
        QStringList targets;
        if (source.contains(QStringLiteral("targets"))) {
            if (source.value(QStringLiteral("targets")).isString())
                targets.append(source.value(QStringLiteral("targets")).toString());
            else
                targets = strings(source.value(QStringLiteral("targets")), &ok);
        }
        if (!ok) return fail(error, QStringLiteral("targets_required"));
        // Card text is generated by native rules; VBA is never allowed to
        // inject a hand-written Card::toString() representation.
        if (source.contains(QStringLiteral("card_text")))
            return fail(error, QStringLiteral("card_text_is_native_only"));
        QString cardText;
        QJsonArray normalizedCards;
        for (const int id : selected)
            normalizedCards.append(id);
        source.insert(QStringLiteral("card_ids"), normalizedCards);
        QString nativeError;
        if (requiresNativeRules(request)) {
            const QJsonObject evaluated = evaluateDraft(source, &nativeError);
            if (!evaluated.value(QStringLiteral("known")).toBool()
                || !evaluated.value(QStringLiteral("can_confirm")).toBool())
                return fail(error, nativeError.isEmpty()
                    ? evaluated.value(QStringLiteral("reason")).toString()
                    : nativeError);
            cardText = evaluated.value(QStringLiteral("card_text")).toString();
        } else if (request.type == InteractionType::ChooseCard) {
            const auto *cards = request.payloadAs<CardInteractionPayload>();
            for (int id : selected) {
                if (!cards || (id == -1 ? cards->hiddenHandCount <= 0
                    : !cards->selection.selectableCards.contains(id)))
                    return fail(error, QStringLiteral("card_unavailable"));
            }
        }
        *response = InteractionResponse::makeCards(request.requestId, selected, cardText);
        auto *value = std::get_if<InteractionResponse::CardSelectionData>(&response->payload);
        value->targets = targets;
        value->activationSkillName = source.value(QStringLiteral("skill_name")).toString();
        value->activationSkillInstanceId = source.value(QStringLiteral("skill_instance_id")).toInt();
        value->subcardIds = selected;
        break;
    }
    case InteractionResponseShape::Assignment: {
        QStringList names;
        QStringList values;
        bool ok = assignments(source.contains(QStringLiteral("assignments"))
                       ? source.value(QStringLiteral("assignments"))
                       : QJsonValue(), &names, &values);
        if (!ok && source.contains(QStringLiteral("names"))
            && source.contains(QStringLiteral("values"))) {
            bool namesOk = true;
            bool valuesOk = true;
            names = strings(source.value(QStringLiteral("names")), &namesOk);
            values = strings(source.value(QStringLiteral("values")), &valuesOk);
            ok = namesOk && valuesOk;
        }
        if (!ok) return fail(error, QStringLiteral("assignment_required"));
        *response = InteractionResponse::makeAssignment(request.requestId, names, values); break;
    }
    case InteractionResponseShape::Rearrangement: {
        bool ok = true; const QList<int> first = ints(source.value(QStringLiteral("top")), &ok);
        const QList<int> second = ints(source.value(QStringLiteral("bottom")), &ok);
        if (!ok) return fail(error, QStringLiteral("rearrangement_required"));
        *response = InteractionResponse::makeRearrangement(request.requestId, first, second); break;
    }
    case InteractionResponseShape::Distribution: {
        bool ok = true;
        const QJsonValue cardValue = source.contains(QStringLiteral("cards"))
            ? source.value(QStringLiteral("cards")) : source.value(QStringLiteral("card_ids"));
        const QList<int> ids = ints(cardValue, &ok);
        QString target = source.value(QStringLiteral("target")).toString();
        if (target.isEmpty()) {
            if (source.value(QStringLiteral("targets")).isString())
                target = source.value(QStringLiteral("targets")).toString();
            else if (source.value(QStringLiteral("targets")).isArray()) {
                bool targetOk = true;
                const QStringList values = strings(source.value(QStringLiteral("targets")), &targetOk);
                if (targetOk && values.size() == 1)
                    target = values.first();
            }
        }
        if (!ok || target.isEmpty()) return fail(error, QStringLiteral("distribution_required"));
        *response = InteractionResponse::makeDistribution(request.requestId, ids, target); break;
    }
    case InteractionResponseShape::GeneralArrangement: {
        bool ok = true; const QStringList names = strings(source.value(QStringLiteral("order")), &ok);
        if (!ok) return fail(error, QStringLiteral("general_arrangement_required"));
        *response = InteractionResponse::makeGeneralArrangement(request.requestId, names); break;
    }
    case InteractionResponseShape::Custom: {
        const auto *custom = request.payloadAs<CustomInteractionPayload>();
        if (custom == nullptr || !source.value(QStringLiteral("type")).isString()
            || !source.value(QStringLiteral("schema_version")).isDouble()
            || source.value(QStringLiteral("value")).isUndefined())
            return fail(error, QStringLiteral("custom_response_required"));
        const int schema = source.value(QStringLiteral("schema_version")).toInt();
        const QString type = source.value(QStringLiteral("type")).toString();
        if (schema != custom->schemaVersion || type != custom->typeName
            || type != QStringLiteral("qsanguosha.qml"))
            return fail(error, QStringLiteral("custom_schema_mismatch"));
        return fail(error, QStringLiteral("unsupported_custom_presenter"));
    }
    case InteractionResponseShape::None: break;
    }
    response->command = request.command;
    const InteractionValidation validation = m_core->validate(*response);
    if (!validation.accepted())
        return fail(error, validation.detail.isEmpty()
            ? interactionRejectionName(validation.rejection) : validation.detail);
    return true;
}
