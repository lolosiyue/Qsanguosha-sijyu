#ifndef TUI_RENDERER_H
#define TUI_RENDERER_H

#include <QString>

#include <functional>

class ClientGameState;
struct InteractionRequest;

class TuiRenderer
{
public:
    using CardResolver = std::function<QString(int)>;
    using NameResolver = std::function<QString(const QString &)>;

    explicit TuiRenderer(bool ansiEnabled = false, CardResolver cardResolver = {},
                         NameResolver nameResolver = {});

    static QString sanitize(const QString &text, qsizetype maximumLength = 4096);
    // askForCard-style wire prompts: key:%src:%dest:%arg:%arg2, via formatClientPromptList.
    static QString formatPrompt(const QString &prompt,
                                const std::function<QString(const QString &)> &translate,
                                const std::function<QString(const QString &)> &playerName = {});
    // Feedback for a control command. Empty when there is nothing worth saying.
    static QString commandResultText(int command, bool success, const QString &message);
    QString renderState(const ClientGameState &state) const;
    QString renderPlayers(const ClientGameState &state) const;
    QString renderHand(const ClientGameState &state) const;
    QString renderInteraction(const InteractionRequest &request) const;

private:
    QString heading(const QString &text) const;
    QString cardText(const ClientGameState &state, int cardId) const;
    QString nameText(const QString &name) const;
    QString interactionTitle(const InteractionRequest &request) const;
    QString answerHint(const InteractionRequest &request) const;
    bool m_ansiEnabled = false;
    CardResolver m_cardResolver;
    NameResolver m_nameResolver;
};

#endif
