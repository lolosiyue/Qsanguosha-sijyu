#ifndef TUI_RENDERER_H
#define TUI_RENDERER_H

#include <QString>
#include <QVariantMap>

#include <functional>

class ClientGameState;
struct InteractionRequest;

class TuiRenderer
{
public:
    using CardResolver = std::function<QString(int)>;
    using NameResolver = std::function<QString(const QString &)>;
    // Object name -> the screen name the player picked. A prompt carries only
    // object names, and renderInteraction() has no ClientGameState to look them
    // up in, so the controller hands its own resolver over.
    using PlayerResolver = std::function<QString(const QString &)>;
    // General name -> its kingdom. The server never broadcasts the kingdom
    // property, so it is read off the general the way Player::getKingdom() does.
    using KingdomResolver = std::function<QString(const QString &)>;

    // Everything the renderer needs the engine for. Each one is optional; an
    // absent resolver means the raw value is shown.
    struct Resolvers
    {
        CardResolver card;
        NameResolver name;
        PlayerResolver player;
        KingdomResolver kingdom;
    };

    explicit TuiRenderer(bool ansiEnabled = false, Resolvers resolvers = {});

    static QString sanitize(const QString &text, qsizetype maximumLength = 4096);
    // Server text written for the desktop log box, rendered for a terminal:
    // tags dropped, <br> and friends turned into a separator, entities
    // unescaped.
    static QString plainText(const QString &markup);
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
    QString playerText(const QString &objectName) const;
    // "時語（sgs1）" -- a script and /players still speak object names.
    QString playerLabel(const QString &objectName) const;
    QString gameStatusText(const QString &status) const;
    // The prompt the desktop client parses as key:src:dest:arg:arg2.
    QString promptText(const QString &prompt) const;
    QString kingdomText(const QVariantMap &player) const;
    QString interactionTitle(const InteractionRequest &request) const;
    QString answerHint(const InteractionRequest &request) const;
    bool m_ansiEnabled = false;
    Resolvers m_resolvers;
};

#endif
