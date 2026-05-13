#ifndef _MELEE_MODE_H
#define _MELEE_MODE_H

#include "standard.h"

class MeleeSlashJink : public Slash
{
    Q_OBJECT

public:
    Q_INVOKABLE MeleeSlashJink(Card::Suit suit, int number);
    QString getSubtype() const;
    bool isKindOf(const char *cardType) const;
    bool isAvailable(const Player *player) const;
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MeleeModePackage : public Package
{
    Q_OBJECT

public:
    MeleeModePackage();
};

#endif
