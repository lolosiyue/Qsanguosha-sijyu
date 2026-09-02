#ifndef TUI_APPLICATION_CONTROLLER_H
#define TUI_APPLICATION_CONTROLLER_H

#include "client-live-session.h"
#include "core/client-core.h"
#include "tui-command.h"
#include "tui-input.h"
#include "tui-interaction-view.h"
#include "tui-renderer.h"

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
    QString resolveNameText(const QString &name) const;
    static QString resolveGeneralKingdom(const QString &generalName);
    QString resolveSkillCardWireText(const QString &skillName, int instanceId,
                                     const QList<int> &subcardIds, QString *error) const;
    void fillPlaySkillCandidates(CardInteractionPayload *payload) const;
    QString renderPiles() const;
    QString renderSkills() const;
    QString renderEquipment() const;
    QString renderLog() const;
    QString resolvePlayerName(const QString &objectName) const;
    QString presentationText(int command, const QString &fallbackText,
                             const QVariant &payload) const;
    QStringList completionExtraTokens() const;

    TuiApplicationOptions m_options;
    ClientCore m_core;
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
};

#endif
