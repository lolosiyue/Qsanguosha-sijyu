#include "ai-strategy-preview-authority.h"
#include "ai-strategy-retrial-context.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QSet>

namespace {
bool fail(QString *error, const char *message)
{ if (error) *error = QString::fromLatin1(message); return false; }
bool base(const AIRequest &request, Room &room, ServerPlayer *viewer,
    QJsonObject &preview, QString *error)
{
    const QString skill = aiStrategyPreviewSkill(request);
    if (skill.isEmpty() || !viewer || viewer->getRoom() != &room
        || request.decisionId == 0 || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision())
        return fail(error, "preview requires a current exact owner question");
    const auto snapshot = request.choiceOptions.context.value("retrial_preview").toObject();
    preview = readAIStrategyRetrialPreview(room, viewer, skill);
    // Equality is a native-issued snapshot comparison, not a caller-supplied
    // face check. A new notification is a new serial even for identical IDs.
    if (!snapshot.value("known").toBool() || !preview.value("known").toBool()
        || !snapshot.value("serial").isString() || snapshot.value("serial").toString().isEmpty()
        || snapshot != preview)
        return fail(error, "preview witness changed or was not issued to this request");
    return true;
}
bool identity(const AIRequest &request, Room &room, ServerPlayer *viewer,
    const SkillInstanceRef &activation, const SkillInstanceRef &source, QString *error)
{
    const QString skill = aiStrategyPreviewSkill(request);
    if (!activation.isValid() || !source.isValid()
        || activation.ownerObjectName != viewer->objectName() || activation.key.skillName != skill
        || !viewer->hasSkillInstance(skill, activation.key.instanceID)
        || viewer->isSkillInvalid(skill, activation.key.instanceID))
        return fail(error, "preview activation instance is not live for this owner");
    auto root = room.resolveSkillInstanceRootRef(activation);
    if (!root.isValid()) root = activation;
    if (root != source) return fail(error, "preview activation root changed");
    bool issued = request.hasSkillActionContext && request.skillActionContext.activationRef == activation
        && request.skillActionContext.sourceRef == source;
    for (const auto &action : request.skillActions)
        if (action.activationRef == activation && action.sourceRef == source) issued = true;
    if (!issued) return fail(error, "preview activation was not issued to this request");
    return true;
}
QJsonObject faceFor(const QJsonObject &preview, int id)
{
    if (!preview.value("ids").toArray().contains(id)) return {};
    for (const auto &value : preview.value("cards").toArray()) {
        const auto face = value.toObject();
        if (face.value("effective_id").toInt(-1) == id) return face;
    }
    return {};
}
}

QString aiStrategyPreviewSkill(const AIRequest &request)
{
    if (request.kind != AIRequest::UseCard
        || (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)) return {};
    if (request.pattern == "@PlusYiji") return "PlusYiji";
    if (request.pattern == "@fatewangzhe") return "fatewangzhe";
    if (request.pattern == "@@caoduo" && request.prompt == "@xinji-use") return "caoduo";
    return {};
}

bool authorizeAIStrategyPreviewEvaluation(const AIRequest &request, Room &room, ServerPlayer *viewer,
    int cardId, QJsonObject *disclosedFace, QString *error)
{
    QJsonObject preview;
    if (!base(request, room, viewer, preview, error)) return false;
    const auto face = faceFor(preview, cardId);
    if (cardId < 0 || face.isEmpty()) return fail(error, "card is outside the issued preview");
    // Caoduo source evaluates EVERY disclosed card before checking the flag.
    // Therefore this readonly admission deliberately does not test eligibility.
    if (disclosedFace) *disclosedFace = face;
    return true;
}

bool authorizeAIStrategyPreviewPayment(const AIRequest &request, Room &room, ServerPlayer *viewer,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QList<int> &costs, QJsonArray *disclosedFaces, QString *error)
{
    QJsonObject preview;
    if (!base(request, room, viewer, preview, error)
        || !identity(request, room, viewer, activation, source, error)) return false;
    const QString skill = aiStrategyPreviewSkill(request);
    if (costs.isEmpty() || costs.size() > 2 || (skill != "PlusYiji" && costs.size() != 1))
        return fail(error, "preview payment count differs from the fixed native skill");
    QSet<int> seen; QJsonArray faces;
    for (int id : costs) {
        const auto face = faceFor(preview, id);
        if (id < 0 || seen.contains(id) || face.isEmpty())
            return fail(error, "preview payment is hidden or duplicate");
        const Card *card = room.getCard(id);
        if (!card || card->isVirtualCard()
            || card->getEffectiveId() != id || !card->hasFlag(skill))
            return fail(error, "preview payment is hidden, duplicate, or no longer flagged");
        seen.insert(id); faces.append(face);
    }
    if (disclosedFaces) *disclosedFaces = faces;
    return true;
}

bool authorizeAIStrategyPreviewPhysicalProduct(const AIRequest &request, Room &room, ServerPlayer *viewer,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    int id, QJsonObject *disclosedFace, QString *error)
{
    if (aiStrategyPreviewSkill(request) != "caoduo")
        return fail(error, "only the exact Caoduo source submits a borrowed physical product");
    QJsonArray faces;
    if (!authorizeAIStrategyPreviewPayment(request, room, viewer, activation, source, {id}, &faces, error)) return false;
    const auto *owner = room.getCardOwner(id);
    if (!owner || owner == viewer || owner->getRoom() != &room || room.getCardPlace(id) != Player::PlaceHand)
        return fail(error, "Caoduo material is no longer in the disclosed foreign hand");
    if (disclosedFace) *disclosedFace = faces.first().toObject();
    return true;
}
