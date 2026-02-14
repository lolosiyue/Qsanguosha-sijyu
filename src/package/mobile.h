#ifndef mobile_PACKAGE_H
#define mobile_PACKAGE_H

#include "package.h"
#include "standard-cards.h"

class mobilePackage : public Package
{
    Q_OBJECT

public:
    mobilePackage();
};

class ShanjiaCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ShanjiaCard(QString shanjia = "shanjia");
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
private:
    QString shanjia;
};

class OLShanjiaCard : public ShanjiaCard
{
    Q_OBJECT
public:
    Q_INVOKABLE OLShanjiaCard();
};

class PingcaiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE PingcaiCard();
    bool isOK(Room *room, const QString &name) const;
    bool shuijingJudge(Room *room) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class BaiyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BaiyiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JinglveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinglveCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class mobileStarPackage : public Package
{
    Q_OBJECT

public:
    mobileStarPackage();
};

class XingZhilveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XingZhilveCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class XingZhilveSlashCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XingZhilveSlashCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class XingZhiyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XingZhiyanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class XingJinfanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XingJinfanCard();
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class mobileSpPackage : public Package
{
    Q_OBJECT

public:
    mobileSpPackage();
};

class ZhoufuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhoufuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class XuejiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XuejiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileShanxiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileShanxiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class LuanzhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LuanzhanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class QiangwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiangwuCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class FumanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE FumanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileFuhaiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileFuhaiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JixuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JixuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class GongsunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GongsunCard();
    void onEffect(CardEffectStruct &effect) const;
};

class YingshiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE YingshiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class QinguoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE QinguoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class KannanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE KannanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class LimuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LimuCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class MobileLiezhiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileLiezhiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class FangtongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FangtongCard();
    void onUse(Room *, CardUseStruct &) const;
};

class MobileXushenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileXushenCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class MobileSpQianxinCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileSpQianxinCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class TongquCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TongquCard();
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class GaoyuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GaoyuanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TiansuanDialog : public QDialog
{
    Q_OBJECT

public:
    static TiansuanDialog *getInstance(const QString &name, const QString &choices = "");

public slots:
    void popup();
    void selectChoice(QAbstractButton *button);

private:
    explicit TiansuanDialog(const QString &name, const QString &choices = "");

    QAbstractButton *createChoiceButton(const QString &choice);
    bool MarkJudge(const QString &choice);
    QButtonGroup *group;
    QVBoxLayout *button_layout;
    QString tiansuan_choices;

signals:
    void onButtonClick();
};

class TiansuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TiansuanCard();
    bool targetsFeasible(const QList<const Player *> &, const Player *) const;
    bool targetFilter(const QList<const Player *> &, const Player *, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class BeizhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BeizhuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class DaojiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE DaojiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ZhouxuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhouxuanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class WuyuanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE WuyuanCard(const QString &wuyuan = "wuyuan");
    void onEffect(CardEffectStruct &effect) const;
private:
    QString wuyuan;
};

class TenyearWuyuanCard : public WuyuanCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearWuyuanCard();
};

class NewShuliangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NewShuliangCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class YizanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE YizanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class HongyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HongyiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class SecondHongyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondHongyiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class SpZhaoxinCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SpZhaoxinCard();
    void use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class SpZhaoxinChooseCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SpZhaoxinChooseCard();
};

class SecondZhanyiViewAsBasicCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondZhanyiViewAsBasicCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class SecondZhanyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondZhanyiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YizhengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YizhengCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MobileLianjiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE MobileLianjiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class NewxuehenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NewxuehenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class WaishiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WaishiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JiaohuaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiaohuaCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ShiheCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShiheCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ZhoulinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhoulinCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class LuanqunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LuanqunCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class NaxueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NaxueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class XietuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XietuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ZuoyouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZuoyouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class QlQingzhengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QlQingzhengCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class QlFangzhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QlFangzhuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class QlJuejinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QlJuejinCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class XiongshiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiongshiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JiejianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiejianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class mobileXhPackage : public Package
{
    Q_OBJECT

public:
    mobileXhPackage();
};

class CsPicaiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CsPicaiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class CsYaozhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CsYaozhuoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class CsXiaoluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CsXiaoluCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class CsXiaolu2Card : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CsXiaolu2Card();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class CsKuijiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CsKuijiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class CsKuijiDisCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CsKuijiDisCard();
    void onUse(Room *, CardUseStruct &) const;
};

class CsNiquCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CsNiquCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class PoxiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE PoxiangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ZhujianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhujianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class DuansuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DuansuoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BuxuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BuxuCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class QuchongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QuchongCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class QinyingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QinyingCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BiweiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BiweiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class Xuanjian : public Weapon
{
    Q_OBJECT

public:
    Q_INVOKABLE Xuanjian(Card::Suit suit, int number);
};

class MobileJiyuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileJiyuCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MZengouCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MZengouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class mobileBsQiPackage : public Package
{
    Q_OBJECT

public:
    mobileBsQiPackage();
};

class mobileBsZhengPackage : public Package
{
    Q_OBJECT

public:
    mobileBsZhengPackage();
};

class mobileBsShiPackage : public Package
{
    Q_OBJECT

public:
    mobileBsShiPackage();
};

class mobileBsJiePackage : public Package
{
    Q_OBJECT

public:
    mobileBsJiePackage();
};

class BsHanzhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BsHanzhanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ZhenfengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhenfengCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class DaozhuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DaozhuanCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class FujiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FujiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class LvemingCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE LvemingCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class TunjunCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TunjunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    QList<int> removeList(QList<int> equips, QList<int> ids) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ZhuguoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhuguoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileJianjiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileJianjiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileKuangxiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileKuangxiangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class GanjueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GanjueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class XiezhengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiezhengCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class QiantunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiantunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MobileDaoshuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MobileDaoshuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class WeisiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WeisiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BsDimengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BsDimengCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class XiongtuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiongtuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JingtuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JingtuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self, int &m) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JiebianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiebianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BsRunweiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BsRunweiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class GanggengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GanggengCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JiejieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiejieCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class ZhengshuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhengshuoCard();
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BsTuntianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BsTuntianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BsQingyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BsQingyanCard();
    const Card *validate(CardUseStruct &cardUse) const;
    const Card *validateInResponse(ServerPlayer *user) const;
};

class CeduanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CeduanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YueyuanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YueyuanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class WeizhuangCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE WeizhuangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetFixed() const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &use) const;
    const Card *validateInResponse(ServerPlayer *player) const;
};

class ChongchaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ChongchaCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class DieyinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DieyinCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JinchuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinchuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};






#endif