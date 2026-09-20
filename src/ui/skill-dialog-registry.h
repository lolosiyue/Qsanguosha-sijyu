#ifndef SKILL_DIALOG_REGISTRY_H
#define SKILL_DIALOG_REGISTRY_H

#include "skill-dialog-info.h"

class QDialog;
class QWidget;

namespace SkillDialogRegistry
{
    using Factory = QDialog *(*)(const SkillDialogInfo &info, QWidget *parent);

    // UI modules register metadata types here; the registry owns dispatch while
    // each factory keeps its dialog lifetime and signal behavior unchanged.
    void registerFactory(const QString &type, Factory factory);
    QDialog *create(const SkillDialogInfo &info, QWidget *parent = nullptr);
}

#endif
