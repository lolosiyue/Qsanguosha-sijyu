#include "client-core.h"
#include "controller-interaction-contract.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

#include <functional>
#include <iostream>

namespace {

int failures = 0;

void check(const char *name, bool passed)
{
    std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
    if (!passed)
        ++failures;
}

InteractionRequest cardRequest(InteractionType type, int minimum, int maximum,
                               bool cancelable = false)
{
    InteractionRequest request;
    request.type = type;
    request.responseSchema = InteractionResponseShape::Cards;
    request.cancelable = cancelable;
    CardInteractionPayload payload;
    payload.selection.minSelection = minimum;
    payload.selection.maxSelection = maximum;
    request.payload = payload;
    return request;
}

QJsonArray options(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values)
        result.append(QJsonObject{{QStringLiteral("value"), value}});
    return result;
}

QJsonObject controllerDescriptor()
{
    QJsonArray fields;
    fields.append(QJsonObject{
        {QStringLiteral("key"), QStringLiteral("choice")},
        {QStringLiteral("type"), QStringLiteral("choice")},
        {QStringLiteral("options"), options({QStringLiteral("red"), QStringLiteral("blue")})},
        {QStringLiteral("default"), QStringLiteral("red")}
    });
    fields.append(QJsonObject{
        {QStringLiteral("key"), QStringLiteral("enabled")},
        {QStringLiteral("type"), QStringLiteral("boolean")},
        {QStringLiteral("default"), true}
    });
    fields.append(QJsonObject{
        {QStringLiteral("key"), QStringLiteral("count")},
        {QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("minimum"), 2},
        {QStringLiteral("maximum"), 4},
        {QStringLiteral("default"), 3}
    });
    fields.append(QJsonObject{
        {QStringLiteral("key"), QStringLiteral("note")},
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("min_length"), 1},
        {QStringLiteral("max_length"), 8}
    });
    fields.append(QJsonObject{
        {QStringLiteral("key"), QStringLiteral("order")},
        {QStringLiteral("type"), QStringLiteral("selection")},
        {QStringLiteral("options"), options({QStringLiteral("first"), QStringLiteral("second")})},
        {QStringLiteral("min_selection"), 0},
        {QStringLiteral("max_selection"), 2}
    });
    return QJsonObject{
        {QStringLiteral("contract_version"), 1},
        {QStringLiteral("can_cancel"), true},
        {QStringLiteral("cancel_value"), QStringLiteral("ABORT")},
        {QStringLiteral("fields"), fields},
        {QStringLiteral("title"), QStringLiteral("probe presentation metadata")}
    };
}

QVariantMap validContractResponse()
{
    return QVariantMap{
        {QStringLiteral("choice"), QStringLiteral("blue")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("count"), 4},
        {QStringLiteral("note"), QStringLiteral("ok")},
        {QStringLiteral("order"), QStringList{QStringLiteral("second"), QStringLiteral("first")}}
    };
}

InteractionRequest customRequest(const QJsonObject &descriptor)
{
    InteractionRequest request;
    request.type = InteractionType::QmlInteract;
    request.responseSchema = InteractionResponseShape::Custom;
    CustomInteractionPayload payload;
    payload.schemaVersion = 1;
    payload.typeName = QStringLiteral("controller-contract-probe");
    QJsonObject parameters;
    parameters.insert(QStringLiteral("controller_ui"), descriptor);
    QJsonObject customPayload;
    customPayload.insert(QStringLiteral("parameters"), parameters);
    payload.payload = customPayload;
    request.payload = payload;
    return request;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ClientCore core;

    quint64 id = core.beginRequest(cardRequest(InteractionType::DiscardCard, 0, 1));
    check("mandatory min=0 rejects explicit Cancel",
          core.submitResponse(InteractionResponse::makeCancel(id)).rejection
              == InteractionRejection::NotCancelable);

    id = core.beginRequest(cardRequest(InteractionType::SkillGongxin, 0, 1));
    check("Gongxin native empty cards answer accepted",
          core.submitResponse(InteractionResponse::makeCards(id, {})).accepted());
    check("duplicate answer rejected as already completed",
          core.submitResponse(InteractionResponse::makeCards(id, {})).rejection
              == InteractionRejection::AlreadyCompleted);

    const quint64 staleId = id;
    const quint64 activeId = core.beginRequest(cardRequest(InteractionType::DiscardCard, 0, 1));
    check("stale answer rejected while a newer request is active",
          core.submitResponse(InteractionResponse::makeCards(staleId, {})).rejection
              == InteractionRejection::AlreadyCompleted);
    check("optional empty cards answer accepted",
          core.submitResponse(InteractionResponse::makeCards(activeId, {})).accepted());

    id = core.beginRequest(cardRequest(InteractionType::ResponseCard, 1, 1));
    check("required CardResponse rejects an empty cards answer",
          !core.submitResponse(InteractionResponse::makeCards(id, {})).accepted());

    const QJsonObject descriptor = controllerDescriptor();
    QString error;
    check("finite controller descriptor accepted",
          ControllerInteractionContract::validateDescriptor(descriptor, &error));
    QVariantMap answer = validContractResponse();
    const QStringList originalOrder = answer.value(QStringLiteral("order")).toStringList();
    check("valid finite response accepts reversed option order unchanged",
          ControllerInteractionContract::validateResponse(descriptor, answer, &error)
              && answer.value(QStringLiteral("order")).toStringList() == originalOrder);
    check("exact cancel_value accepted",
          ControllerInteractionContract::validateResponse(descriptor,
              QVariant(QStringLiteral("ABORT")), &error));
    check("nonmatching cancel_value rejected as ordinary response",
          !ControllerInteractionContract::validateResponse(descriptor,
              QVariant(QStringLiteral("ABORTED")), &error));

    QJsonObject numericStringCancel = descriptor;
    numericStringCancel.insert(QStringLiteral("cancel_value"), QStringLiteral("0"));
    check("numeric string cancel_value accepts its exact string",
          ControllerInteractionContract::validateResponse(numericStringCancel,
              QVariant(QStringLiteral("0")), &error));
    check("numeric string cancel_value rejects a coerced number",
          !ControllerInteractionContract::validateResponse(numericStringCancel,
              QVariant(0), &error));
    QJsonObject numericCancel = descriptor;
    numericCancel.insert(QStringLiteral("cancel_value"), 0);
    check("numeric cancel_value accepts an integer JSON number",
          ControllerInteractionContract::validateResponse(numericCancel, QVariant(0), &error));
    check("numeric cancel_value rejects a coerced string",
          !ControllerInteractionContract::validateResponse(numericCancel,
              QVariant(QStringLiteral("0")), &error));
    check("numeric cancel_value rejects a boolean",
          !ControllerInteractionContract::validateResponse(numericCancel, QVariant(false), &error));

    QJsonObject nestedCancel = descriptor;
    nestedCancel.insert(QStringLiteral("cancel_value"), QJsonObject{
        {QStringLiteral("status"), QJsonArray{QStringLiteral("0"), false}}
    });
    const QVariantMap exactNestedCancel{{QStringLiteral("status"),
        QVariantList{QStringLiteral("0"), false}}};
    check("nested cancel_value accepts exact JSON types",
          ControllerInteractionContract::validateResponse(nestedCancel, exactNestedCancel, &error));
    const QVariantMap coercedNestedCancel{{QStringLiteral("status"), QVariantList{0, false}}};
    check("nested cancel_value rejects numeric string coercion",
          !ControllerInteractionContract::validateResponse(nestedCancel, coercedNestedCancel, &error));
    const QVariantMap reorderedNestedCancel{{QStringLiteral("status"),
        QVariantList{false, QStringLiteral("0")}}};
    check("nested cancel_value preserves array order",
          !ControllerInteractionContract::validateResponse(nestedCancel, reorderedNestedCancel, &error));

    QVariantMap wrongType = answer;
    wrongType.insert(QStringLiteral("enabled"), QStringLiteral("true"));
    check("wrong boolean type rejected",
          !ControllerInteractionContract::validateResponse(descriptor, wrongType, &error));
    QVariantMap outOfBounds = answer;
    outOfBounds.insert(QStringLiteral("count"), 5);
    check("integer upper bound enforced",
          !ControllerInteractionContract::validateResponse(descriptor, outOfBounds, &error));
    outOfBounds.insert(QStringLiteral("count"), 1);
    check("integer lower bound enforced",
          !ControllerInteractionContract::validateResponse(descriptor, outOfBounds, &error));
    QVariantMap duplicateSelection = answer;
    duplicateSelection.insert(QStringLiteral("order"),
        QStringList{QStringLiteral("first"), QStringLiteral("first")});
    check("duplicate selection rejected",
          !ControllerInteractionContract::validateResponse(descriptor, duplicateSelection, &error));

    QJsonObject badOptions = descriptor;
    QJsonArray badFields = badOptions.value(QStringLiteral("fields")).toArray();
    QJsonObject badSelection = badFields.at(4).toObject();
    badSelection.insert(QStringLiteral("options"), options({QStringLiteral("same"), QStringLiteral("same")}));
    badFields.replace(4, badSelection);
    badOptions.insert(QStringLiteral("fields"), badFields);
    check("duplicate descriptor options rejected",
          !ControllerInteractionContract::validateDescriptor(badOptions, &error));

    QJsonObject badType = descriptor;
    badFields = badType.value(QStringLiteral("fields")).toArray();
    QJsonObject badFieldType = badFields.at(1).toObject();
    badFieldType.insert(QStringLiteral("type"), QStringLiteral("arbitrary-json-schema"));
    badFields.replace(1, badFieldType);
    badType.insert(QStringLiteral("fields"), badFields);
    check("unsupported field type rejected",
          !ControllerInteractionContract::validateDescriptor(badType, &error));

    QJsonObject badBounds = descriptor;
    badFields = badBounds.value(QStringLiteral("fields")).toArray();
    QJsonObject badIntegerBounds = badFields.at(2).toObject();
    badIntegerBounds.insert(QStringLiteral("minimum"), 5);
    badIntegerBounds.insert(QStringLiteral("maximum"), 4);
    badFields.replace(2, badIntegerBounds);
    badBounds.insert(QStringLiteral("fields"), badFields);
    check("invalid integer bounds rejected",
          !ControllerInteractionContract::validateDescriptor(badBounds, &error));

    id = core.beginRequest(customRequest(descriptor));
    const QVariantMap customEmptyCards{{QStringLiteral("choice"), QStringLiteral("red")},
        {QStringLiteral("enabled"), true}, {QStringLiteral("count"), 2},
        {QStringLiteral("note"), QStringLiteral("x")},
        {QStringLiteral("order"), QStringList{}}};
    check("custom response accepts schema-valid empty card selection",
          core.submitResponse(InteractionResponse::makeCustom(
              id, 1, QStringLiteral("controller-contract-probe"), customEmptyCards)).accepted());

    std::cout << (failures == 0 ? "RESULT PASS" : "RESULT FAIL")
              << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
