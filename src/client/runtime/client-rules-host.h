#ifndef CLIENT_RULES_HOST_H
#define CLIENT_RULES_HOST_H

#include "client-rules-session.h"
#include "client-rules-ingress.h"
#include <QString>
#include <memory>

class QCoreApplication;

// The production C ABI and the native lifecycle probe use this SAME host.
// Paths are constructor-only deployment configuration, never Worker input.
// One host owns one Engine/QCoreApplication on one serialized thread. Shutdown
// is terminal: create a fresh Worker/process rather than restarting globals.
class ClientRulesHost final
{
public:
    ClientRulesHost(const QString &assets, const QString &work, const QString &userData);
    ~ClientRulesHost();
    ClientRulesHost(const ClientRulesHost &) = delete;
    ClientRulesHost &operator=(const ClientRulesHost &) = delete;

    int initialize();
    int evaluate();
    int shutdown();
    // Additive stream envelope v1. No change to the existing W2 bridge exports.
    int stream();

private:
    enum class Phase { New, Ready, Closed };
    int fail(const QString &path, const QString &reason, int status);
    void releaseEngine();
    QString file(const char *name) const;

    QString m_assets, m_work, m_userData;
    Phase m_phase = Phase::New;
    bool m_ownsEngine = false;
    int m_argc = 1;
    char m_name[18] = "qsanguosha_client";
    char *m_argv[2] = {m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_application;
    ClientRulesSession m_session;
    ClientRulesIngress m_ingress;
    bool m_streamEnabled = false;
};

#endif
