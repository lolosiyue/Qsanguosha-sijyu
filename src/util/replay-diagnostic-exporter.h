#ifndef REPLAY_DIAGNOSTIC_EXPORTER_H
#define REPLAY_DIAGNOSTIC_EXPORTER_H

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

class Replayer;

struct ReplayDiagnosticExportRequest
{
    QString replayPath;
    QString manifestPath;
    QStringList snapshotPaths;

    bool includeStateNow = false;
    QJsonObject stateNow;
    QString stateNowOmission;

    bool includeDiagnostics = false;
    QJsonObject diagnostics;
    QString diagnosticsOmission;
};

struct ReplayDiagnosticExportResult
{
    bool success = false;
    QString error;
    QStringList includedFiles;
    QMap<QString, QString> omittedFiles;
};

class ReplayDiagnosticExporter
{
public:
    static QJsonObject createDiagnostics(const Replayer &replayer);
    static ReplayDiagnosticExportResult exportBundle(
        const QString &outputPath,
        const ReplayDiagnosticExportRequest &request);
};

#endif
