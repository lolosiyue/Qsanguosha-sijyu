#include "tui-ui-mode.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

const QString UiModeKey = QStringLiteral("ui_mode");

// A dedicated ini rather than the application's default QSettings scope
// (which would need an organization name this app has never set): one small
// file, one key, easy to point at a temp directory from a test. Returns a
// path rather than a QSettings itself -- QSettings is a QObject and cannot
// be returned by value.
QString tuiUiModeSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/ui-mode.ini");
}

} // namespace

TuiUiModeDecision tuiResolveUiMode(const TuiUiModeInputs &inputs)
{
    TuiUiModeDecision decision;

    // Not one of docs/tui-board-ui.md §6.1's own rows -- the table assumes
    // --ui is either absent or one of the two known values. Checked first so
    // a typo is reported as itself rather than as whatever the table's other
    // rows happen to fall through to.
    const bool flagGiven = !inputs.flag.isEmpty();
    const bool wantsBoard = flagGiven && inputs.flag == QStringLiteral("board");
    const bool wantsClassic = flagGiven && inputs.flag == QStringLiteral("classic");
    if (flagGiven && !wantsBoard && !wantsClassic) {
        decision.conflict = true;
        decision.conflictReason =
            tr("--ui 的值 \"%1\" 无法识别，只接受 classic 或 board").arg(inputs.flag);
        return decision;
    }

    // §6.1 rows 1-3: each forcing condition alone means classic, no
    // question asked. Evaluated in the table's own order, first match wins,
    // though nothing downstream cares which one fired except the message
    // below.
    QString forcingReason;
    if (inputs.hasScript)
        forcingReason = tr("--script");
    else if (!inputs.stdoutIsTty || !inputs.stdinIsTty)
        forcingReason = tr("标准输入或标准输出不是终端");
    else if (inputs.plain)
        forcingReason = tr("--plain／--no-color／NO_COLOR");

    if (!forcingReason.isEmpty()) {
        // §6.1 row 4, the deliberate one: asking for board while a forcing
        // condition holds is an error, not a silent downgrade. Downgrading
        // here would turn "I asked for board and got no board" into
        // something the player has to go debug; erasing that signal is
        // exactly what this row exists to avoid. The automatic path below
        // (no --ui at all) is the one place a quiet fallback is correct,
        // because there the player expressed no intent for this run to
        // contradict.
        if (wantsBoard) {
            decision.conflict = true;
            decision.conflictReason =
                tr("--ui board 与 %1 冲突：board 需要一个真正的交互式终端").arg(forcingReason);
            return decision;
        }
        decision.mode = TuiUiMode::Classic;
        return decision;
    }

    // §6.1 row 5: an explicit choice with no forcing condition is followed
    // exactly, never questioned.
    if (wantsBoard) {
        decision.mode = TuiUiMode::Board;
        return decision;
    }
    if (wantsClassic) {
        decision.mode = TuiUiMode::Classic;
        return decision;
    }

    // §6.1 row 6: no --ui, but a previous run's answer was remembered.
    if (inputs.savedChoice == QStringLiteral("board")) {
        decision.mode = TuiUiMode::Board;
        return decision;
    }
    if (inputs.savedChoice == QStringLiteral("classic")) {
        decision.mode = TuiUiMode::Classic;
        return decision;
    }

    // §6.1 row 7: nothing on file, nothing forced -- ask once, before
    // connecting. mode stays TuiUiMode::Classic (the struct's default) until
    // the caller records an answer.
    decision.askUser = true;
    return decision;
}

QString tuiSavedUiMode()
{
    QSettings settings(tuiUiModeSettingsPath(), QSettings::IniFormat);
    const QString value = settings.value(UiModeKey).toString();
    if (value == QStringLiteral("board") || value == QStringLiteral("classic"))
        return value;
    return QString();
}

bool tuiSaveUiMode(TuiUiMode mode)
{
    QSettings settings(tuiUiModeSettingsPath(), QSettings::IniFormat);
    settings.setValue(UiModeKey,
        mode == TuiUiMode::Board ? QStringLiteral("board") : QStringLiteral("classic"));
    // setValue() only stages the write; sync() is what actually touches the
    // filesystem and is what status() reports on afterwards (an unwritable
    // config directory, a read-only filesystem, etc. would otherwise fail
    // silently -- the ini would just never appear, and the next run would
    // ask the startup question again with no indication why "remember"
    // didn't work).
    settings.sync();
    return settings.status() == QSettings::NoError;
}
