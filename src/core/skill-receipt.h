#ifndef SKILL_RECEIPT_H
#define SKILL_RECEIPT_H

#include "skill.h"

namespace SkillReceipt {

// The two overloads keep the existing player property and player tag counters.
int nextDispatch(Room *room, ServerPlayer *recipient, const QString &name);
int nextDispatch(ServerPlayer *recipient, const QString &tag);

SkillInstanceRef sourceRef(const QVariantMap &receipt);
QVariantMap create(Room *room, ServerPlayer *recipient, const QString &name, const SkillContext &ctx, int amount);
QVariantMap merge(QVariantList &receipts, Room *room, ServerPlayer *recipient, const QString &name,
                  const SkillContext &ctx, int amount);

QVariantList forPlayer(const Player *recipient, const QString &name);
void addToPlayer(Room *room, ServerPlayer *recipient, const QString &name, const SkillContext &ctx, int amount);
void clearPlayer(Room *room, ServerPlayer *recipient, const QString &name);

QString useKey(Room *room, const QString &name);
void clearUse(Room *room, const QVariant &data, const QString &name);

SkillContext toTriggerContext(const QString &definition, TriggerEvent event, ServerPlayer *player,
                               QVariant &data, const QVariantMap &receipt);
bool containsForPlayer(const SkillContext &ctx, const QString &name);
void collectForPlayer(const QString &definition, const QString &name, TriggerEvent event,
                      ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts);

}

#endif
