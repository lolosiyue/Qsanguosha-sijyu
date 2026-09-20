#include "tui-skill-dialog.h"
#include "tui-text.h"

#include "engine.h"
#include "runtime/client-player-model.h"
#include "server-info.h"
#include "skill-declaration.h"

#include <QHash>

#include <algorithm>

namespace {

QString engineName(const QString &name)
{
    return Sanguosha != nullptr ? Sanguosha->translate(name) : name;
}

// Keep the committed clone alive through the subsequent viewAs()/wire build.
QHash<QString, SkillDeclarationSession *> &appliedSessions()
{
    static QHash<QString, SkillDeclarationSession *> sessions;
    return sessions;
}

void clearAppliedSession(const QString &skillName)
{
    SkillDeclarationSession *old = appliedSessions().take(skillName);
    if (old == nullptr)
        return;
    const QString tagKey = old->info().objectName.isEmpty() ? skillName : old->info().objectName;
    if (QSanEngine::Self != nullptr)
        QSanEngine::Self->removeTag(tagKey);
    delete old;
}

QStringList mergedBans(const QStringList &banPackages)
{
    QStringList result = banPackages;
    for (const QString &package : ServerInfo.BanPackages) {
        if (!result.contains(package))
            result.append(package);
    }
    std::sort(result.begin(), result.end());
    return result;
}

QList<TuiSkillDeclaration> declarationsIn(const SkillDeclarationSession &session)
{
    if (!session.supported() || !session.active())
        return {};
    QList<TuiSkillDeclaration> result;
    for (const SkillDeclarationCandidate &candidate : session.candidates()) {
        if (candidate.reason == SkillDeclarationReason::CandidateRemoved)
            continue;
        TuiSkillDeclaration option;
        option.name = candidate.value;
        option.label = engineName(candidate.value);
        if (option.label.isEmpty())
            option.label = candidate.label;
        option.enabled = candidate.enabled;
        option.reason = skillDeclarationReasonName(candidate.reason);
        result.append(option);
    }
    return result;
}

QString listing(const QList<TuiSkillDeclaration> &options)
{
    QStringList parts;
    for (const TuiSkillDeclaration &option : options) {
        const QString shown = option.label == option.name
            ? option.name
            : tuiText("tui_declaration_label").arg(option.label, option.name);
        parts << (option.enabled ? shown : tuiText("tui_declaration_disabled").arg(shown));
    }
    return parts.join(tuiText("tui_list_separator"));
}

} // namespace

QList<TuiSkillDeclaration> tuiSkillDeclarations(const QString &skillName,
                                                const QStringList &banPackages, quint64 requestId)
{
    if (Sanguosha == nullptr || skillName.isEmpty())
        return {};
    const SkillDeclarationSession session(skillName, QSanEngine::Self,
        Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        mergedBans(banPackages), requestId);
    return declarationsIn(session);
}

bool tuiSkillNeedsDeclaration(const QString &skillName, const QStringList &banPackages,
                              quint64 requestId)
{
    for (const TuiSkillDeclaration &option : tuiSkillDeclarations(skillName, banPackages, requestId)) {
        if (option.enabled)
            return true;
    }
    return false;
}

bool tuiApplySkillDeclaration(const QString &skillName, const QString &option,
                              const QStringList &banPackages, QString *error, quint64 requestId)
{
    auto fail = [error](const QString &text) {
        if (error != nullptr)
            *error = text;
        return false;
    };
    if (Sanguosha == nullptr)
        return fail(tuiText("tui_engine_not_loaded"));

    // An invalid attempt must clear the previous answer before the service
    // validates the replacement; viewAs() must never see stale declaration data.
    clearAppliedSession(skillName);
    auto *session = new SkillDeclarationSession(skillName, QSanEngine::Self,
        Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        mergedBans(banPackages), requestId);
    session->clearChoice();
    const QList<TuiSkillDeclaration> options = declarationsIn(*session);
    const QString skillLabel = engineName(skillName);
    if (options.isEmpty()) {
        const bool accepted = option.isEmpty() && session->apply(QString());
        if (accepted) {
            appliedSessions().insert(skillName, session);
            return true;
        }
        delete session;
        return option.isEmpty()
            ? true : fail(tuiText("tui_declaration_not_needed").arg(skillLabel));
    }

    QString firstEnabled;
    for (const TuiSkillDeclaration &candidate : options) {
        if (candidate.enabled) {
            firstEnabled = candidate.name;
            break;
        }
    }
    if (option.isEmpty() && !session->validate(QString()).accepted) {
        delete session;
        if (firstEnabled.isEmpty())
            return true;
        return fail(tuiText("tui_declaration_required")
                        .arg(skillLabel, firstEnabled, listing(options)));
    }

    QString canonical = option;
    for (const TuiSkillDeclaration &candidate : options) {
        if (candidate.label == option) {
            canonical = candidate.name;
            break;
        }
    }
    const SkillDeclarationValidation validation = session->validate(canonical);
    if (!validation.accepted) {
        delete session;
        return fail(tuiText("tui_declaration_unknown").arg(skillLabel, option, listing(options)));
    }
    if (!session->apply(canonical)) {
        delete session;
        return fail(tuiText("tui_declaration_unknown").arg(skillLabel, option, listing(options)));
    }
    appliedSessions().insert(skillName, session);
    return true;
}
