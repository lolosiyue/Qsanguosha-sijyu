#include "hegemony-mode.h"

#include "player.h"
#include "server-info.h"
#include "settings.h"

#include <QMap>

bool HegemonyMode::enabled()
{
    return Config.EnableHegemony;
}

bool HegemonyMode::enabledFor(const Player *player)
{
    return player && player->isClientPlayer() ? ServerInfo.EnableHegemony : Config.EnableHegemony;
}

bool HegemonyMode::enabledForCatalog(bool inRoom)
{
    return inRoom ? Config.EnableHegemony : ServerInfo.EnableHegemony;
}

QString HegemonyMode::mappedRole(const QString &kingdom)
{
    static const QMap<QString, QString> roles{
        {QStringLiteral("wei"), QStringLiteral("lord")},
        {QStringLiteral("shu"), QStringLiteral("loyalist")},
        {QStringLiteral("wu"), QStringLiteral("rebel")},
        {QStringLiteral("qun"), QStringLiteral("renegade")}};
    return roles.value(kingdom, kingdom == QLatin1String("god")
        ? QStringLiteral("careerist") : kingdom);
}

QString HegemonyMode::seemingKingdom(const Player *player)
{
    // Public faction identity must never inspect another player's hidden generals.
    if (player->getRole().startsWith(QStringLiteral("careerist_")) && player->hasShownRole())
        return player->getRole();
    if (!player->hasShownOneGeneral())
        return QString();
    return player->getRole() == QLatin1String("careerist")
        ? QStringLiteral("careerist") : player->getKingdom();
}

bool HegemonyMode::isFriendWith(const Player *self, const Player *other, bool considerAnjiang)
{
    // Recruitment publicly establishes allegiance without exposing either general.
    if ((self->getRole().startsWith(QStringLiteral("careerist_"))
            || other->getRole().startsWith(QStringLiteral("careerist_")))
        && self->hasShownRole() && other->hasShownRole())
        return self->getRole() == other->getRole();
    if (considerAnjiang) {
        if (!other->hasShownOneGeneral() && self != other)
            return false;
    } else if (!self->hasShownOneGeneral() || !other->hasShownOneGeneral()) {
        return false;
    }
    if (self->getRole() == QLatin1String("careerist") || other->getRole() == QLatin1String("careerist"))
        return false;
    if (self->getRole().startsWith(QStringLiteral("careerist_"))
        || other->getRole().startsWith(QStringLiteral("careerist_")))
        return self->getRole() == other->getRole();
    return self->getKingdom() == other->getKingdom();
}

bool HegemonyMode::willBeFriendWith(const Player *self, const Player *other)
{
    if (self->hasShownOneGeneral() || !other->hasShownOneGeneral()
        || other->getRole().startsWith(QLatin1String("careerist")) || !self->getActualGeneral1())
        return false;
    const QString kingdom = self->getHegemonyKingdom();
    if (kingdom != other->getKingdom())
        return false;
    int allies = 1;
    // Only our own identity and publicly shown sovereigns may influence
    // this prospective relationship; another concealed lord is private.
    bool livingLord = self->isAlive() && self->isHegemonyLord();
    bool deadLord = false;
    const QList<const Player *> siblings = self->getSiblings();
    foreach (const Player *sibling, siblings) {
        if (sibling->getKingdom() != kingdom)
            continue;
        if (sibling->hasShownGeneral() && sibling->isHegemonyLord()) {
            livingLord = livingLord || sibling->isAlive();
            deadLord = deadLord || sibling->isDead();
        }
        if (sibling->hasShownOneGeneral() && !sibling->getRole().startsWith(QLatin1String("careerist")))
            ++allies;
    }
    return !deadLord && (livingLord || allies <= (siblings.size() + 1) / 2);
}

bool HegemonyMode::hasShownSkill(const Player *player, const QString &skillName)
{
    if (player->hasShownGeneral() && player->inHeadSkills(skillName))
        return true;
    if (player->hasShownGeneral2() && player->inDeputySkills(skillName))
        return true;
    return false;
}

bool HegemonyMode::innateGeneralShown(const Player *owner, int bindHead)
{
    if (!owner)
        return false;
    if (bindHead == 1)
        return owner->hasShownGeneral();
    if (bindHead == 2)
        return owner->hasShownGeneral2();
    return false;
}

bool HegemonyMode::innateGeneralConcealed(const Player *owner, int bindHead)
{
    if (!owner)
        return false;
    if (bindHead == 1)
        return !owner->hasShownGeneral();
    if (bindHead == 2)
        return !owner->hasShownGeneral2();
    return false;
}
