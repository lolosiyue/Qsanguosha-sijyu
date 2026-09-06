#ifndef TUI_STREAM_PRESENTER_H
#define TUI_STREAM_PRESENTER_H

#include "tui-presenter.h"

// The line-oriented client the TUI has always been: one line per write, straight
// to stdout. Kept as a peer of the board presenter rather than a fallback --
// scripts, redirected output and CI all run through it.
class TuiStreamPresenter final : public TuiPresenter
{
public:
    // Empty sinks mean stdout and stderr; tests inject their own.
    explicit TuiStreamPresenter(Sink out = {}, Sink err = {});

    void writeOutput(const QString &text) override;
    void writeError(const QString &text) override;
    void shutdown() override;
    void stateChanged(const ClientGameState &state) override;
    void interactionChanged(const InteractionRequest *request) override;

private:
    Sink m_out;
    Sink m_err;
};

#endif
