#ifndef TUI_APPLICATION_CONTROLLER_H
#define TUI_APPLICATION_CONTROLLER_H

#include "client-live-session.h"
#include "core/client-core.h"
#include "tui-command.h"
#include "tui-input.h"
#include "tui-interaction-view.h"
#include "tui-client-player.h"
#include "tui-renderer.h"
#include "tui-room-context.h"

#include <QFile>
#include <QList>
#include <QVariant>

class TuiScriptRunner;

struct TuiApplicationOptions
{
    ClientLiveSessionOptions session;
    bool ansiEnabled = false;
    QString logFile;
    QString scriptFile;
};

class TuiApplicationController final : public QObject
{
    Q_OBJECT

public:
    explicit TuiApplicationController(const TuiApplicationOptions &options,
                                      QObject *parent = nullptr);
    bool start(QString *error = nullptr);

public slots:
    void handleInputLine(const QString &line);

private:
    void handleCommand(const TuiCommandIntent &intent);
    bool trySkipRoleAssignment();
    void writeOutput(const QString &text);
    void writeError(const QString &text);
    void writeAutomationMarker(const QString &marker);
    bool appendLogLine(const QString &line);
    void requestExit(int code);
    QString resolveCardWireText(int cardId) const;
    QString resolveCardDisplayText(int cardId) const;
    // What the engine says about a candidate for the request being answered.
    // Advisory only: the parser still accepts whatever the player types.
    QString resolveCardHint(int cardId) const;
    QString resolvePlayerHint(const QString &objectName) const;
    // Where the engine would let this card be aimed, before anything is picked.
    TuiRenderer::CardTargets cardTargetAdvice(const Card *card) const;
    TuiRenderer::CardTargets resolveCardTargets(int cardId) const;
    // /hand asks the play-phase question whether or not a prompt is up.
    QString resolveHandCardHint(int cardId) const;
    // The card an answer amounts to: a parsed wire string when the player
    // composed one, otherwise the single card they picked. Null when the answer
    // is not a card play. The caller owns nothing; a parsed virtual is queued
    // for deletion the way tuiResolveSkillCardWireText() queues its own.
    const Card *answerCard(const InteractionResponse &response) const;
    // Runs the engine over a finished play-card answer the way the desktop's
    // OK button does. Returns false with a reason the player can act on.
    // Never consulted unless the player is answering a play-card prompt.
    bool checkPlayAnswer(const InteractionResponse &response, QString *error) const;
    // Stage one of a two-part activation: the skill card is built but has no
    // targets yet. Prints the candidates and remembers the line to replay.
    bool beginTargetStage(const QString &firstLine, const InteractionResponse &response);
    void clearPendingActivation();
    QString resolveNameText(const QString &name) const;
    static QString resolveGeneralKingdom(const QString &generalName);
    QString resolveSkillCardWireText(const QString &skillName, int instanceId,
                                     const QList<int> &subcardIds, QString *error) const;
    void fillSkillCandidates(InteractionType type, CardInteractionPayload *payload) const;
    QString resolveSkillHint(const QString &skillName, int instanceId) const;
    QString renderPiles() const;
    QString renderSkills() const;
    QString renderEquipment() const;
    QString renderLog() const;
    QString resolvePlayerName(const QString &objectName) const;
    QString resolveLogPlayerName(const QString &objectName) const;
    void appendSynthesizedLogs(const QSanProtocol::ProtocolMessage &message);
    QString presentationText(int command, const QString &fallbackText,
                             const QVariant &payload) const;
    QStringList completionExtraTokens() const;

    TuiApplicationOptions m_options;
    ClientCore m_core;
    // Owns the room the engine resolves cards through; no QObject parent,
    // it is a value member the controller destroys itself.
    TuiRoomContext m_roomContext;
    TuiPlayerModel m_players;
    ClientLiveSession m_session;
    TuiRenderer m_renderer;
    TuiInteractionView m_view;
    TuiInput m_input;
    TuiScriptRunner *m_script = nullptr;
    QFile m_log;
    bool m_logFailed = false;
    bool m_trusted = false;
    bool m_exiting = false;
    bool m_gameOverMarked = false;
    QString m_lastMarkedSyncId;
    // The question the active request is asking, kept so the hint resolvers do
    // not have to reach into ClientCore while it is presenting.
    InteractionType m_hintType = InteractionType::None;
    CardUseStruct::CardUseReason m_hintReason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
    // The reason a skill would be activated under, which is not the card-hint
    // reason: prompts that take cards but no skills leave this UNKNOWN.
    CardUseStruct::CardUseReason m_skillReason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
    QString m_hintPattern;
    // Who the active prompt offers as a target, so the engine is only ever
    // asked about players the server already put on the menu.
    QStringList m_hintTargets;
    // A play-card answer waiting for its targets. Holding the player's own
    // first line (rather than the parsed answer) means stage two simply
    // replays "<line> -> <targets>" through the one parser.
    struct PendingActivation
    {
        bool active = false;
        QString firstLine;
    };
    PendingActivation m_pending;
    // The card the last skill activation was built from, valid only for the
    // event handler that built it.
    mutable const Card *m_builtSkillCard = nullptr;
    QList<int> m_renPile;
};

#endif
