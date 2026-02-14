#ifndef Tenyear_H
#define Tenyear_H

//#include "standard.h"
#include "ol.h"

class ZhizheCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhizheCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearXdPackage : public Package
{
    Q_OBJECT

public:
    TenyearXdPackage();
};

class ZhukouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhukouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class PingxiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE PingxiangCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ShouliCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShouliCard();
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
    void addMark(ServerPlayer *player1, ServerPlayer *player2) const;
};

class ShencaiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShencaiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class TuoyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TuoyuCard();
    void onUse(Room *, CardUseStruct &) const;
};

class FenyueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FenyueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class LijiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LijiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class ZhafuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZhafuCard();
    void onEffect(CardEffectStruct &effect) const;
};

class TianjiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TianjiangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ZhurenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhurenCard();
    void ZhurenGetSlash(ServerPlayer *source) const;
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class XiangmianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiangmianCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearLianjiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TenyearLianjiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class DaoshuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE DaoshuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class SushouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SushouCard();
    void onUse(Room *, CardUseStruct &) const;
};

class JianjiYHCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JianjiYHCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearAocaiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearAocaiCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class TenyearDuwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearDuwuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearSongciCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearSongciCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class KaijiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE KaijiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JingzaoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JingzaoCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
    void getCards(ServerPlayer *player, QList<int> card_ids) const;
};

class TenyearZhaohanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearZhaohanCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class TenyearJueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearJueCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class TenyearZhongjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearZhongjianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class AnzhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE AnzhiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearLingyinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearLingyinCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class CaizhuangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CaizhuangCard();

    int getSuitsNum(ServerPlayer *source) const;
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class QianlongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QianlongCard();
    void onUse(Room *, CardUseStruct &) const;
};

class TenyearNewShichouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearNewShichouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const;
};

class ZunweiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZunweiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class BazhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BazhanCard(QString bazhan = "bazhan");
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void BazhanEffect(ServerPlayer *from, ServerPlayer *to) const;
    void onEffect(CardEffectStruct &effect) const;
private:
    QString bazhan;
};

class SecondBazhanCard : public BazhanCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondBazhanCard();
};

class XingzuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XingzuoCard();
    void onUse(Room *, CardUseStruct &) const;
};

class MiaoxianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MiaoxianCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *source) const;
};

class YuqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YuqiCard();
    void onUse(Room *, CardUseStruct &) const;
};

class JiqiaosyCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiqiaosyCard();
    void onUse(Room *, CardUseStruct &) const;
};

class BaoshuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BaoshuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class XiaowuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiaowuCard();
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ShawuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShawuCard();
    void use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const;
};

class TenyearGusheCard : public GusheCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearGusheCard();
};

class FengyingCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE FengyingCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class JijiaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JijiaoCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TanbeiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TanbeiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class SidaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SidaoCard();
    void onUse(Room *, CardUseStruct &) const;
};

class HeqiaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HeqiaCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class HeqiaUseCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HeqiaUseCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class TuoxianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TuoxianCard();
    void onUse(Room *room, CardUseStruct &use) const;
};

class LibangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LibangCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class CuichuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CuichuanCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearBingjiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearBingjiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ShilieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShilieCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ShilieGetCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShilieGetCard();
    void onUse(Room *, CardUseStruct &) const;
};

class YijiaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YijiaoCard();
    void onEffect(CardEffectStruct &effect) const;
};

class SpCuoruiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SpCuoruiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class SecondSpCuoruiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondSpCuoruiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class SpMouzhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SpMouzhuCard();
    bool targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JijieCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JijieCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class SongshuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SongshuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class SpKuizhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SpKuizhuCard();
    void onUse(Room *, CardUseStruct &) const;
};

class SpQianxinCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SpQianxinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class FuhaiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE FuhaiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ChanniCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ChanniCard();
    void onEffect(CardEffectStruct &effect) const;
};

class XiongmangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiongmangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class TenyearHuoshuiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TenyearHuoshuiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearQingchengCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TenyearQingchengCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class XuezhaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XuezhaoCard(const QString &xuezhao = "xuezhao");
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
private:
    QString xuezhao;
};

class SecondXuezhaoCard : public XuezhaoCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondXuezhaoCard();
};

class MinsiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MinsiCard();
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JijingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JijingCard();
    void onUse(Room *, CardUseStruct &) const;
};

class CixiaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CixiaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *, ServerPlayer *, QList<ServerPlayer *> &targets) const;
};

class JieyinghCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JieyinghCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class PingjianDialog : public QDialog
{
    Q_OBJECT

public:
    static PingjianDialog *getInstance();

public slots:
    void popup();
    void selectSkill(QAbstractButton *button);

private:
    explicit PingjianDialog();

    QAbstractButton *createSkillButton(const QString &skill_name);
    QButtonGroup *group;
    QVBoxLayout *button_layout;

signals:
    void onButtonClick();
};

class PingjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE PingjianCard();
    //bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class SecondYujueCard : public YujueCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondYujueCard();
};

class WeiwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WeiwuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class TenyearHcPackage : public Package
{
    Q_OBJECT

public:
    TenyearHcPackage();
};

class AnliaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE AnliaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class KanjiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE KanjiCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TenyearGueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearGueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class TenyearQuanjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearQuanjianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BoyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BoyanCard();
    void onEffect(CardEffectStruct &effect) const;
};

class JianliangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JianliangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class WeimengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WeimengCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearFenglveCard :public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearFenglveCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearFenglveGiveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearFenglveGiveCard();
    void onUse(Room *, CardUseStruct &) const;
};

class QiangzhiZHCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiangzhiZHCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class XunliPutCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XunliPutCard();
    void onUse(Room *, CardUseStruct &) const;
};

class XunliCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XunliCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ZhishiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhishiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class LieyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LieyiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class MiduCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MiduCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearJiezhenCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TenyearJiezhenCard();
    void onEffect(CardEffectStruct &effect) const;
};

class DunshiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DunshiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class YongbiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YongbiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearShuheCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TenyearShuheCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class YingruiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YingruiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class GuowuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GuowuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class ZhuningCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhuningCard();
    void onEffect(CardEffectStruct &effect) const;
};

class CuijianCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE CuijianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class SecondCuijianCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondCuijianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class FupingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FupingCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *source) const;
};

class WeilieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WeilieCard();
    void onUse(Room *room, CardUseStruct &use) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class YuanyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YuanyuCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ChenjianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ChenjianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class JinhuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinhuiCard();
    void usecard(Room *room, ServerPlayer *source, ServerPlayer *target, const Card *card) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JinhuiUseCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinhuiUseCard();
    void onUse(Room *, CardUseStruct &) const;
};

class QingtanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QingtanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class XunjiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XunjiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class FengyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FengyanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class LiushiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LiushiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearKuangfuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearKuangfuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class BusuanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE BusuanCard();
    void onEffect(CardEffectStruct &effect) const;
};

class ShanxiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ShanxiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class LvxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LvxinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class HuandaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HuandaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ShuaijieCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ShuaijieCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class QianzhengCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE QianzhengCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class FuxieCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE FuxieCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ShouxingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShouxingCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
};

class JiangxianCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JiangxianCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class PingzhiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE PingzhiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class PingluCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE PingluCard();
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ThZhenguiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ThZhenguiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class YanzuoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE YanzuoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearXhPackage : public Package
{
    Q_OBJECT

public:
    TenyearXhPackage();
};

class ZhengyueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhengyueCard();
    void onUse(Room *room, CardUseStruct &use) const;
};

class SayingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SayingCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *source) const;
};

class JiaohaoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JiaohaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class YinluCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE YinluCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self,int &m) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JichunCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JichunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class WanchanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE WanchanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class QuzhouCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE QuzhouCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearMouPackage : public Package
{
    Q_OBJECT

public:
    TenyearMouPackage();
};

class ZhongyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhongyanCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ThShuliangCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ThShuliangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JiusiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiusiCard();
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class ShimouCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ShimouCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ThWuyanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ThWuyanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class DouweiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE DouweiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class XianjuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE XianjuCard();
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ThKegouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ThKegouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class DixianCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE DixianCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TenyearWeiPackage : public Package
{
    Q_OBJECT

public:
    TenyearWeiPackage();
};

class WohengCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE WohengCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ZhanpanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZhanpanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class LingseCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE LingseCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ManhouCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ManhouCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TanluanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TanluanCard();
    void onUse(Room *room, CardUseStruct &use) const;
};

class WeitiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE WeitiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class GuilinCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuilinCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ThMuzhenCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ThMuzhenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ChuanyuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ChuanyuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class GengduCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GengduCard();
    void onUse(Room *room, CardUseStruct &use) const;
};

class JikunCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JikunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JieshiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JieshiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class BizuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE BizuCard();
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &tos) const;
};

class JilieCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JilieCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ShugangCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ShugangCard();
    void onUse(Room *room, CardUseStruct &use) const;
};

class DianlunCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE DianlunCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ThJiweiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ThJiweiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class QiaoduiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE QiaoduiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JunheCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JunheCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class QuxianCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE QuxianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ZouyiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZouyiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ZhanyanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZhanyanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class FuhuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FuhuiCard();
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class MohuaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MohuaCard();
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class ZongtaoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZongtaoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ChouxiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ChouxiCard();
    void onUse(Room *room, CardUseStruct &use) const;
};

class JichaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JichaoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ShurenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShurenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ShouhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShouhuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class ZijueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZijueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JianliCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JianliCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HangzhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HangzhuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ZhenyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhenyuCard();
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class JieluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JieluCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};





#endif