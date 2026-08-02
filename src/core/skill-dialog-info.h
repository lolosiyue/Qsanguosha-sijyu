#ifndef _SKILL_DIALOG_INFO_H
#define _SKILL_DIALOG_INFO_H

#include <QVariantMap>

struct SkillDialogInfo
{
    QString type;
    QString objectName;
    QVariantMap parameters;

    bool isValid() const { return !type.isEmpty(); }

    static SkillDialogInfo named(const QString &dialogType, const QString &object = QString())
    {
        SkillDialogInfo info;
        info.type = dialogType;
        info.objectName = object;
        return info;
    }

    static SkillDialogInfo guhuo(const QString &object, bool left = true, bool right = true,
                                 bool playOnly = true, bool slashCombined = false,
                                 bool delayedTricks = false, bool refresh = false)
    {
        SkillDialogInfo info = named("guhuo", object);
        info.parameters.insert("left", left);
        info.parameters.insert("right", right);
        info.parameters.insert("playOnly", playOnly);
        info.parameters.insert("slashCombined", slashCombined);
        info.parameters.insert("delayedTricks", delayedTricks);
        info.parameters.insert("refresh", refresh);
        return info;
    }

    static SkillDialogInfo juguan(const QString &object, const QString &cardNames)
    {
        SkillDialogInfo info = named("juguan", object);
        info.parameters.insert("cardNames", cardNames);
        return info;
    }

    static SkillDialogInfo tiansuan(const QString &object, const QString &choices = QString())
    {
        SkillDialogInfo info = named("tiansuan", object);
        info.parameters.insert("choices", choices);
        return info;
    }
};

Q_DECLARE_METATYPE(SkillDialogInfo)

#endif
