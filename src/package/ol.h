#ifndef OL_PACKAGE_H
#define OL_PACKAGE_H

#include "sp.h"
#include "wind.h"

class OLMumuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLMumuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLMumu2Card : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLMumu2Card();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLRendeCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLRendeCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class OLFenxunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLFenxunCard();
    void onEffect(CardEffectStruct &effect) const;
};

class OLSpPackage : public Package
{
    Q_OBJECT

public:
    OLSpPackage();
};

class JiqiaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiqiaoCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class GusheCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GusheCard(QString skill_name = "gushe");
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    int pindian(ServerPlayer *from, ServerPlayer *target, const Card *card1, const Card *card2) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
private:
    QString skill_name;
};

class GuolunCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuolunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class MubingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MubingCard();
    void onUse(Room *, CardUseStruct &) const;
};

class ZiquCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZiquCard();
    void onUse(Room *, CardUseStruct &) const;
};

class ManwangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ManwangCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class JuesiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JuesiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JianshuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JianshuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class LizhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LizhanCard();
    bool targetFilter(const QList<const Player *> &, const Player *to_select, const Player *) const;
    void use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const;
};

class WeikuiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WeikuiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class LihunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LihunCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};










class OLQifuPackage : public Package
{
    Q_OBJECT

public:
    OLQifuPackage();
};

class OLCcxhPackage : public Package
{
    Q_OBJECT

public:
    OLCcxhPackage();
};

class LiluCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LiluCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class YujueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YujueCard(QString zhihu = "zhihu");
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
private:
    QString zhihu;
};

class QujiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QujiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class DingpanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE DingpanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class DenglouCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE DenglouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class ZengdaoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZengdaoCard();
    void onEffect(CardEffectStruct &effect) const;
};

class ZengdaoRemoveCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZengdaoRemoveCard();
    void onUse(Room *room, CardUseStruct &) const;
};

class FenglveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FenglveCard();
    void onUse(Room *, CardUseStruct &) const;
};

class MoushiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MoushiCard();
    void onEffect(CardEffectStruct &effect) const;
};


class QuxiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QuxiCard();
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YinjuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE YinjuCard();
    void onEffect(CardEffectStruct &effect) const;
};

class JinChongxinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinChongxinCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ZaowangCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZaowangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JinJianheCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinJianheCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onEffect(CardEffectStruct &effect) const;
};

class ShefuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShefuCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ShefuDialog : public GuhuoDialog
{
    Q_OBJECT

public:
    static ShefuDialog *getInstance(const QString &object);

protected:
    explicit ShefuDialog(const QString &object);
    bool isButtonEnabled(const QString &button_name) const;
};

class BifaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BifaCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class SongciCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SongciCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLLianjiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE OLLianjiCard();
    void onEffect(CardEffectStruct &effect) const;
};



class JuguanDialog : public QDialog
{
    Q_OBJECT

public:
    static JuguanDialog *getInstance(const QString &object, const QString &card_names);

public slots:
    void popup();
    void selectCard(QAbstractButton *button);

private:
    explicit JuguanDialog(const QString &object, const QString &card_names);

    virtual bool isButtonEnabled(const QString &button_name) const;
    QAbstractButton *createButton(const Card *card);
    QHash<QString, const Card *> map;
    QButtonGroup *group;
    QVBoxLayout *button_layout;
    QString cards;

signals:
    void onButtonClick();
};

class JuguanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JuguanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class OLLuanzhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLLuanzhanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class SecondMansiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SecondMansiCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class BushiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BushiCard();
    void onUse(Room *room, CardUseStruct &) const;
};

class MidaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MidaoCard();

    void onUse(Room *, CardUseStruct &) const;
};

class SpCanshiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE SpCanshiCard();
    bool targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class ShoufuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShoufuCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ShoufuPutCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShoufuPutCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void onUse(Room *, CardUseStruct &card_use) const;
};

class GuanxuCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuanxuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class GuanxuChooseCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuanxuChooseCard();
    void onUse(Room *, CardUseStruct &) const;
};

class GuanxuDiscardCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE GuanxuDiscardCard();
    void onUse(Room *, CardUseStruct &) const;
};

class NewZhoufuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NewZhoufuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *, ServerPlayer *, QList<ServerPlayer *> &targets) const;
};

class TenyearZhoufuCard : public NewZhoufuCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearZhoufuCard();
};

class JianjieCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JianjieCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JianjieHuojiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JianjieHuojiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class JianjieLianhuanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JianjieLianhuanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class JianjieYeyanCard : public SkillCard
{
    Q_OBJECT

public:
    void damage(ServerPlayer *shenzhouyu, ServerPlayer *target, int point) const;
};

class GreatJianjieYeyanCard : public JianjieYeyanCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GreatJianjieYeyanCard();

    bool targetFilter(const QList<const Player *> &targets,const Player *to_select, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select,const Player *Self, int &maxVotes) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class SmallJianjieYeyanCard : public JianjieYeyanCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SmallJianjieYeyanCard();
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    void onEffect(CardEffectStruct &effect) const;
};

class YinbingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YinbingCard();

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class DuanfaCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE DuanfaCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};


class LiehouCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE LiehouCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class XiaosiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiaosiCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class AocaiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE AocaiCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class DuwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DuwuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class YuanhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YuanhuCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void onEffect(CardEffectStruct &effect) const;
};

class JianjiCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE JianjiCard();
    void onEffect(CardEffectStruct &effect) const;
};

class ZiyuanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZiyuanCard();
    void onEffect(CardEffectStruct &effect) const;
};

class XingguCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XingguCard();

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class NeifaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE NeifaCard();
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YoulongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YoulongCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *source) const;
};

class YoulongDialog : public GuhuoDialog
{
    Q_OBJECT

public:
    static YoulongDialog *getInstance(const QString &object);

protected:
    explicit YoulongDialog(const QString &object);
    bool isButtonEnabled(const QString &button_name) const;
};

class JinzhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinzhiCard(QString skill_name = "jinzhi");
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
    const Card *validateInResponse(ServerPlayer *source) const;
private:
    QString skill_name;
};

class SecondJinzhiCard : public JinzhiCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SecondJinzhiCard();
};

class XionghuoCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE XionghuoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onEffect(CardEffectStruct &effect) const;
};

class OLZhennanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE OLZhennanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class OLXingwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLXingwuCard();
    void onEffect(CardEffectStruct &effect) const;
};

class TenyearXingwuCard : public OLXingwuCard
{
    Q_OBJECT

public:
    Q_INVOKABLE TenyearXingwuCard();
};

class ZhouxuanzCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE ZhouxuanzCard();
    void onUse(Room *, CardUseStruct &) const;
};



class OLMouPackage : public Package
{
    Q_OBJECT

public:
    OLMouPackage();
};

class GodPackage : public Package
{
    Q_OBJECT

public:
    GodPackage();
};

class WeimianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WeimianCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class KouchaoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE KouchaoCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class HunjiangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HunjiangCard();
    bool targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JinlanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JinlanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class ShengongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShengongCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class XufaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XufaCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class HongtuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HongtuCard();
    bool targetFilter(const QList<const Player *> &tos, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &use) const;
};

class FushiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FushiCard();
    bool targetFilter(const QList<const Player *> &tos, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &card_use) const;
};

class ZuolianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZuolianCard();
    bool targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class WeifuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WeifuCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class olYichengCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE olYichengCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const;
};

class olYicheng2Card : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE olYicheng2Card();
    void onUse(Room *room, CardUseStruct &use) const;
};

class ChanshuangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ChanshuangCard();
    bool targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class Xuanzhu2Card : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE Xuanzhu2Card();
    void onUse(Room *room, CardUseStruct &use) const;
};

class XuanzhuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XuanzhuCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class QushiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QushiCard();
    bool targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class WeijieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WeijieCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class ChenglieCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ChenglieCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class PingduanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE PingduanCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class YanliangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YanliangCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class RenxianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE RenxianCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class BojueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE BojueCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class FengshangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE FengshangCard();
    bool targetFixed() const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class ShuziCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ShuziCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JiguCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiguCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class JiewanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiewanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class XianyingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XianyingCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class OLLiyongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLLiyongCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JingxianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JingxianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class XiayongCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XiayongCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class WenrenCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE WenrenCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class SiqiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SiqiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class QiaozhiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiaozhiCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ZonghuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZonghuCard();
    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class DeruCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DeruCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class JiaweiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE JiaweiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class LunzhanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LunzhanCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class OL2ShanjiaCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OL2ShanjiaCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class LucunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE LucunCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class DiciCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DiciCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class TunanCard : public SkillCard
{
    Q_OBJECT
public:
    Q_INVOKABLE TunanCard();
    void onEffect(CardEffectStruct &effect) const;
};

class SibingCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SibingCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class SuyiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE SuyiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class XieweiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE XieweiCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class YouqueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YouqueCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class OLDemonPackage : public Package
{
    Q_OBJECT

public:
    OLDemonPackage();
};

class KuanmoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE KuanmoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class GangqianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE GangqianCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class HuanhuoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HuanhuoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class OLQingshiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLQingshiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MiluoCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MiluoCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class OLJueyanCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLJueyanCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class OLShuliangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLShuliangCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void onUse(Room *room, CardUseStruct &card_use) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class ZhitianCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhitianCard();
    void onUse(Room *room, CardUseStruct &card_use) const;
};

class ZhijueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE ZhijueCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class YinglveCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE YinglveCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class MengshiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE MengshiCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class HuanpeiCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE HuanpeiCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class DinglunCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE DinglunCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class OLJinjinCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE OLJinjinCard();

    bool targetFixed() const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;

    const Card *validateInResponse(ServerPlayer *user) const;
    const Card *validate(CardUseStruct &cardUse) const;
};

class QiangangCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiangangCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class QiwuCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE QiwuCard();
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
};

class CuijueCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CuijueCard();
    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
};

class QiexieWeapon : public Weapon
{
    Q_OBJECT
public:
    Q_INVOKABLE QiexieWeapon(Card::Suit suit = Card::NoSuit, int number = 0);
    void onInstall(ServerPlayer *player) const;
    void onUninstall(ServerPlayer *player) const;
};




#endif