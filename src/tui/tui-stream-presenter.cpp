#include "tui-stream-presenter.h"

#include <QTextStream>

TuiStreamPresenter::TuiStreamPresenter(Sink out, Sink err)
    : m_out(std::move(out)), m_err(std::move(err))
{
}

void TuiStreamPresenter::writeOutput(const QString &text)
{
    const QString line = text + QLatin1Char('\n');
    if (m_out) {
        m_out(line);
        return;
    }
    QTextStream(stdout) << line << Qt::flush;
}

void TuiStreamPresenter::writeError(const QString &text)
{
    const QString line = QStringLiteral("TUI_ERROR ") + text + QLatin1Char('\n');
    if (m_err) {
        m_err(line);
        return;
    }
    QTextStream(stderr) << line << Qt::flush;
}

void TuiStreamPresenter::shutdown()
{
    // Nothing to give back: this presenter never took the terminal over.
}

void TuiStreamPresenter::stateChanged(const ClientGameState &)
{
    // The line client prints what it is told to print; it has no view to refresh.
}

void TuiStreamPresenter::interactionChanged(const InteractionRequest *)
{
    // Likewise: TuiInteractionView already wrote the prompt through writeOutput().
}
