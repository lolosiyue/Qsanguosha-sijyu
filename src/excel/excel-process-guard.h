#ifndef QSANGUOSHA_EXCEL_PROCESS_GUARD_H
#define QSANGUOSHA_EXCEL_PROCESS_GUARD_H

#include <QObject>
#include <QJsonObject>
#include <QString>

class QTimer;

class ExcelProcessGuard : public QObject
{
    Q_OBJECT
public:
    explicit ExcelProcessGuard(QObject *parent = nullptr);
    ~ExcelProcessGuard() override;

    bool initialize(qint64 parentPid, const QString &bootstrapPath, const QString &nonce,
                    const QString &session, const QString &token, const QString &runtimeTier,
                    int maxPlayers, QString *error = nullptr);
    bool writeReady(quint16 port, QString *error = nullptr);
    bool writeError(const QString &safeCode, QString *error = nullptr);
    bool validateParent(QString *error = nullptr) const;
    bool parentAlive() const;
    bool startMonitoring(int intervalMs = 1000);

    static QString generateToken();
    static QString generateSession();

signals:
    void parentDied();

private slots:
    void checkParent();

private:
    bool writeBootstrap(const QJsonObject &object, QString *error);
    qint64 m_parentPid = 0;
    quint64 m_parentCreated = 0;
    QString m_bootstrapPath;
    QString m_nonce;
    QString m_session;
    QString m_token;
    QString m_runtimeTier;
    int m_maxPlayers = 0;
    QTimer *m_timer = nullptr;
#ifdef Q_OS_WIN
    void *m_parentHandle = nullptr;
    void *m_watchThread = nullptr;
    void *m_stopEvent = nullptr;
    static unsigned long __stdcall parentWaitThread(void *context);
    bool applyCurrentUserAcl(const QString &directory, QString *error) const;
    bool isExcelParent(QString *error) const;
#endif
};

#endif
