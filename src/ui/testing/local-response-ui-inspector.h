#ifndef LOCAL_RESPONSE_UI_INSPECTOR_H
#define LOCAL_RESPONSE_UI_INSPECTOR_H

#include <QWidget>

class QLabel;
class QPushButton;

class LocalResponseUiInspector final : public QWidget
{
public:
    explicit LocalResponseUiInspector(QWidget *parent = nullptr);

    void setCaseName(const QString &value);
    void setMode(const QString &value);
    void setRequest(const QString &command, int serial);
    void setClientState(const QString &status, const QString &pattern);
    void setPresentationResult(const QString &value);
    void setReply(bool received, const QString &command = QString(),
        const QString &body = QString());
    void setFinalResult(const QString &value);

    QPushButton *nextActionButton() const;
    QPushButton *remainingActionsButton() const;
    QPushButton *snapshotButton() const;
    QPushButton *screenshotButton() const;
    QPushButton *closeButton() const;

private:
    QLabel *m_caseValue;
    QLabel *m_modeValue;
    QLabel *m_requestValue;
    QLabel *m_clientValue;
    QLabel *m_presentationValue;
    QLabel *m_replyReceivedValue;
    QLabel *m_replyCommandValue;
    QLabel *m_replyBodyValue;
    QLabel *m_finalValue;
    QPushButton *m_nextActionButton;
    QPushButton *m_remainingActionsButton;
    QPushButton *m_snapshotButton;
    QPushButton *m_screenshotButton;
    QPushButton *m_closeButton;
};

#endif
