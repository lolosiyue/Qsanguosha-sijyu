#include "skill-receipt.h"
#include "room.h"

namespace SkillReceipt {
namespace {

QByteArray playerProperty(const QString &name)
{
    return (name + "_v2_effects").toUtf8();
}

// The property stays authoritative after skill loss; the source state only mirrors it for the UI.
void syncPlayerReceiptState(Room *room, const QVariantMap &receipt, bool clear)
{
    static constexpr char amountKey[] = "receipt_amount";
    ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
    const SkillInstanceRef source = sourceRef(receipt);
    if (!owner || !source.isValid()) return;
    if (clear) owner->removeSkillInstanceStateValue(source.key.skillName, source.key.instanceID, amountKey);
    else owner->setSkillInstanceStateValue(source.key.skillName, source.key.instanceID,
        amountKey, receipt.value("amount"), SkillStatePublic);
}

}

int nextDispatch(Room *room, ServerPlayer *recipient, const QString &name)
{
    // Source instance numbers are owner-local; continuations need their own stable key.
    const QByteArray key = (name + "_v2_receipt_sequence").toUtf8();
    const int id = recipient->property(key.constData()).toInt() + 1;
    room->setPlayerProperty(recipient, key.constData(), id);
    return id;
}

int nextDispatch(ServerPlayer *recipient, const QString &tag)
{
    const int id = recipient->getTag(tag).toInt() + 1;
    recipient->setTag(tag, id);
    return id;
}

SkillInstanceRef sourceRef(const QVariantMap &receipt)
{
    return SkillInstanceRef(receipt.value("owner").toString(),
        SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
}

QVariantMap create(Room *room, ServerPlayer *recipient, const QString &name, const SkillContext &ctx, int amount)
{
    return QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
        {"instance", ctx.sourceRef.key.instanceID}, {"actor", recipient->objectName()}, {"amount", amount},
        {"dispatch", nextDispatch(room, recipient, name)}};
}

// Adds to the recipient's receipt from the same source, or appends a new one.
QVariantMap merge(QVariantList &receipts, Room *room, ServerPlayer *recipient, const QString &name,
                  const SkillContext &ctx, int amount)
{
    for (QVariant &entry : receipts) {
        QVariantMap receipt = entry.toMap();
        if (sourceRef(receipt) != ctx.sourceRef || receipt.value("actor").toString() != recipient->objectName()) continue;
        receipt.insert("amount", receipt.value("amount").toInt() + amount);
        entry = receipt;
        return receipt;
    }
    receipts << create(room, recipient, name, ctx, amount);
    return receipts.last().toMap();
}

QVariantList forPlayer(const Player *recipient, const QString &name)
{
    return recipient->property(playerProperty(name).constData()).toList();
}

void addToPlayer(Room *room, ServerPlayer *recipient, const QString &name, const SkillContext &ctx, int amount)
{
    if (!recipient || !ctx.sourceRef.isValid() || amount <= 0) return;
    const QByteArray property = playerProperty(name);
    QVariantList receipts = recipient->property(property.constData()).toList();
    const QVariantMap receipt = merge(receipts, room, recipient, name, ctx, amount);
    room->setPlayerProperty(recipient, property.constData(), receipts);
    syncPlayerReceiptState(room, receipt, false);
}

void clearPlayer(Room *room, ServerPlayer *recipient, const QString &name)
{
    const QByteArray property = playerProperty(name);
    for (const QVariant &entry : recipient->property(property.constData()).toList())
        syncPlayerReceiptState(room, entry.toMap(), true);
    room->setPlayerProperty(recipient, property.constData(), QVariantList());
}

// Per-use receipts live on the card, keyed by the enclosing use so nested uses of one card stay apart.
QString useKey(Room *room, const QString &name)
{
    const qint64 id = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
    return id > 0 ? name + ":" + QString::number(id) : QString();
}

void clearUse(Room *room, const QVariant &data, const QString &name)
{
    const CardUseStruct use = data.value<CardUseStruct>();
    const QString key = useKey(room, name);
    if (use.card && !key.isEmpty()) use.card->removeTag(key);
}

SkillContext toTriggerContext(const QString &definition, TriggerEvent event, ServerPlayer *player,
                              QVariant &data, const QVariantMap &receipt)
{
    SkillContext ctx;
    ctx.skill_name = definition;
    ctx.owner = ctx.invoker = ctx.initiator = player;
    ctx.sourceRef = sourceRef(receipt);
    // This is a previously applied effect, not a new activation of a removed skill.
    ctx.instanceID = receipt.value("dispatch").toInt();
    ctx.original_data = &data;
    ctx.current_event = event;
    ctx.setModifiedAmount(receipt.value("amount").toInt());
    return ctx;
}

bool containsForPlayer(const SkillContext &ctx, const QString &name)
{
    if (!ctx.owner || !ctx.owner->isAlive()) return false;
    for (const QVariant &entry : forPlayer(ctx.owner, name)) {
        const QVariantMap receipt = entry.toMap();
        if (sourceRef(receipt) == ctx.sourceRef && receipt.value("actor").toString() == ctx.owner->objectName()
            && receipt.value("amount").toInt() > 0) return true;
    }
    return false;
}

void collectForPlayer(const QString &definition, const QString &name, TriggerEvent event,
                      ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts)
{
    if (!player || !player->isAlive()) return;
    for (const QVariant &entry : forPlayer(player, name)) {
        const QVariantMap receipt = entry.toMap();
        const SkillInstanceRef source = sourceRef(receipt);
        if (!source.isValid() || receipt.value("actor").toString() != player->objectName()
            || receipt.value("amount").toInt() <= 0) continue;
        contexts << toTriggerContext(definition, event, player, data, receipt);
    }
}

}
