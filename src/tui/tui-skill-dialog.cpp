#include "tui-skill-dialog.h"
#include "tui-text.h"

#include "card.h"
#include "engine.h"
#include "server-info.h"
#include "skill.h"
#include "skill-dialog-info.h"
#include "standard.h"
#include "tui-client-player.h"

#include <QHash>
#include <QVariantMap>

namespace {

const char *const kGuhuo = "guhuo";
const char *const kJuguan = "juguan";
const char *const kTiansuan = "tiansuan";

// Card and skill names come out of the engine's own table, not the text
// client's: they are the same words the desktop shows.
QString engineName(const QString &name)
{
    return Sanguosha != nullptr ? Sanguosha->translate(name) : name;
}

// The desktop keeps one dialog per skill alive for the life of the process and
// lets its buttons own the cloned option cards. This is that dialog without the
// widgets. The clones have to outlive the call that builds them: the tag the
// skill reads back holds a bare pointer to one of them.
struct DialogState
{
    QString type;
    // Both the tag key and the skill name the option cards are stamped with.
    QString tagKey;
    QVariantMap parameters;
    // What the option list was built against. A different game can ban a
    // different set, and the clones would otherwise outlive the reason they
    // were on offer.
    QStringList banned;
    QStringList optionNames;
    QHash<QString, const Card *> cards;

    ~DialogState() { qDeleteAll(cards); }
};

QHash<QString, DialogState *> &dialogCache()
{
    static QHash<QString, DialogState *> cache;
    return cache;
}

// A trigger skill can carry its dialog on the view-as skill inside it, which
// is the skill the player is offered by name.
SkillDialogInfo dialogInfoFor(const QString &skillName)
{
    if (Sanguosha == nullptr || skillName.isEmpty())
        return SkillDialogInfo();
    if (const Skill *skill = Sanguosha->getSkill(skillName)) {
        const SkillDialogInfo info = skill->getDialogInfo();
        if (info.isValid())
            return info;
    }
    if (const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(skillName)) {
        const SkillDialogInfo info = viewAs->getDialogInfo();
        if (info.isValid())
            return info;
    }
    return SkillDialogInfo();
}

void addCardOption(DialogState *state, const QString &name)
{
    if (name.isEmpty() || state->cards.contains(name))
        return;
    Card *card = Sanguosha->cloneCard(name);
    if (card == nullptr)
        return;
    // A clone that renames itself would otherwise be listed twice.
    if (state->cards.contains(card->objectName())) {
        delete card;
        return;
    }
    card->setSkillName(state->tagKey);
    card->setCanRecast(false);
    state->cards.insert(card->objectName(), card);
    state->optionNames << card->objectName();
}

void buildGuhuo(DialogState *state, const QStringList &banPackages)
{
    const bool left = state->parameters.value(QStringLiteral("left"), true).toBool();
    const bool right = state->parameters.value(QStringLiteral("right"), true).toBool();
    const bool slashCombined
        = state->parameters.value(QStringLiteral("slashCombined"), false).toBool();
    const bool delayedTricks
        = state->parameters.value(QStringLiteral("delayedTricks"), false).toBool();

    if (left) {
        for (const BasicCard *engineCard : Sanguosha->findChildren<const BasicCard *>()) {
            if (engineCard->objectName().startsWith(QLatin1Char('_'))
                || banPackages.contains(engineCard->getPackage()))
                continue;
            if (slashCombined && engineCard->isKindOf("Slash")
                && engineCard->objectName() != QLatin1String("slash"))
                continue;
            addCardOption(state, engineCard->objectName());
        }
    }
    if (right) {
        for (const TrickCard *engineCard : Sanguosha->findChildren<const TrickCard *>()) {
            if (engineCard->objectName().startsWith(QLatin1Char('_'))
                || banPackages.contains(engineCard->getPackage())
                || (!delayedTricks && !engineCard->isNDTrick()))
                continue;
            addCardOption(state, engineCard->objectName());
        }
    }
}

void buildJuguan(DialogState *state)
{
    QString names = state->parameters.value(QStringLiteral("cardNames")).toString();
    names.remove(QLatin1Char('!'));
    names.remove(QLatin1Char('$'));
    for (const QString &name : names.split(QLatin1Char(','), Qt::SkipEmptyParts))
        addCardOption(state, name.trimmed());
}

void buildTiansuan(DialogState *state)
{
    const QString choices = state->parameters.value(QStringLiteral("choices")).toString();
    for (const QString &choice : choices.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = choice.trimmed();
        if (!trimmed.isEmpty() && !state->optionNames.contains(trimmed))
            state->optionNames << trimmed;
    }
}

DialogState *dialogFor(const QString &skillName, const QStringList &banPackages)
{
    const SkillDialogInfo info = dialogInfoFor(skillName);
    if (!info.isValid())
        return nullptr;

    // The desktop merges the server's banned packages into the engine-wide
    // ServerInfo; the text client never fills that in, so it passes them.
    QStringList banned = banPackages;
    for (const QString &package : ServerInfo.BanPackages) {
        if (!banned.contains(package))
            banned << package;
    }
    banned.sort();

    DialogState *cached = dialogCache().value(skillName, nullptr);
    // The desktop's "update" flag deletes the dialog and builds a new one; the
    // parameter comparison catches a skill that changed its mind otherwise.
    const bool refresh = info.parameters.value(QStringLiteral("refresh"), false).toBool();
    if (cached != nullptr && !refresh && cached->type == info.type
        && cached->parameters == info.parameters && cached->banned == banned)
        return cached;

    delete dialogCache().take(skillName);
    auto *state = new DialogState;
    state->type = info.type;
    state->tagKey = info.objectName.isEmpty() ? skillName : info.objectName;
    state->parameters = info.parameters;
    state->banned = banned;
    if (state->type == QLatin1String(kGuhuo))
        buildGuhuo(state, banned);
    else if (state->type == QLatin1String(kJuguan))
        buildJuguan(state);
    else if (state->type == QLatin1String(kTiansuan))
        buildTiansuan(state);
    dialogCache().insert(skillName, state);
    return state;
}

CardUseStruct::CardUseReason currentReason()
{
    return Sanguosha != nullptr ? Sanguosha->getCurrentCardUseReason()
                                : CardUseStruct::CARD_USE_REASON_UNKNOWN;
}

// GuhuoDialog::shouldPopup() and its two siblings, verbatim.
bool shouldPopup(const DialogState *state)
{
    if (state->type == QLatin1String(kGuhuo)) {
        return !state->parameters.value(QStringLiteral("playOnly"), true).toBool()
            || currentReason() == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    if (state->type == QLatin1String(kJuguan)) {
        const QString cards = state->parameters.value(QStringLiteral("cardNames")).toString();
        return !cards.isEmpty()
            && (cards.endsWith(QLatin1Char('!'))
                || currentReason() == CardUseStruct::CARD_USE_REASON_PLAY);
    }
    // TiansuanDialog has no such gate: what its marks leave is what it shows.
    return state->type == QLatin1String(kTiansuan);
}

// TiansuanDialog::MarkJudge().
bool tiansuanAllows(const DialogState *state, const QString &choice, const Player *self)
{
    const QString mark = state->tagKey + QStringLiteral("_tiansuan_remove_") + choice;
    for (const QString &markName : self->getMarkNames()) {
        if (markName.startsWith(mark) && self->getMark(markName) > 0)
            return false;
    }
    return true;
}

bool optionEnabled(const DialogState *state, const QString &name)
{
    const Player *self = QSanEngine::Self;
    if (self == nullptr)
        return false;
    if (state->type == QLatin1String(kTiansuan))
        return tiansuanAllows(state, name, self);

    const Card *card = state->cards.value(name, nullptr);
    if (card == nullptr)
        return false;
    if (state->type == QLatin1String(kJuguan)) {
        const QString cards = state->parameters.value(QStringLiteral("cardNames")).toString();
        return !self->isLocked(card)
            && (cards.startsWith(QLatin1Char('$'))
                || currentReason() != CardUseStruct::CARD_USE_REASON_PLAY
                || card->isAvailable(self));
    }
    if ((state->parameters.value(QStringLiteral("playOnly"), true).toBool()
         || currentReason() == CardUseStruct::CARD_USE_REASON_PLAY)
        && !card->isAvailable(self))
        return false;
    return !self->isLocked(card);
}

QList<TuiSkillDeclaration> declarationsIn(const DialogState *state)
{
    if (state == nullptr || !shouldPopup(state))
        return {};
    QList<TuiSkillDeclaration> result;
    const bool tiansuan = state->type == QLatin1String(kTiansuan);
    for (const QString &name : state->optionNames) {
        const bool enabled = optionEnabled(state, name);
        // The tiansuan dialog builds no button at all for a choice its marks
        // have taken away, so there is nothing to list and nothing to accept.
        if (tiansuan && !enabled)
            continue;
        TuiSkillDeclaration option;
        option.name = name;
        option.label = engineName(name);
        option.enabled = enabled;
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
    return parts.join(QStringLiteral("、"));
}

} // namespace

QList<TuiSkillDeclaration> tuiSkillDeclarations(const QString &skillName,
                                                const QStringList &banPackages)
{
    if (Sanguosha == nullptr)
        return {};
    return declarationsIn(dialogFor(skillName, banPackages));
}

bool tuiSkillNeedsDeclaration(const QString &skillName, const QStringList &banPackages)
{
    for (const TuiSkillDeclaration &option : tuiSkillDeclarations(skillName, banPackages)) {
        if (option.enabled)
            return true;
    }
    return false;
}

bool tuiApplySkillDeclaration(const QString &skillName, const QString &option,
                              const QStringList &banPackages, QString *error)
{
    auto fail = [error](const QString &text) {
        if (error != nullptr)
            *error = text;
        return false;
    };

    const QString skillLabel = engineName(skillName);
    // dialogFor() can rebuild the state and delete the old one, so it is called
    // once here and the option list is read off that same instance.
    const DialogState *state = Sanguosha != nullptr ? dialogFor(skillName, banPackages) : nullptr;
    Player *self = QSanEngine::Self;
    // Every popup() starts by clearing the last answer, so a declaration never
    // leaks from the activation before this one.
    if (state != nullptr && self != nullptr)
        self->removeTag(state->tagKey);

    const QList<TuiSkillDeclaration> options = declarationsIn(state);
    if (options.isEmpty()) {
        return option.isEmpty() ? true : fail(tuiText("tui_declaration_not_needed").arg(skillLabel));
    }

    QString firstEnabled;
    for (const TuiSkillDeclaration &candidate : options) {
        if (candidate.enabled) {
            firstEnabled = candidate.name;
            break;
        }
    }
    if (option.isEmpty()) {
        // Nothing in the dialog is usable: the desktop closes it again without
        // asking, and so does this.
        if (firstEnabled.isEmpty())
            return true;
        return fail(tuiText("tui_declaration_required")
                        .arg(skillLabel, firstEnabled, listing(options)));
    }

    const TuiSkillDeclaration *chosen = nullptr;
    for (const TuiSkillDeclaration &candidate : options) {
        if (candidate.name.compare(option, Qt::CaseInsensitive) == 0
            || candidate.label == option) {
            chosen = &candidate;
            break;
        }
    }
    if (chosen == nullptr)
        return fail(tuiText("tui_declaration_unknown").arg(skillLabel, option, listing(options)));
    if (self == nullptr)
        return fail(tuiText("tui_engine_not_loaded"));

    if (state->type == QLatin1String(kTiansuan)) {
        self->setTag(state->tagKey, chosen->name);
        return true;
    }
    const Card *card = state->cards.value(chosen->name, nullptr);
    if (card == nullptr)
        return fail(tuiText("tui_declaration_unknown").arg(skillLabel, option, listing(options)));
    self->setTag(state->tagKey, QVariant::fromValue(card));
    return true;
}
