#include "engine-bootstrap.h"
#include "card.h"
#include "engine.h"
#include "player.h"
#include "skill.h"
#include "standard.h"

#include <QCoreApplication>
#include <QDebug>

// 迴歸測例：`target:playerName` 是 EquipsNullified 專屬的來源玩家條件，
// 不是通用 CardLimitation 的第五欄。ExpPattern 只解析四欄（卡名／花色／
// 點數／區域），所以帶 target 的 pattern 必須由 isEquipsNullified() 自己
// 解析，否則它會退化成「對所有人無效」。
class EquipsNullifiedProbePlayer : public Player
{
public:
    EquipsNullifiedProbePlayer()
        : Player(nullptr)
    {
    }

    int aliveCount(bool = false) const override { return 1; }
    QString getGameMode() const override { return QStringLiteral("test"); }
    Player *getNextAlive(int = 1) const override { return const_cast<EquipsNullifiedProbePlayer *>(this); }
    Player *getLastAlive(int = 1) const override { return const_cast<EquipsNullifiedProbePlayer *>(this); }
};

// hasArmorEffect() 的「依名查防具」分支需要一件防具，而 test player 沒有裝備區；
// 用 ViewAsEquipSkill 造一件虛擬防具，讓該分支不需要真實牌堆。
class EquipsNullifiedViewAsArmorSkill : public ViewAsEquipSkill
{
public:
    EquipsNullifiedViewAsArmorSkill()
        : ViewAsEquipSkill(QStringLiteral("test-equips-nullified-armor"))
    {
    }

    QString viewAsEquip(const Player *) const override
    {
        return QStringLiteral("eight_diagram");
    }
};

int runEquipsNullifiedTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    Card *armor = Sanguosha->cloneCard(QStringLiteral("eight_diagram"));
    if (armor == nullptr) {
        qCritical() << "could not clone eight_diagram";
        return 2;
    }
    armor->setId(7);

    EquipsNullifiedProbePlayer owner;
    owner.setObjectName(QStringLiteral("owner"));
    owner.setAlive(true);
    EquipsNullifiedProbePlayer target;
    target.setObjectName(QStringLiteral("target"));
    target.setAlive(true);
    EquipsNullifiedProbePlayer other;
    other.setObjectName(QStringLiteral("other"));
    other.setAlive(true);

    EquipsNullifiedViewAsArmorSkill viewAsArmor;
    Sanguosha->addSkills(QList<const Skill *>() << &viewAsArmor);
    owner.Player::addSkill(viewAsArmor.objectName());

    if (owner.isEquipsNullified(armor, &target)) {
        qCritical() << "fresh player already reports nullified equipment";
        return 3;
    }

    owner.addEquipsNullified(QStringLiteral("Armor|.|.|.|target:target"),
        QStringLiteral("equips-nullified-test"), true);

    if (!owner.isEquipsNullified(armor, &target)) {
        qCritical() << "target: pattern did not nullify the armor for the named source";
        return 4;
    }
    if (owner.isEquipsNullified(armor, &other)) {
        qCritical() << "target: pattern leaked to an unrelated source player";
        return 5;
    }
    if (owner.isEquipsNullified(armor, nullptr)) {
        qCritical() << "target: pattern nullified the armor without any source context";
        return 6;
    }
    if (!owner.hasArmorEffect(QStringLiteral("eight_diagram"), &other)) {
        qCritical() << "hasArmorEffect lost the armor for an unrelated source";
        return 7;
    }
    if (owner.hasArmorEffect(QStringLiteral("eight_diagram"), &target)) {
        qCritical() << "hasArmorEffect kept the armor for the nullified source";
        return 8;
    }
    if (!owner.hasArmorEffect(QStringLiteral("eight_diagram"))) {
        qCritical() << "hasArmorEffect without a source must ignore target: rules";
        return 9;
    }

    owner.removeEquipsNullified(QStringLiteral("Armor|.|.|.|target:target"),
        QStringLiteral("equips-nullified-test"), true);
    if (owner.isEquipsNullified(armor, &target)) {
        qCritical() << "removeEquipsNullified left the target: rule in place";
        return 10;
    }

    // 只寫 base class 名的 pattern 必須被補成完整四欄；舊碼補成
    // `Armor|.|.|$1`（第四欄是空字串而非 `.`），matchExpPattern 一律回 false，
    // 令 QinggangTag、extensions/sijyu.lua 那批限制靜靜失效。
    owner.addEquipsNullified(QStringLiteral("Armor"),
        QStringLiteral("equips-nullified-test"), true);
    if (!owner.isEquipsNullified(armor, nullptr)) {
        qCritical() << "base-class pattern 'Armor' did not nullify the armor";
        return 11;
    }
    if (owner.hasArmorEffect(QStringLiteral("eight_diagram"))) {
        qCritical() << "hasArmorEffect kept a globally nullified armor";
        return 12;
    }
    owner.removeEquipsNullified(QStringLiteral("Armor"),
        QStringLiteral("equips-nullified-test"), true);
    if (owner.isEquipsNullified(armor, nullptr)) {
        qCritical() << "removeEquipsNullified left the base-class rule in place";
        return 13;
    }

    // 三欄 pattern（例如 2026 鐵騎那類 `Armor|red`）同樣要補成四欄。
    // 用紅色防具測，否則花色欄比對不到。
    Card *redArmor = Sanguosha->cloneCard(QStringLiteral("eight_diagram"), Card::Heart, 7);
    if (redArmor == nullptr) {
        qCritical() << "could not clone a red eight_diagram";
        return 16;
    }
    redArmor->setId(8);
    owner.addEquipsNullified(QStringLiteral("Armor|red"),
        QStringLiteral("equips-nullified-test"), true);
    if (!owner.isEquipsNullified(redArmor, nullptr)) {
        qCritical() << "two-field pattern 'Armor|red' did not nullify a red armor";
        return 14;
    }
    owner.removeEquipsNullified(QStringLiteral("Armor|red"),
        QStringLiteral("equips-nullified-test"), true);
    if (owner.isEquipsNullified(redArmor, nullptr)) {
        qCritical() << "removeEquipsNullified left the two-field rule in place";
        return 15;
    }

    qInfo() << "equips-nullified target: regression passed";
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    return runEquipsNullifiedTests();
}
