#ifndef TUI_RENDERER_H
#define TUI_RENDERER_H

#include "tui-resolvers.h"

#include <QVariantMap>

class ClientGameState;
struct InteractionRequest;

class TuiRenderer
{
public:
    // The renderer used to own these; they moved out so the board presenter can
    // ask the engine the same questions without pulling in line formatting.
    // The aliases keep every existing TuiRenderer::Resolvers reference valid.
    using CardTargets = TuiCardTargets;
    using Resolvers = TuiResolvers;
    using CardResolver = std::function<QString(int)>;
    using NameResolver = std::function<QString(const QString &)>;
    using PlayerResolver = std::function<QString(const QString &)>;
    using KingdomResolver = std::function<QString(const QString &)>;
    using CardHintResolver = std::function<QString(int)>;
    using PlayerHintResolver = std::function<QString(const QString &)>;
    using SkillHintResolver = std::function<QString(const QString &, int)>;
    using CardTargetResolver = std::function<TuiCardTargets(int)>;

    explicit TuiRenderer(bool ansiEnabled = false, Resolvers resolvers = {});

    static QString sanitize(const QString &text, qsizetype maximumLength = 4096);
    // Server text written for the desktop log box, rendered for a terminal:
    // tags dropped, <br> and friends turned into a separator, entities
    // unescaped.
    static QString plainText(const QString &markup);
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
    // The player-facing name of a prompt, used to talk about a request
    // without exposing its wire id.
    QString interactionTitle(const InteractionRequest &request) const;
    // A wire token (connection state, phase, role, general, ...) as the player
    // should read it: a fixed label when the protocol owns the vocabulary,
    // otherwise whatever the engine translation table says.
    QString nameText(const QString &name) const;

private:
    QString heading(const QString &text) const;
    QString cardText(const ClientGameState &state, int cardId) const;
    QString playerText(const QString &objectName) const;
    // "時語（sgs1）" -- a script and /players still speak object names.
    QString playerLabel(const QString &objectName) const;
    QString gameStatusText(const QString &status) const;
    QString kingdomText(const QVariantMap &player) const;
    QString answerHint(const InteractionRequest &request) const;
    bool m_ansiEnabled = false;
    Resolvers m_resolvers;
};

#endif
