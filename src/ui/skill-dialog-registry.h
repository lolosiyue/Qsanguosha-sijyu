#ifndef SKILL_DIALOG_REGISTRY_H
#define SKILL_DIALOG_REGISTRY_H

#include "skill-dialog-info.h"

class QDialog;
class QWidget;

namespace SkillDialogRegistry
{
    QDialog *create(const SkillDialogInfo &info, QWidget *parent = nullptr);
}

#endif
