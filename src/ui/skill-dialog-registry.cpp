#include "skill-dialog-registry.h"

#include "mobile.h"
#include "ol.h"
#include "special-skill-dialogs.h"
#include "wind.h"

#include <QDialog>
#include <QHash>

namespace {

QHash<QString, SkillDialogRegistry::Factory> &factories()
{
    static QHash<QString, SkillDialogRegistry::Factory> result;
    return result;
}

QDialog *createGuhuo(const SkillDialogInfo &info, QWidget *)
{
    return GuhuoDialog::getInstance(
        info.objectName,
        info.parameters.value(QStringLiteral("left"), true).toBool(),
        info.parameters.value(QStringLiteral("right"), true).toBool(),
        info.parameters.value(QStringLiteral("playOnly"), true).toBool(),
        info.parameters.value(QStringLiteral("slashCombined"), false).toBool(),
        info.parameters.value(QStringLiteral("delayedTricks"), false).toBool(),
        info.parameters.value(QStringLiteral("refresh"), false).toBool());
}

QDialog *createJuguan(const SkillDialogInfo &info, QWidget *)
{
    return JuguanDialog::getInstance(
        info.objectName,
        info.parameters.value(QStringLiteral("cardNames")).toString());
}

QDialog *createTiansuan(const SkillDialogInfo &info, QWidget *)
{
    return TiansuanDialog::getInstance(
        info.objectName,
        info.parameters.value(QStringLiteral("choices")).toString());
}

void installBuiltinFactories()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    SkillDialogRegistry::registerFactory(QStringLiteral("guhuo"), createGuhuo);
    SkillDialogRegistry::registerFactory(QStringLiteral("juguan"), createJuguan);
    SkillDialogRegistry::registerFactory(QStringLiteral("tiansuan"), createTiansuan);
}

}

void SkillDialogRegistry::registerFactory(const QString &type, Factory factory)
{
    if (!type.isEmpty() && factory != nullptr)
        factories().insert(type, factory);
}

QDialog *SkillDialogRegistry::create(const SkillDialogInfo &info, QWidget *parent)
{
    if (!info.isValid())
        return nullptr;

    installBuiltinFactories();
    installSpecialSkillDialogFactories();
    const Factory factory = factories().value(info.type, nullptr);
    QDialog *dialog = factory == nullptr ? nullptr : factory(info, parent);

    if (dialog != nullptr && parent != nullptr && dialog->parent() != parent)
        dialog->setParent(parent, Qt::Dialog);
    return dialog;
}
