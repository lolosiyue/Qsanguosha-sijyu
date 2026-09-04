#ifndef TUI_SKILL_DIALOG_H
#define TUI_SKILL_DIALOG_H

#include <QList>
#include <QString>
#include <QStringList>

// One button of a skill's declaration dialog. The desktop draws these as
// GuhuoDialog / JuguanDialog / TiansuanDialog; here they are lines the player
// picks by name.
struct TuiSkillDeclaration
{
    // What the player types, and what the skill reads back out of the tag.
    QString name;
    // The translated name, for the listing only.
    QString label;
    // The desktop greys the button out. A disabled line is still listed and
    // still accepted: the check runs off a client-side copy of the table and a
    // wrong hint must not become a wall.
    bool enabled = true;
};

// The declarations this skill would put in front of the player right now, in
// the dialog's own order. Empty when the skill has no dialog at all or when
// the dialog would stay shut for the current card use reason -- both of which
// the desktop answers by going straight through with nothing declared.
QList<TuiSkillDeclaration> tuiSkillDeclarations(const QString &skillName,
                                                const QStringList &banPackages);

// Whether an answer using this skill has to carry a declaration. A dialog
// with nothing enabled left in it does not stop the desktop either.
bool tuiSkillNeedsDeclaration(const QString &skillName, const QStringList &banPackages);

// Writes the declaration where the skill's viewAs() reads it, exactly as the
// dialog's applyOption() does, and clears the previous one either way. An
// empty option only clears, and is an error only when one was required.
//
// The option is matched against both the internal name ("slash") and the
// translated one ("杀"). On failure error carries the listing the player needs.
bool tuiApplySkillDeclaration(const QString &skillName, const QString &option,
                              const QStringList &banPackages, QString *error);

#endif
