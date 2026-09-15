#include "game-action-model.h"

#include <QJsonArray>

namespace {
QJsonArray entriesJson(const QList<GameActionEntry> &entries)
{
    QJsonArray result;
    for (const GameActionEntry &entry : entries) result.append(entry.toJson());
    return result;
}
}

QJsonObject GameActionEntry::toJson() const
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("enabled"), enabled}, {QStringLiteral("selected"), selected},
            {QStringLiteral("reason"), reason}};
}

bool GameActionModel::isCurrentFor(quint64 generation, quint64 revision,
                                   quint64 request) const
{
    return sessionGeneration == generation && presentationRevision == revision
        && requestId == request;
}

QJsonObject GameActionModel::toJson() const
{
    QJsonObject serializedRequest;
    if (request.isValid()) {
        serializedRequest = request.toJson();
        serializedRequest.insert(QStringLiteral("request_id"), QString::number(request.requestId));
    }
    return {{QStringLiteral("session_generation"), QString::number(sessionGeneration)},
            {QStringLiteral("presentation_revision"), QString::number(presentationRevision)},
            {QStringLiteral("request_id"), QString::number(requestId)},
            {QStringLiteral("request"), serializedRequest},
            {QStringLiteral("supported"), supported}, {QStringLiteral("unsupported_reason"), unsupportedReason},
            {QStringLiteral("prompt"), prompt}, {QStringLiteral("actions"), entriesJson(actions)},
            {QStringLiteral("action_context"), actionContext},
            {QStringLiteral("cards"), entriesJson(cards)}, {QStringLiteral("players"), entriesJson(players)},
            {QStringLiteral("skills"), entriesJson(skills)},
            {QStringLiteral("top_cards"), entriesJson(topCards)},
            {QStringLiteral("bottom_cards"), entriesJson(bottomCards)},
            {QStringLiteral("arranging_cards"), arrangingCards},
            {QStringLiteral("can_move_to_top"), canMoveToTop},
            {QStringLiteral("can_move_to_bottom"), canMoveToBottom},
            {QStringLiteral("min_selection"), minSelection}, {QStringLiteral("max_selection"), maxSelection},
            {QStringLiteral("can_confirm"), canConfirm}, {QStringLiteral("can_cancel"), canCancel},
            {QStringLiteral("can_finish"), canFinish}};
}
