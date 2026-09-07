#ifndef CLIENT_STATE_PROJECTION_H
#define CLIENT_STATE_PROJECTION_H

#include "card.h"
#include "player.h"

#include <QSet>
#include <QString>
#include <QVariantMap>

namespace ClientRules {

// ClientGameStateReducer normalizes a few wire property names. Player still
// exposes the original names through Q_PROPERTY, so a native client projection
// has to restore those names when applying state to an engine Player.
inline QString wirePropertyName(const QString &stateKey)
{
    if (stateKey == QLatin1String("max_hp"))
        return QStringLiteral("maxhp");
    if (stateKey == QLatin1String("deputy_general"))
        return QStringLiteral("general2");
    return stateKey;
}

// Keys that need typed handling, or deliberately belong to presentation/state
// rather than Player's scalar property surface.
inline const QSet<QString> &structuredPlayerStateKeys()
{
    static const QSet<QString> keys{
        QStringLiteral("object_name"), QStringLiteral("screen_name"),
        QStringLiteral("hand_count"), QStringLiteral("hand_max"),
        QStringLiteral("flags"), QStringLiteral("marks"),
        QStringLiteral("history"), QStringLiteral("card_limitations"),
        QStringLiteral("skills"), QStringLiteral("skill_instances"),
        QStringLiteral("piles"), QStringLiteral("general_piles"),
        QStringLiteral("tags"), QStringLiteral("ui_state"),
        QStringLiteral("equip_areas"), QStringLiteral("skill_descriptions"),
        QStringLiteral("card_descriptions"), QStringLiteral("revealed_general"),
        QStringLiteral("offensive_distance"), QStringLiteral("defensive_distance")};
    return keys;
}

inline bool playerStateChanged(const QVariantMap &applied, const QVariantMap &data,
                               const QString &key)
{
    return applied.value(key) != data.value(key);
}

inline void applyPlayerState(Player *player, const QVariantMap &data,
                             const QVariantMap &applied)
{
    if (player == nullptr)
        return;

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (structuredPlayerStateKeys().contains(it.key())
            || !playerStateChanged(applied, data, it.key())) {
            continue;
        }
        // Unknown names such as distanceTo_<name> and View_As_Equips_List are
        // intentionally dynamic QObject properties; engine client paths read
        // them from exactly there.
        player->setProperty(wirePropertyName(it.key()).toLatin1().constData(), it.value());
    }

    if (playerStateChanged(applied, data, QStringLiteral("screen_name")))
        player->setScreenName(data.value(QStringLiteral("screen_name")).toString());

    if (playerStateChanged(applied, data, QStringLiteral("flags"))) {
        player->setFlags(QStringLiteral("."));
        for (const QString &flag : data.value(QStringLiteral("flags")).toStringList())
            player->setFlags(flag);
    }

    if (playerStateChanged(applied, data, QStringLiteral("marks"))) {
        const QVariantMap marks = data.value(QStringLiteral("marks")).toMap();
        for (const QString &mark : player->getMarkNames()) {
            if (!marks.contains(mark))
                player->setMark(mark, 0);
        }
        for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
            player->setMark(it.key(), it.value().toInt());
    }

    if (playerStateChanged(applied, data, QStringLiteral("history"))) {
        player->clearHistory();
        const QVariantMap history = data.value(QStringLiteral("history")).toMap();
        for (auto it = history.constBegin(); it != history.constEnd(); ++it)
            player->addHistory(it.key(), it.value().toInt());
    }

    if (playerStateChanged(applied, data, QStringLiteral("card_limitations"))) {
        player->clearCardLimitation();
        for (const QVariant &value : data.value(QStringLiteral("card_limitations")).toList()) {
            const QVariantMap limitation = value.toMap();
            QStringList methods;
            for (const QVariant &method : limitation.value(QStringLiteral("methods")).toList()) {
                const QString name = method.toString();
                if (!name.isEmpty())
                    methods.append(name);
            }
            if (methods.isEmpty())
                continue;
            player->setCardLimitation(methods.join(QLatin1Char(',')),
                limitation.value(QStringLiteral("pattern")).toString(),
                limitation.value(QStringLiteral("reason")).toString(),
                limitation.value(QStringLiteral("single_turn")).toBool());
        }
    }

    if (playerStateChanged(applied, data, QStringLiteral("skills"))) {
        const QStringList wanted = data.value(QStringLiteral("skills")).toStringList();
        for (const QString &skill : applied.value(QStringLiteral("skills")).toStringList()) {
            if (!wanted.contains(skill))
                player->loseSkill(skill);
        }
        const QStringList had = applied.value(QStringLiteral("skills")).toStringList();
        for (const QString &skill : wanted) {
            if (!had.contains(skill))
                player->addSkill(skill);
        }
    }
}

} // namespace ClientRules

#endif
