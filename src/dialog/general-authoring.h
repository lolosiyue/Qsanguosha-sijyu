#ifndef GENERAL_AUTHORING_H
#define GENERAL_AUTHORING_H

#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QUrl>

namespace GeneralAuthoring {
constexpr int MaxCodeBytes = 65536;
constexpr int MaxProjectBytes = 2 * 1024 * 1024;
constexpr int MaxVersions = 16;
struct Version { QJsonObject spec; QString code; };
struct Context {
    QString text;
    QSet<QString> globals, methods, luaFunctions, libraryFunctions;
    bool isValid() const { return !text.isEmpty() && !globals.isEmpty() && !methods.isEmpty(); }
};
Context bundledContext();
QJsonObject defaultSpec();
QStringList validateSpec(const QJsonObject &spec, const QSet<QString> &occupied = {});
QString assemble(const QJsonObject &spec, const QString &skills);
QStringList validateCode(const QJsonObject &spec, const QString &code, const Context &context);
QString comparison(const QString &before, const QString &after);
bool validEndpoint(const QUrl &url);
// Sanitized provider-reported counters; absent/unsupported is JSON null, not 0.
// No prices or billed-cost estimates are derived from these counters.
QJsonObject responseUsage(const QJsonObject &envelope);

// All state is inert text. No engine Lua state, evaluation or package-store action.
class Document {
public:
    QJsonObject spec = defaultSpec(), originalSpec;
    QString reviewed, candidate;
    QVector<Version> history;
    QSet<QString> occupied;
    Context context = bundledContext();
    void rememberSecret(const QString &secret);
    bool containsSecret(const QByteArray &bytes) const;
    void setSpec(const QJsonObject &value);
    void setReviewed(const QString &value);
    void checkpoint();
    bool undo();
    QByteArray preview(const QString &model, const QString &instruction, QString *error) const;
    quint64 beginRequest();
    void cancel();
    bool receive(quint64 id, const QByteArray &response, QString *error);
    bool apply(QString *error);
    QStringList diagnostics() const;
    QByteArray project(QString *error) const;
    bool importProject(const QByteArray &bytes, QString *error);
    bool exportDisabled(const QString &parent, const QByteArray &cardPng, QString *path, QString *error) const;
    QJsonObject lastUsage() const { return m_usage; }
    bool busy() const { return m_pending != 0; }
private:
    quint64 m_serial = 0, m_pending = 0, m_revision = 0, m_requestRevision = 0, m_candidateRevision = 0;
    QStringList m_secrets;
    QJsonObject m_usage = responseUsage({});
};
}
#endif
