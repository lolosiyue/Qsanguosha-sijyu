#include "skill-dialog-registry.h"

#include "mobile.h"
#include "ol.h"
#include "wind.h"

#include <QDialog>

QDialog *SkillDialogRegistry::create(const SkillDialogInfo &info, QWidget *parent)
{
    if (!info.isValid())
        return nullptr;

    QDialog *dialog = nullptr;
    if (info.type == QStringLiteral("guhuo")) {
        dialog = GuhuoDialog::getInstance(
            info.objectName,
            info.parameters.value(QStringLiteral("left"), true).toBool(),
            info.parameters.value(QStringLiteral("right"), true).toBool(),
            info.parameters.value(QStringLiteral("playOnly"), true).toBool(),
            info.parameters.value(QStringLiteral("slashCombined"), false).toBool(),
            info.parameters.value(QStringLiteral("delayedTricks"), false).toBool(),
            info.parameters.value(QStringLiteral("refresh"), false).toBool());
    } else if (info.type == QStringLiteral("juguan")) {
        dialog = JuguanDialog::getInstance(
            info.objectName,
            info.parameters.value(QStringLiteral("cardNames")).toString());
    } else if (info.type == QStringLiteral("tiansuan")) {
        dialog = TiansuanDialog::getInstance(
            info.objectName,
            info.parameters.value(QStringLiteral("choices")).toString());
    }

    if (dialog != nullptr && parent != nullptr && dialog->parent() != parent)
        dialog->setParent(parent, Qt::Dialog);
    return dialog;
}
