#include "tui-script-runner.h"

#include "core/client-core.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QRegularExpression>

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

} // namespace

TuiScriptRunner::TuiScriptRunner(ClientCore *core, LineSink sink, QObject *parent)
    : QObject(parent), m_core(core), m_sink(std::move(sink))
{
    m_waitTimer.setSingleShot(true);
    connect(&m_waitTimer, &QTimer::timeout, this, [this]() {
        fail(tr("脚本在第 %1 行等待超时").arg(m_index + 1));
    });
}

bool TuiScriptRunner::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr)
            *error = tr("无法打开脚本：%1").arg(file.errorString());
        return false;
    }
    m_lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    m_index = 0;
    return true;
}

void TuiScriptRunner::start()
{
    QTimer::singleShot(0, this, &TuiScriptRunner::advance);
}

void TuiScriptRunner::setEventFormatter(EventFormatter formatter)
{
    m_eventFormatter = std::move(formatter);
}

void TuiScriptRunner::notifyStateChanged()
{
    if (!m_waitCondition.isEmpty() && conditionMatches(m_waitCondition)) {
        m_waitCondition.clear();
        m_waitTimer.stop();
        ++m_index;
        QTimer::singleShot(0, this, &TuiScriptRunner::advance);
    }
}

bool TuiScriptRunner::conditionMatches(const QStringList &tokens) const
{
    if (m_core == nullptr || tokens.isEmpty())
        return false;
    const ClientGameState *state = m_core->state();
    const QString condition = tokens.first().toLower();
    if (condition == QLatin1String("active"))
        return state->connectionValue(QStringLiteral("state")) == QLatin1String("active");
    if (condition == QLatin1String("game_started"))
        return state->gameValue(QStringLiteral("started")).toBool();
    if (condition == QLatin1String("game_over"))
        return state->gameValue(QStringLiteral("game_over")).toBool();
    if (condition == QLatin1String("sync_complete"))
        return state->connectionValue(QStringLiteral("sync_phase")) == QLatin1String("end");
    if (condition == QLatin1String("interaction") && tokens.size() >= 2)
        return m_core->hasActiveRequest()
            && interactionTypeName(m_core->activeRequest().type).compare(tokens.at(1), Qt::CaseInsensitive) == 0;
    if (condition == QLatin1String("player") && tokens.size() >= 2)
        return state->hasPlayer(tokens.at(1));
    if (condition == QLatin1String("state"))
        return stateValueMatches(tokens.mid(1));
    if (condition == QLatin1String("log"))
        return logContains(tokens.mid(1));
    return false;
}

bool TuiScriptRunner::stateValueMatches(const QStringList &tokens) const
{
    if (m_core == nullptr || tokens.size() < 2)
        return false;

    QVariant current = m_core->state()->toJson().toVariantMap();
    const QStringList path = tokens.first().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (path.isEmpty())
        return false;
    for (const QString &component : path) {
        if (current.metaType().id() == QMetaType::QVariantMap) {
            const QVariantMap map = current.toMap();
            if (!map.contains(component))
                return false;
            current = map.value(component);
            continue;
        }
        if (current.metaType().id() == QMetaType::QVariantList) {
            bool numeric = false;
            const int index = component.toInt(&numeric);
            const QVariantList list = current.toList();
            if (!numeric || index < 0 || index >= list.size())
                return false;
            current = list.at(index);
            continue;
        }
        return false;
    }

    QString actual;
    const QJsonValue json = QJsonValue::fromVariant(current);
    if (json.isObject())
        actual = QString::fromUtf8(QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));
    else if (json.isArray())
        actual = QString::fromUtf8(QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact));
    else if (json.isBool())
        actual = json.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    else if (json.isDouble())
        actual = QString::number(json.toDouble(), 'g', 16);
    else if (json.isNull() || json.isUndefined())
        actual = QStringLiteral("null");
    else
        actual = json.toString();
    return actual == tokens.mid(1).join(QLatin1Char(' '));
}

bool TuiScriptRunner::logContains(const QStringList &tokens) const
{
    if (m_core == nullptr || tokens.isEmpty())
        return false;
    const QString expected = tokens.join(QLatin1Char(' '));
    const QVariantList events = m_core->state()->presentationEvents();
    // Match what the player sees as well as the core's untranslated fallback.
    for (const QVariant &entry : events) {
        const QVariantMap event = entry.toMap();
        const QString fallback = event.value(QStringLiteral("text")).toString();
        if (fallback.contains(expected))
            return true;
        if (!m_eventFormatter)
            continue;
        const QString rendered = m_eventFormatter(
            event.value(QStringLiteral("command")).toInt(), fallback,
            event.value(QStringLiteral("payload")));
        if (rendered.contains(expected))
            return true;
    }
    return false;
}

bool TuiScriptRunner::assertCondition(const QStringList &tokens, QString *error) const
{
    if (conditionMatches(tokens))
        return true;
    if (error != nullptr)
        *error = tr("脚本断言失败：%1").arg(tokens.join(QLatin1Char(' ')));
    return false;
}

void TuiScriptRunner::advance()
{
    while (m_index < m_lines.size()) {
        const QString line = m_lines.at(m_index).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            ++m_index;
            continue;
        }
        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (tokens.first().compare(QStringLiteral("wait"), Qt::CaseInsensitive) == 0) {
            QStringList condition = tokens.mid(1);
            int timeoutMs = 30000;
            if (!condition.isEmpty()) {
                bool numeric = false;
                const int parsed = condition.last().toInt(&numeric);
                if (numeric) {
                    timeoutMs = parsed;
                    condition.removeLast();
                }
            }
            if (conditionMatches(condition)) {
                ++m_index;
                continue;
            }
            m_waitCondition = condition;
            m_waitTimer.start(qMax(1, timeoutMs));
            return;
        }
        if (tokens.first().compare(QStringLiteral("assert"), Qt::CaseInsensitive) == 0) {
            QString error;
            if (!assertCondition(tokens.mid(1), &error)) {
                fail(tr("第 %1 行：%2").arg(m_index + 1).arg(error));
                return;
            }
            ++m_index;
            continue;
        }
        if (m_sink)
            m_sink(line);
        ++m_index;
        QTimer::singleShot(0, this, &TuiScriptRunner::advance);
        return;
    }
    emit finished();
}

void TuiScriptRunner::fail(const QString &message)
{
    m_waitTimer.stop();
    m_waitCondition.clear();
    emit scriptError(message);
}
