#ifndef _STANDARD_SKILLCARDS_H
#define _STANDARD_SKILLCARDS_H

#include "skill.h"
#include "card.h"

// Guanxing and Yizhi share observation rules while retaining their own sources.
class Guanxing : public TriggerSkillV2 {
public:
    explicit Guanxing(const QString &name = "guanxing");
    bool canPreshow() const override;
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override;
    void record(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override;
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override;
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override;
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override;
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override;
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *) const override;
};

class StrengthenPackage : public Package
{
    Q_OBJECT

public:
    StrengthenPackage();
};

class TestPackage : public Package
{
    Q_OBJECT

public:
    TestPackage();
};

class NosTuxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NosTuxiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};
class NosYiji : public TriggerSkillV2
{
public:
    NosYiji();
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override;
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *) const override;

protected:
    int n;
};
class NosRendeCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NosRendeCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

// Retained wire entry; the server rebuilds it through RendeViewAsSkill.
class RendeCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE RendeCard();
};

class NosKurouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NosKurouCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class NosFanjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NosFanjianCard();
    void onEffect(CardEffectStruct &effect) const;
};

class QingnangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QingnangCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class Hujia : public TriggerSkillV2
{
public:
    Hujia(const QString &hujia = "hujia");
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override;
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *) const override;

protected:
    QString hujia;
};

class ZhihengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhihengCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YijueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YijueCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JieyinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JieyinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TuxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TuxiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class FanjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FanjianCard();
    void onEffect(CardEffectStruct &effect) const;
};

class KurouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE KurouCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class LianyingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LianyingCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class LijianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LijianCard(bool cancelable = true);

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;

private:
    bool duel_cancelable;
};

class NosLijianCard : public LijianCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NosLijianCard();
};

class ChuliCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ChuliCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class LiuliCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LiuliCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class FenweiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FenweiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YijiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YijiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JianyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JianyanCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class GuoseCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GuoseCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JijiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JijiangCard(const QString &jijiang = "jijiang");

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
private:
    const QString jijiang;
};

class JijiangViewAsSkill : public ViewAsSkillV2
{
public:
    JijiangViewAsSkill();

    bool isEnabledAtPlay(const Player *player) const;
    bool isEnabledAtResponse(const Player *player, const QString &pattern) const;
    bool canActivate(const ActiveSkillRequest &request) const override;
    const Card *createCard(const ActiveSkillRequest &request) const override;
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }

private:
    static bool hasShuGenerals(const Player *player);
};

class MobileTongjiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileTongjiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void onEffect(CardEffectStruct &effect) const;
};









#endif
