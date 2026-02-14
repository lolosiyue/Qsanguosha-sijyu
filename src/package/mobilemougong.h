#ifndef MOBILEMOUZHI_H
#define MOBILEMOUZHI_H

#include "thicket.h"

class MobileMouZhiPackage : public Package
{
    Q_OBJECT

public:
    MobileMouZhiPackage();
};

class MobileMouZhihengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouZhihengCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};


class MobileMouShiPackage : public Package
{
    Q_OBJECT

public:
    MobileMouShiPackage();
};

class MobileMouDuanliangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouDuanliangCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMouShipoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouShipoCard();
    void onUse(Room *room, CardUseStruct &use) const;
};


class MobileMouYuPackage : public Package
{
    Q_OBJECT

public:
    MobileMouYuPackage();
};

class MobileMouKejiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouKejiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};


class MobileMouNengPackage : public Package
{
    Q_OBJECT

public:
    MobileMouNengPackage();
};

class MobileMouYangweiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouYangweiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class MobileMouTongPackage : public Package
{
    Q_OBJECT

public:
    MobileMouTongPackage();
};

class MobileMouGangLieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouGangLieCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMouLeijiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouLeijiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMouXingshangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouXingshangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMouFangzhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouFangzhuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMouSongweiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouSongweiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMouJiefanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouJiefanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMouXianzhenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouXianzhenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileMouJingceCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouJingceCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
};

class MobileMouTiaoxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouTiaoxinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileMouLuanwuCard : public LuanwuCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouLuanwuCard();
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileMouQuhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouQuhuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class MobileMouShensuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileMouShensuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;

    const Card *validate(CardUseStruct &use) const;
};





#endif