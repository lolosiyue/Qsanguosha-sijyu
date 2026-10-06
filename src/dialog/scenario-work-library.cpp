#include "scenario-work-library.h"
#include "scenario-work-dialog.h"
#include "scenario-work-examples.h"
#include "work-scenario.h"

#include <QApplication>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace {
QWidget *dialogParent()
{
    return QApplication::activeWindow();
}

QString kindText(ScenarioWork::WorkKind kind)
{
    return kind == ScenarioWork::WorkKind::Stage ? QObject::tr("Stage") : QObject::tr("Scene");
}

const ScenarioWork::ProgressSnapshot *latestSnapshot(
    const ScenarioWork::WorkProgress &progress, const QString &entryId)
{
    const ScenarioWork::ProgressSnapshot *latest = nullptr;
    for (const auto &snapshot : progress.snapshots)
        if (snapshot.entryId == entryId && (!latest || snapshot.createdAt >= latest->createdAt))
            latest = &snapshot;
    return latest;
}

struct EntryState {
    QString label;
    ScenarioWork::CarryState carry;
};

// The original setup, then the latest snapshot, then every snapshot of the entry.
QList<EntryState> entryStates(const ScenarioWork::WorkProgress &progress, const QString &entryId)
{
    QList<EntryState> states { { ScenarioWorkLibrary::tr("Original entry"), ScenarioWork::CarryState() } };
    if (const auto *latest = latestSnapshot(progress, entryId))
        states.append({ ScenarioWorkLibrary::tr("Latest snapshot (%1)")
                            .arg(latest->createdAt.toString(Qt::ISODate)),
            latest->carry });
    for (const auto &snapshot : progress.snapshots)
        if (snapshot.entryId == entryId)
            states.append({ QStringLiteral("%1  %2").arg(snapshot.createdAt.toString(Qt::ISODate), snapshot.id),
                snapshot.carry });
    return states;
}
}

QString ScenarioWorkLibrary::requiredExtensionNames(const QJsonObject &compatibility)
{
    QStringList names;
    // Display the work's saved requirements, including incompatible imports.
    for (const auto &value : compatibility.value(QStringLiteral("extensions")).toArray())
        if (value.isString())
            names << value.toString();
    return names.join(QStringLiteral(", "));
}

ScenarioWorkLibrary::ScenarioWorkLibrary(const QString &libraryRoot, QObject *parent)
    : QObject(parent)
    , m_libraryRoot(libraryRoot)
{
}

void ScenarioWorkLibrary::open()
{
    m_runtimeCompatibility = QSanWorks::currentCompatibility();
    if (ScenarioWork::listWorks(m_libraryRoot).isEmpty()) {
        for (const auto &example : scenarioWorkExamples(m_runtimeCompatibility)) {
            QString error;
            if (!ScenarioWork::writeWork(m_libraryRoot, example, &error)) {
                QMessageBox::warning(dialogParent(), tr("Cannot save example work"), error);
                break;
            }
        }
    }
    reload();
    if (m_hasTrialDraft)
        QTimer::singleShot(0, this, &ScenarioWorkLibrary::resumeTrialDraft);
}

void ScenarioWorkLibrary::reload()
{
    m_works.clear();
    for (const auto &info : ScenarioWork::listWorks(m_libraryRoot)) {
        m_works << QVariantMap { { QStringLiteral("path"), info.path }, { QStringLiteral("title"), info.title },
            { QStringLiteral("kind"), kindText(info.kind) }, { QStringLiteral("revision"), info.revision } };
    }
    emit worksChanged();
}

QVariantMap ScenarioWorkLibrary::details(const QString &path) const
{
    bool ok = false;
    const auto work = readWork(path, &ok);
    if (!ok)
        return QVariantMap();
    ScenarioWork::WorkProgress progress;
    QString progressError;
    if (!ScenarioWork::loadProgress(m_libraryRoot, work, &progress, &progressError) && progressError.isEmpty())
        progressError = tr("Cannot read progress");
    QVariantList entries;
    for (const auto &entry : work.entries) {
        QString sceneIntro;
        for (const auto &scene : work.scenes)
            if (scene.id == entry.sceneId && scene.revision == entry.sceneRevision) {
                sceneIntro = scene.intro;
                break;
            }
        const auto states = entryStates(progress, entry.id);
        QStringList labels;
        for (const auto &state : states)
            labels << state.label;
        entries << QVariantMap { { QStringLiteral("id"), entry.id }, { QStringLiteral("title"), entry.title },
            { QStringLiteral("intro"), entry.intro }, { QStringLiteral("sceneIntro"), sceneIntro },
            { QStringLiteral("locked"), !ScenarioWork::canPlayEntry(work, progress, entry.id) },
            { QStringLiteral("continuation"), entry.id == progress.continuationEntryId },
            { QStringLiteral("states"), labels }, { QStringLiteral("defaultState"), labels.size() > 1 ? 1 : 0 } };
    }
    return QVariantMap { { QStringLiteral("path"), path }, { QStringLiteral("title"), work.title },
        { QStringLiteral("author"), work.author }, { QStringLiteral("revision"), work.revision },
        { QStringLiteral("intro"), work.intro }, { QStringLiteral("kind"), kindText(work.kind) },
        { QStringLiteral("rule"), work.rule },
        { QStringLiteral("extensions"), requiredExtensionNames(work.compatibility) },
        { QStringLiteral("compatibilityError"), compatibilityError(work) },
        { QStringLiteral("progressError"), progressError },
        { QStringLiteral("canContinue"), !progress.continuationEntryId.isEmpty() },
        { QStringLiteral("entries"), entries } };
}

ScenarioWork::WorkDefinition ScenarioWorkLibrary::readWork(const QString &path, bool *ok) const
{
    ScenarioWork::WorkDefinition work;
    QString error;
    *ok = !path.isEmpty() && ScenarioWork::readWork(path, &work, &error);
    return work;
}

bool ScenarioWorkLibrary::writeWork(const ScenarioWork::WorkDefinition &work)
{
    QString error;
    if (!ScenarioWork::writeWork(m_libraryRoot, work, &error)) {
        QMessageBox::warning(dialogParent(), tr("Cannot save work"), error);
        return false;
    }
    reload();
    auto written = work;
    if (written.revision.isEmpty())
        written.revision = ScenarioWork::computeRevision(written);
    emit workWritten(ScenarioWork::workFilePath(m_libraryRoot, written));
    return true;
}

void ScenarioWorkLibrary::openEditor(const ScenarioWork::WorkDefinition &work)
{
    ScenarioWorkEditorDialog dialog(dialogParent(), m_runtimeCompatibility, work.rules);
    dialog.setWork(work);
    connect(&dialog, &ScenarioWorkEditorDialog::workSaved, this,
        [this, &dialog](const auto &saved) { dialog.setProperty("saveSucceeded", writeWork(saved)); });
    bool trialRequested = false;
    connect(&dialog, &ScenarioWorkEditorDialog::playRequested, this,
        [this, &trialRequested](const ScenarioWork::WorkLaunch &launch) {
            trialRequested = true;
            m_trialDraft = launch.work;
            m_hasTrialDraft = true;
            emit playRequested(launch);
        });
    dialog.exec();
    if (!trialRequested)
        m_hasTrialDraft = false;
}

void ScenarioWorkLibrary::resumeTrialDraft()
{
    if (m_hasTrialDraft)
        openEditor(m_trialDraft);
}

void ScenarioWorkLibrary::newSceneWork()
{
    auto work = ScenarioWork::defaultWork();
    work.compatibility = m_runtimeCompatibility;
    openEditor(work);
}

void ScenarioWorkLibrary::newStageWork()
{
    auto work = ScenarioWork::defaultWork();
    work.compatibility = m_runtimeCompatibility;
    work.kind = ScenarioWork::WorkKind::Stage;
    openEditor(work);
}

void ScenarioWorkLibrary::editWork(const QString &path)
{
    bool ok = false;
    const auto work = readWork(path, &ok);
    if (ok)
        openEditor(work);
}

void ScenarioWorkLibrary::duplicateWork(const QString &path)
{
    bool ok = false;
    auto work = readWork(path, &ok);
    if (!ok)
        return;
    work.id = QUuid::createUuid().toString().mid(1, 36);
    work.revision.clear();
    work.title += tr(" (Copy)");
    writeWork(work);
}

void ScenarioWorkLibrary::importWork()
{
    const QString path = QFileDialog::getOpenFileName(
        dialogParent(), tr("Import work"), QString(), tr("Work files (*.qswork.json)"));
    if (path.isEmpty())
        return;
    ScenarioWork::WorkDefinition work;
    QString error;
    if (!ScenarioWork::readWork(path, &work, &error)) {
        QMessageBox::warning(dialogParent(), tr("Invalid work"), error);
        return;
    }
    writeWork(work);
}

void ScenarioWorkLibrary::exportWork(const QString &path)
{
    bool ok = false;
    const auto work = readWork(path, &ok);
    if (!ok)
        return;
    const QString target = QFileDialog::getSaveFileName(
        dialogParent(), tr("Export work"), QString(), tr("Work files (*.qswork.json)"));
    if (target.isEmpty())
        return;
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(dialogParent(), tr("Cannot export work"), file.errorString());
        return;
    }
    const QByteArray bytes = QJsonDocument(ScenarioWork::workToJson(work)).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(dialogParent(), tr("Cannot export work"), file.errorString());
}

QString ScenarioWorkLibrary::compatibilityError(const ScenarioWork::WorkDefinition &work) const
{
    if (work.compatibility == m_runtimeCompatibility)
        return QString();
    return tr("This work was created for a different runtime manifest or card catalog. Saved "
              "compatibility:\n%1\nCurrent compatibility:\n%2")
        .arg(QString::fromUtf8(QJsonDocument(work.compatibility).toJson(QJsonDocument::Indented)),
            QString::fromUtf8(QJsonDocument(m_runtimeCompatibility).toJson(QJsonDocument::Indented)));
}

bool ScenarioWorkLibrary::loadProgress(
    const ScenarioWork::WorkDefinition &work, ScenarioWork::WorkProgress *progress)
{
    QString error;
    if (ScenarioWork::loadProgress(m_libraryRoot, work, progress, &error))
        return true;
    QMessageBox::warning(dialogParent(), tr("Cannot read progress"), error);
    return false;
}

void ScenarioWorkLibrary::launch(const ScenarioWork::WorkDefinition &work, const QString &entryId,
    const ScenarioWork::CarryState &carry)
{
    if (entryId.isEmpty())
        return;
    ScenarioWork::WorkLaunch launch;
    launch.work = work;
    launch.entryId = entryId;
    launch.carry = carry;
    emit playRequested(launch);
}

void ScenarioWorkLibrary::playEntry(const QString &path, const QString &entryId, int stateIndex)
{
    bool ok = false;
    const auto work = readWork(path, &ok);
    if (!ok)
        return;
    ScenarioWork::WorkProgress progress;
    if (!loadProgress(work, &progress))
        return;
    const QString reason = compatibilityError(work);
    if (!reason.isEmpty()) {
        QMessageBox::warning(dialogParent(), tr("Incompatible work"), reason);
        return;
    }
    if (!ScenarioWork::canPlayEntry(work, progress, entryId)) {
        QMessageBox::warning(dialogParent(), tr("Entry locked"), tr("Complete the preceding stage first."));
        return;
    }
    const auto states = entryStates(progress, entryId);
    if (stateIndex >= 0 && stateIndex < states.size())
        launch(work, entryId, states.at(stateIndex).carry);
}

void ScenarioWorkLibrary::continueWork(const QString &path)
{
    bool ok = false;
    const auto work = readWork(path, &ok);
    if (!ok)
        return;
    const QString reason = compatibilityError(work);
    if (!reason.isEmpty()) {
        QMessageBox::warning(dialogParent(), tr("Incompatible work"), reason);
        return;
    }
    ScenarioWork::WorkProgress progress;
    if (!loadProgress(work, &progress))
        return;
    if (progress.continuationEntryId.isEmpty()) {
        QMessageBox::information(dialogParent(), tr("Continue"), tr("No continuation is available."));
        return;
    }
    const auto *latest = latestSnapshot(progress, progress.continuationEntryId);
    launch(work, progress.continuationEntryId, latest ? latest->carry : ScenarioWork::CarryState());
}
