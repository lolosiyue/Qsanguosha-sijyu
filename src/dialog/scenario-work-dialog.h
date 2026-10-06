#ifndef QSAN_SCENARIO_WORK_DIALOG_H
#define QSAN_SCENARIO_WORK_DIALOG_H

#include "scenario-work.h"
#include <QDialog>
#include <QJsonObject>

class QListWidget;
class QLineEdit;
class QTextEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class ScenarioWorkLibrary;

class ScenarioWorkEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScenarioWorkEditorDialog(QWidget *parent = nullptr,
        const QJsonObject &compatibility = QJsonObject(), const QJsonObject &rules = QJsonObject());
    void setWork(const ScenarioWork::WorkDefinition &work);
    ScenarioWork::WorkDefinition work() const { return m_work; }

signals:
    void workSaved(const ScenarioWork::WorkDefinition &work);
    void playRequested(const ScenarioWork::WorkLaunch &launch);

private slots:
    void addScene();
    void editScene();
    void removeScene();
    void addEntry();
    void editEntry();
    void importScene();
    void removeEntry();
    void moveEntryUp();
    void moveEntryDown();
    void saveWork();
    void rebindCompatibility();

private:
    void buildUi();
    void refreshScenes();
    void refreshEntries();
    void readFields();
    bool editSceneDefinition(ScenarioWork::SceneDefinition *scene, bool isNew);
    bool editEntryDefinition(ScenarioWork::StageEntry *entry, bool isNew);
    bool editGoal(ScenarioWork::GoalDefinition *goal);

    ScenarioWork::WorkDefinition m_work;
    QJsonObject m_runtimeCompatibility;
    QJsonObject m_runtimeRules;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_author = nullptr;
    QLineEdit *m_rule = nullptr;
    QLineEdit *m_requiredExtensions = nullptr;
    QTextEdit *m_intro = nullptr;
    QComboBox *m_kind = nullptr;
    QComboBox *m_selection = nullptr;
    QCheckBox *m_secondGeneral = nullptr;
    QCheckBox *m_fixedSeats = nullptr;
    QCheckBox *m_carryHp = nullptr;
    QCheckBox *m_carryMaxHp = nullptr;
    QCheckBox *m_carryHujia = nullptr;
    QCheckBox *m_carryGenerals = nullptr;
    QCheckBox *m_carryHand = nullptr;
    QCheckBox *m_carryEquip = nullptr;
    QLineEdit *m_carryMarks = nullptr;
    QLineEdit *m_carrySkills = nullptr;
    QListWidget *m_scenes = nullptr;
    QListWidget *m_entries = nullptr;
};

// Widget view over ScenarioWorkLibrary for builds without the QML home page.
class ScenarioWorkLibraryDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScenarioWorkLibraryDialog(ScenarioWorkLibrary *library, QWidget *parent = nullptr);

private slots:
    void refresh();
    void playWork();

private:
    QString selectedPath() const;

    ScenarioWorkLibrary *m_library = nullptr;
    QListWidget *m_list = nullptr;
};

#endif
