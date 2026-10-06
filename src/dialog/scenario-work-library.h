#ifndef QSAN_SCENARIO_WORK_LIBRARY_H
#define QSAN_SCENARIO_WORK_LIBRARY_H

#include "scenario-work.h"
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Shared work-library backend for ScenarioWorkLibraryDialog and the home ScenarioWorksScene.qml.
// Works are addressed by file path. Message boxes, file pickers and the work editor use the
// active window as their parent.
class ScenarioWorkLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList works READ works NOTIFY worksChanged)
public:
    explicit ScenarioWorkLibrary(const QString &libraryRoot, QObject *parent = nullptr);
    static QString requiredExtensionNames(const QJsonObject &compatibility);

    // Rows of {path, title, kind, revision}.
    QVariantList works() const { return m_works; }

    // Refresh the runtime compatibility, seed examples into an empty library, reload, and
    // reopen the editor of a work left for trial play. Call before showing either view.
    void open();
    Q_INVOKABLE void reload();
    // Work metadata, compatibility, continuation and entries with lock state and initial-state labels.
    Q_INVOKABLE QVariantMap details(const QString &path) const;

    Q_INVOKABLE void newSceneWork();
    Q_INVOKABLE void newStageWork();
    Q_INVOKABLE void editWork(const QString &path);
    Q_INVOKABLE void duplicateWork(const QString &path);
    Q_INVOKABLE void importWork();
    Q_INVOKABLE void exportWork(const QString &path);
    // stateIndex selects from the entry's initial-state labels in details().
    Q_INVOKABLE void playEntry(const QString &path, const QString &entryId, int stateIndex);
    Q_INVOKABLE void continueWork(const QString &path);

signals:
    void worksChanged();
    void workWritten(const QString &path);
    void playRequested(const ScenarioWork::WorkLaunch &launch);

private:
    ScenarioWork::WorkDefinition readWork(const QString &path, bool *ok) const;
    bool writeWork(const ScenarioWork::WorkDefinition &work);
    void openEditor(const ScenarioWork::WorkDefinition &work);
    void resumeTrialDraft();
    QString compatibilityError(const ScenarioWork::WorkDefinition &work) const;
    bool loadProgress(const ScenarioWork::WorkDefinition &work, ScenarioWork::WorkProgress *progress);
    void launch(const ScenarioWork::WorkDefinition &work, const QString &entryId,
        const ScenarioWork::CarryState &carry);

    QString m_libraryRoot;
    QJsonObject m_runtimeCompatibility;
    QVariantList m_works;
    ScenarioWork::WorkDefinition m_trialDraft;
    bool m_hasTrialDraft = false;
};

#endif
