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
    QString renderState(const ClientGameState &state) const;
    QString renderPlayers(const ClientGameState &state) const;
    QString renderHand(const ClientGameState &state) const;
    QString renderInteraction(const InteractionRequest &request) const;

private:
    QString heading(const QString &text) const;
    QString cardText(const ClientGameState &state, int cardId) const;
    QString nameText(const QString &name) const;
    bool m_ansiEnabled = false;
    CardResolver m_cardResolver;
    NameResolver m_nameResolver;
};

#endif
