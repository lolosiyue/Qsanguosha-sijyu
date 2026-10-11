#include "ai-strategy-round33-common.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>

void projectAIRound33AmazingGrace(AIRequest &request, Room &room,
                                 ServerPlayer *viewer, const Card *trick,
                                 ServerPlayer *from, ServerPlayer *to)
{
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision()
        || request.kind!=AIRequest::RespondCard
        || request.choiceOptions.question!=QStringLiteral("askForNullification")
        || request.choiceOptions.reason!=QStringLiteral("nullification")
        || request.pattern!=QStringLiteral("nullification")
        || request.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || request.handlingMethod!=Card::MethodUse || !trick
        || trick->getClassName()!=QStringLiteral("AmazingGrace")) return;
    auto &context=request.choiceOptions.context;
    auto common=context.value("smart_common").toObject();
    QJsonObject row{{"known",false}};
    const auto publish=[&](){common.insert("amazing_grace",row);context.insert("smart_common",common);};
    const auto players=room.getAllPlayers(true);
    if (!players.contains(viewer) || !to || !players.contains(to)
        || (from && !players.contains(from))) {publish();return;}
    // Match Room::getUseStruct's exact source key without operator[] insertion.
    // The current native question supplies trick identity; neither a Lua wire
    // nor a merely matching class can nominate another historical use.
    const QVariant history=room.getTag(QStringLiteral("UseHistory")+trick->toString());
    if (!history.canConvert<CardUseStruct>()) {publish();return;}
    const auto use=history.value<CardUseStruct>();
    if (use.card!=trick || use.from!=from || !use.to.contains(to)) {publish();return;}
    QJsonArray targets;
    for (auto *target:use.to) {
        if (!target || !players.contains(target)) {publish();return;}
        targets.append(target->objectName());
    }
    const QString identity=aiLegacyCardIdentity(room,viewer,trick);
    if (identity.isEmpty()) {publish();return;}
    row=QJsonObject{{"known",true},{"card_identity",identity},{"targets",targets},
        {"ids_complete",false}};
    // This exact unkeyed tag is the donor read. Standard AmazingGrace writes
    // AmazingGrace+toString instead; substituting that displayed pool changes
    // source policy. Missing/non-QVariantList values yield [] through fixed
    // SWIG QVariant::toIntList, and no unrelated tag is exposed.
    const QVariant value=room.getTag(QStringLiteral("AmazingGrace"));
    const QVariantList values=value.canConvert<QVariantList>()?value.toList():QVariantList();
    if (values.isEmpty()) {
        row.insert("ids_complete",true);
        row.insert("ids",QJsonArray());row.insert("cards",QJsonArray());
    }
    // Public membership in a different fillAG pool does not disclose which
    // subset/order a privately restored unkeyed tag holds. The only fixed
    // active unkeyed writer (scarletayuhuo6087) writes its exhausted empty list;
    // nonempty restored/unknown-writer values have no full-sequence witness.
    // Keep ids_complete=false with no IDs/faces, even if every face is public.
    auto declared=context.value("card").toObject();
    declared.insert("legacy_identity_token",identity);
    context.insert("card",declared);
    publish();
}
