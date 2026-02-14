#ifndef MAOTU_H
#define MAOTU_H

//#include "package.h"
#include "standard.h"

class MaotuPackage : public Package
{
    Q_OBJECT

public:
    MaotuPackage();
};

class MTJieliCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MTJieliCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &) const;
};

class MTWeiqieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MTWeiqieCard();

    void onUse(Room *, CardUseStruct &) const;
};

class MTZhilieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MTZhilieCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MTRenyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MTRenyiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MTGuzhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MTGuzhaoCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    int pindian(ServerPlayer *from, ServerPlayer *target, const Card *card1, const Card *card2) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MTJiyeCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MTJiyeCard();

    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class MTZhiheCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MTZhiheCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

#endif
