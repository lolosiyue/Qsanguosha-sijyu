#ifndef PLAYER_ARMOR_EFFECT_DOMAIN_H
#define PLAYER_ARMOR_EFFECT_DOMAIN_H
#include <QString>
// Public definition-domain observation only. Never calls a player rule or
// initializes/mutates the source's lazily cached armor-name list.
bool isPlayerArmorEffectNamePossible(const QString &name);
#endif
