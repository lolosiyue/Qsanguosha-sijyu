#include "scenario-work-dialog.h"
#include "customassigndialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextEdit>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QString requiredExtensionNames(const QJsonObject &compatibility)
{
    QStringList names;
    // Display the work's saved requirements, including incompatible imports.
    for (const auto &value : compatibility.value(QStringLiteral("extensions")).toArray())
        if (value.isString())
            names << value.toString();
    return names.join(QStringLiteral(", "));
}

QString kindText(ScenarioWork::WorkKind kind)
{
    return kind == ScenarioWork::WorkKind::Stage ? QObject::tr("Stage") : QObject::tr("Scene");
}

QComboBox *predicateTypeBox(QWidget *parent)
{
    auto *box = new QComboBox(parent);
    box->addItem(QObject::tr("Alive"), int(ScenarioWork::PredicateType::Alive));
    box->addItem(QObject::tr("Dead"), int(ScenarioWork::PredicateType::Dead));
    box->addItem(QObject::tr("HP"), int(ScenarioWork::PredicateType::Hp));
    box->addItem(QObject::tr("Mark"), int(ScenarioWork::PredicateType::Mark));
    box->addItem(QObject::tr("Completed player turns"), int(ScenarioWork::PredicateType::Turns));
    return box;
}

QComboBox *predicateOpBox(QWidget *parent)
{
    auto *box = new QComboBox(parent);
    box->addItem(QObject::tr("<"), int(ScenarioWork::PredicateOp::Lt));
    box->addItem(QObject::tr("≤"), int(ScenarioWork::PredicateOp::Le));
    box->addItem(QObject::tr("="), int(ScenarioWork::PredicateOp::Eq));
    box->addItem(QObject::tr("≥"), int(ScenarioWork::PredicateOp::Ge));
    box->addItem(QObject::tr(">"), int(ScenarioWork::PredicateOp::Gt));
    return box;
}
}

ScenarioWorkEditorDialog::ScenarioWorkEditorDialog(
    QWidget *parent, const QJsonObject &compatibility, const QJsonObject &rules)
    : QDialog(parent)
    , m_runtimeCompatibility(compatibility)
    , m_runtimeRules(rules)
{
    m_work = ScenarioWork::defaultWork();
    buildUi();
    setWork(m_work);
}

void ScenarioWorkEditorDialog::buildUi()
{
    setWindowTitle(tr("Work editor"));
    resize(980, 700);
    m_title = new QLineEdit;
    m_author = new QLineEdit;
    m_rule = new QLineEdit;
    m_rule->setReadOnly(true);
    m_requiredExtensions = new QLineEdit;
    m_requiredExtensions->setReadOnly(true);
    m_intro = new QTextEdit;
    m_intro->setAcceptRichText(false);
    m_kind = new QComboBox;
    m_kind->addItem(tr("Scene work"), int(ScenarioWork::WorkKind::Scene));
    m_kind->addItem(tr("Stage work"), int(ScenarioWork::WorkKind::Stage));
    m_selection = new QComboBox;
    m_selection->addItem(tr("Sequential unlock"), int(ScenarioWork::SelectionPolicy::Sequential));
    m_selection->addItem(tr("Free choice"), int(ScenarioWork::SelectionPolicy::Free));
    m_secondGeneral = new QCheckBox(tr("Allow second general"));
    m_fixedSeats = new QCheckBox(tr("Fixed seats and roles"));
    m_fixedSeats->setChecked(true);
    m_fixedSeats->setEnabled(false);

    auto *metadata = new QFormLayout;
    metadata->addRow(tr("Title"), m_title);
    metadata->addRow(tr("Author"), m_author);
    metadata->addRow(tr("Rule"), m_rule);
    metadata->addRow(tr("Required extensions"), m_requiredExtensions);
    metadata->addRow(tr("Introduction"), m_intro);
    metadata->addRow(tr("Work type"), m_kind);
    metadata->addRow(tr("Stage selection"), m_selection);
    metadata->addRow(tr("Rule identity"), m_secondGeneral);
    metadata->addRow(QString(), m_fixedSeats);
    auto *rebind = new QPushButton(tr("Use current runtime manifest and card catalog"));
    metadata->addRow(QString(), rebind);
    connect(rebind, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::rebindCompatibility);

    auto *carry = new QGroupBox(tr("Carry state policy"));
    auto *carryLayout = new QGridLayout(carry);
    m_carryHp = new QCheckBox(tr("HP"));
    m_carryMaxHp = new QCheckBox(tr("Max HP"));
    m_carryHujia = new QCheckBox(tr("Hujia"));
    m_carryGenerals = new QCheckBox(tr("Generals"));
    m_carryHand = new QCheckBox(tr("Hand"));
    m_carryEquip = new QCheckBox(tr("Equipment"));
    m_carryMarks = new QLineEdit;
    m_carryMarks->setPlaceholderText(tr("comma-separated marks"));
    m_carrySkills = new QLineEdit;
    m_carrySkills->setPlaceholderText(tr("comma-separated acquired skills"));
    carryLayout->addWidget(m_carryHp, 0, 0);
    carryLayout->addWidget(m_carryMaxHp, 0, 1);
    carryLayout->addWidget(m_carryHujia, 0, 2);
    carryLayout->addWidget(m_carryGenerals, 0, 3);
    carryLayout->addWidget(m_carryHand, 1, 0);
    carryLayout->addWidget(m_carryEquip, 1, 1);
    carryLayout->addWidget(new QLabel(tr("Marks")), 1, 2);
    carryLayout->addWidget(m_carryMarks, 1, 3);
    carryLayout->addWidget(new QLabel(tr("Skills")), 2, 0);
    carryLayout->addWidget(m_carrySkills, 2, 1, 1, 3);

    m_scenes = new QListWidget;
    m_entries = new QListWidget;
    auto *sceneButtons = new QHBoxLayout;
    auto *newScene = new QPushButton(tr("Add scene"));
    auto *importScene = new QPushButton(tr("Import existing scene"));
    auto *editSceneButton = new QPushButton(tr("Edit"));
    auto *removeSceneButton = new QPushButton(tr("Remove"));
    sceneButtons->addWidget(newScene);
    sceneButtons->addWidget(importScene);
    sceneButtons->addWidget(editSceneButton);
    sceneButtons->addWidget(removeSceneButton);
    auto *entryButtons = new QHBoxLayout;
    auto *newEntry = new QPushButton(tr("Add stage entry"));
    auto *removeEntryButton = new QPushButton(tr("Remove"));
    auto *up = new QPushButton(tr("Move up"));
    auto *down = new QPushButton(tr("Move down"));
    auto *editEntryButton = new QPushButton(tr("Edit entry / pinned revision"));
    entryButtons->addWidget(editEntryButton);
    connect(editEntryButton, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::editEntry);
    entryButtons->addWidget(newEntry);
    entryButtons->addWidget(removeEntryButton);
    entryButtons->addWidget(up);
    entryButtons->addWidget(down);
    auto *lists = new QSplitter;
    auto *scenePage = new QWidget;
    auto *sceneLayout = new QVBoxLayout(scenePage);
    sceneLayout->addWidget(new QLabel(tr("Pinned scene revisions")));
    sceneLayout->addWidget(m_scenes);
    sceneLayout->addLayout(sceneButtons);
    auto *entryPage = new QWidget;
    auto *entryLayout = new QVBoxLayout(entryPage);
    entryLayout->addWidget(new QLabel(tr("Ordered stage entries")));
    entryLayout->addWidget(m_entries);
    entryLayout->addLayout(entryButtons);
    lists->addWidget(scenePage);
    lists->addWidget(entryPage);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto *trial = new QPushButton(tr("Trial launch"));
    buttons->addButton(trial, QDialogButtonBox::ActionRole);
    connect(newScene, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::addScene);
    connect(importScene, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::importScene);
    connect(editSceneButton, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::editScene);
    connect(removeSceneButton, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::removeScene);
    connect(newEntry, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::addEntry);
    connect(removeEntryButton, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::removeEntry);
    connect(up, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::moveEntryUp);
    connect(down, &QPushButton::clicked, this, &ScenarioWorkEditorDialog::moveEntryDown);
    connect(buttons, &QDialogButtonBox::accepted, this, &ScenarioWorkEditorDialog::saveWork);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(trial, &QPushButton::clicked, this, [this] {
        readFields();
        const int row = m_entries->currentRow();
        if (row < 0) {
            QMessageBox::information(this, tr("Trial launch"), tr("Select a stage entry first."));
            return;
        }
        QStringList errors;
        if (!ScenarioWork::validateWork(m_work, &errors)) {
            QMessageBox::warning(this, tr("Invalid work"), errors.join('\n'));
            return;
        }
        m_work.revision = ScenarioWork::computeRevision(m_work);
        ScenarioWork::WorkLaunch launch;
        launch.work = m_work;
        launch.entryId = m_work.entries.at(row).id;
        launch.trial = true;
        accept();
        emit playRequested(launch);
    });
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(metadata);
    layout->addWidget(carry);
    layout->addWidget(lists, 1);
    layout->addWidget(buttons);
}

void ScenarioWorkEditorDialog::setWork(const ScenarioWork::WorkDefinition &work)
{
    m_work = work;
    m_rule->setText(work.rule);
    m_requiredExtensions->setText(requiredExtensionNames(work.compatibility));
    m_title->setText(work.title);
    m_author->setText(work.author);
    m_intro->setPlainText(work.intro);
    m_kind->setCurrentIndex(m_kind->findData(int(work.kind)));
    m_selection->setCurrentIndex(m_selection->findData(int(work.selection)));
    m_carryHp->setChecked(work.carry.hp);
    m_carryMaxHp->setChecked(work.carry.maxhp);
    m_carryHujia->setChecked(work.carry.hujia);
    m_carryGenerals->setChecked(work.carry.generals);
    m_carryHand->setChecked(work.carry.hand);
    m_carryEquip->setChecked(work.carry.equip);
    m_carryMarks->setText(work.carry.marks.join(','));
    m_carrySkills->setText(work.carry.skills.join(','));
    m_secondGeneral->setChecked(work.rules.value(QStringLiteral("secondGeneral")).toBool());
    m_fixedSeats->setChecked(true);
    refreshScenes();
    refreshEntries();
    if (m_scenes->count())
        m_scenes->setCurrentRow(0);
    if (m_entries->count())
        m_entries->setCurrentRow(0);
}

void ScenarioWorkEditorDialog::readFields()
{
    m_work.title = m_title->text().trimmed();
    m_work.author = m_author->text().trimmed();
    m_work.intro = m_intro->toPlainText();
    m_work.kind = ScenarioWork::WorkKind(m_kind->currentData().toInt());
    m_work.selection = ScenarioWork::SelectionPolicy(m_selection->currentData().toInt());
    if (m_work.compatibility.isEmpty())
        m_work.compatibility = m_runtimeCompatibility;
    if (m_work.rules.isEmpty())
        m_work.rules = m_runtimeRules;
    m_work.rules.insert(QStringLiteral("secondGeneral"), m_secondGeneral->isChecked());
    m_work.rules.insert(QStringLiteral("fixedSeats"), m_fixedSeats->isChecked());
    m_work.carry.hp = m_carryHp->isChecked();
    m_work.carry.maxhp = m_carryMaxHp->isChecked();
    m_work.carry.hujia = m_carryHujia->isChecked();
    m_work.carry.generals = m_carryGenerals->isChecked();
    m_work.carry.hand = m_carryHand->isChecked();
    m_work.carry.equip = m_carryEquip->isChecked();
    m_work.carry.marks = m_carryMarks->text().split(',', Qt::SkipEmptyParts);
    m_work.carry.skills = m_carrySkills->text().split(',', Qt::SkipEmptyParts);
    for (QString &mark : m_work.carry.marks)
        mark = mark.trimmed();
    for (QString &skill : m_work.carry.skills)
        skill = skill.trimmed();
    m_work.carry.marks.removeAll(QString());
    m_work.carry.skills.removeAll(QString());
    m_work.carry.marks.removeDuplicates();
    m_work.carry.skills.removeDuplicates();
}

void ScenarioWorkEditorDialog::refreshScenes()
{
    m_scenes->clear();
    for (const auto &scene : m_work.scenes)
        m_scenes->addItem(QStringLiteral("%1  [%2]").arg(scene.title, scene.revision.left(12)));
}

void ScenarioWorkEditorDialog::refreshEntries()
{
    m_entries->clear();
    for (const auto &entry : m_work.entries)
        m_entries->addItem(
            QStringLiteral("%1  →  %2 [%3]").arg(entry.title, entry.sceneId, entry.sceneRevision.left(12)));
}

bool ScenarioWorkEditorDialog::editSceneDefinition(ScenarioWork::SceneDefinition *scene, bool isNew)
{
    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? tr("Add scene") : tr("Edit scene"));
    auto *title = new QLineEdit(scene->title);
    auto *author = new QLineEdit(scene->author);
    auto *intro = new QTextEdit;
    intro->setPlainText(scene->intro);
    auto *opening = new QTextEdit;
    opening->setPlainText(scene->opening);
    auto *ending = new QTextEdit;
    ending->setPlainText(scene->ending);
    auto *seat = new QSpinBox;
    seat->setRange(0, 9);
    seat->setValue(scene->playerSeat);
    auto *setup = new QPushButton(tr("Edit initial scene configuration"));
    auto *goals = new QPushButton(tr("Configure success/failure goals"));
    auto *useGoal = new QCheckBox(tr("Use custom goals"));
    useGoal->setChecked(!scene->goals.isEmpty());
    auto goal = scene->goals.value(0);
    QString document = scene->setup;
    auto *form = new QFormLayout(&dialog);
    form->addRow(tr("Title"), title);
    form->addRow(tr("Author"), author);
    form->addRow(tr("Introduction"), intro);
    form->addRow(tr("Opening"), opening);
    form->addRow(tr("Ending"), ending);
    form->addRow(tr("Player seat (zero based)"), seat);
    form->addRow(setup);
    form->addRow(useGoal);
    form->addRow(goals);
    connect(setup, &QPushButton::clicked, &dialog, [&] {
        CustomAssignDialog editor(&dialog, true);
        QString error;
        if (!document.isEmpty() && !editor.loadDocument(document, &error)) {
            QMessageBox::warning(&dialog, tr("Invalid scene"), error);
            return;
        }
        if (editor.exec() == QDialog::Accepted && !editor.exportDocument(&document, &error))
            QMessageBox::warning(&dialog, tr("Invalid scene"), error);
    });
    connect(goals, &QPushButton::clicked, &dialog, [&] {
        if (editGoal(&goal))
            useGoal->setChecked(true);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        CustomAssignDialog validator(&dialog, true);
        QString error;
        if (document.isEmpty() || (document != scene->setup && !validator.loadDocument(document, &error))) {
            QMessageBox::warning(&dialog, tr("Invalid scene"),
                error.isEmpty() ? tr("Configure the initial scene first.") : error);
            return;
        }
        ScenarioWork::LegacySceneDocument parsed;
        if (!ScenarioWork::parseLegacyScene(document, &parsed, &error)
            || seat->value() >= parsed.players.size()) {
            QMessageBox::warning(
                &dialog, tr("Invalid scene"), tr("The player seat must exist in the initial scene."));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    scene->title = title->text().trimmed();
    scene->author = author->text().trimmed();
    scene->intro = intro->toPlainText();
    scene->opening = opening->toPlainText();
    scene->ending = ending->toPlainText();
    scene->setup = document;
    scene->playerSeat = seat->value();
    scene->goals.clear();
    if (useGoal->isChecked())
        scene->goals << goal;
    if (scene->id.isEmpty())
        scene->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    scene->revision = ScenarioWork::computeSceneRevision(*scene);
    return true;
}

bool ScenarioWorkEditorDialog::editEntryDefinition(ScenarioWork::StageEntry *entry, bool isNew)
{
    if (m_work.scenes.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot add stage entry"), tr("Pin a scene revision first."));
        return false;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? tr("Add stage entry") : tr("Edit stage entry"));
    auto *title = new QLineEdit(entry->title);
    auto *scene = new QComboBox;
    for (const auto &item : m_work.scenes)
        scene->addItem(item.title + QStringLiteral(" [") + item.revision.left(12) + QLatin1Char(']'),
            item.id + QLatin1Char('|') + item.revision);
    const int index = scene->findData(entry->sceneId + QLatin1Char('|') + entry->sceneRevision);
    if (index >= 0)
        scene->setCurrentIndex(index);
    auto *intro = new QTextEdit;
    intro->setPlainText(entry->intro);
    auto *overrideGoal = new QCheckBox(tr("Override the pinned scene goals"));
    overrideGoal->setChecked(!entry->goals.isEmpty());
    auto *goals = new QPushButton(tr("Configure success/failure goals"));
    auto goal = entry->goals.value(0);
    auto *form = new QFormLayout(&dialog);
    form->addRow(tr("Title"), title);
    form->addRow(tr("Pinned scene"), scene);
    form->addRow(tr("Introduction"), intro);
    form->addRow(overrideGoal);
    form->addRow(goals);
    connect(goals, &QPushButton::clicked, &dialog, [&] {
        if (editGoal(&goal))
            overrideGoal->setChecked(true);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    const QStringList idRev = scene->currentData().toString().split('|');
    entry->title = title->text().trimmed();
    entry->intro = intro->toPlainText();
    entry->sceneId = idRev.value(0);
    entry->sceneRevision = idRev.value(1);
    entry->goals.clear();
    if (overrideGoal->isChecked())
        entry->goals << goal;
    if (entry->id.isEmpty())
        entry->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return true;
}

bool ScenarioWorkEditorDialog::editGoal(ScenarioWork::GoalDefinition *goal)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Goal definition"));
    dialog.resize(850, 650);
    auto *layout = new QVBoxLayout(&dialog);
    auto *mode = new QComboBox;
    mode->addItem(tr("Objective"), int(ScenarioWork::GoalMode::Objective));
    mode->addItem(tr("Settlement"), int(ScenarioWork::GoalMode::Settlement));
    mode->setCurrentIndex(mode->findData(int(goal->mode)));
    layout->addWidget(mode);
    // Each group owns editable predicate rows; widget values are committed only on OK.
    auto buildGroup = [&](const QString &title, const ScenarioWork::GoalGroup &group, QCheckBox **all) {
        auto *box = new QGroupBox(title);
        auto *rows = new QVBoxLayout(box);
        *all = new QCheckBox(tr("All predicates must match (unchecked: any)"));
        (*all)->setChecked(group.all);
        rows->addWidget(*all);
        auto *table = new QTableWidget(0, 5);
        table->setHorizontalHeaderLabels(
            { tr("Type"), tr("Seat (zero based)"), tr("Comparison"), tr("Value"), tr("Mark") });
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        rows->addWidget(table);
        auto addRow = [table](const ScenarioWork::GoalPredicate &p) {
            const int r = table->rowCount();
            table->insertRow(r);
            auto *type = predicateTypeBox(table);
            type->setCurrentIndex(type->findData(int(p.type)));
            auto *seat = new QSpinBox(table);
            seat->setRange(0, 9);
            seat->setValue(p.seat);
            auto *op = predicateOpBox(table);
            op->setCurrentIndex(op->findData(int(p.op)));
            auto *value = new QSpinBox(table);
            value->setRange(-999999, 999999);
            value->setValue(p.threshold);
            auto *mark = new QLineEdit(p.mark, table);
            table->setCellWidget(r, 0, type);
            table->setCellWidget(r, 1, seat);
            table->setCellWidget(r, 2, op);
            table->setCellWidget(r, 3, value);
            table->setCellWidget(r, 4, mark);
            auto update = [=] {
                const auto kind = ScenarioWork::PredicateType(type->currentData().toInt());
                const bool numeric = kind == ScenarioWork::PredicateType::Hp
                    || kind == ScenarioWork::PredicateType::Mark
                    || kind == ScenarioWork::PredicateType::Turns;
                seat->setEnabled(kind != ScenarioWork::PredicateType::Turns);
                op->setEnabled(numeric);
                value->setEnabled(numeric);
                mark->setEnabled(kind == ScenarioWork::PredicateType::Mark);
            };
            connect(type, &QComboBox::currentIndexChanged, table, update);
            update();
        };
        for (const auto &p : group.predicates)
            addRow(p);
        auto *buttons = new QHBoxLayout;
        auto *add = new QPushButton(tr("Add predicate"));
        auto *remove = new QPushButton(tr("Remove predicate"));
        buttons->addWidget(add);
        buttons->addWidget(remove);
        rows->addLayout(buttons);
        connect(add, &QPushButton::clicked, box, [addRow] { addRow(ScenarioWork::GoalPredicate()); });
        connect(remove, &QPushButton::clicked, box, [table] {
            if (table->currentRow() >= 0)
                table->removeRow(table->currentRow());
        });
        layout->addWidget(box);
        return table;
    };
    QCheckBox *successAll, *failureAll;
    auto *success = buildGroup(tr("Success predicates"), goal->success, &successAll);
    auto *failure = buildGroup(tr("Failure predicates"), goal->failure, &failureAll);
    auto readGroup = [](QTableWidget *table, bool all) {
        ScenarioWork::GoalGroup group;
        group.all = all;
        for (int r = 0; r < table->rowCount(); ++r) {
            ScenarioWork::GoalPredicate p;
            p.type = ScenarioWork::PredicateType(
                static_cast<QComboBox *>(table->cellWidget(r, 0))->currentData().toInt());
            p.seat = static_cast<QSpinBox *>(table->cellWidget(r, 1))->value();
            p.op = ScenarioWork::PredicateOp(
                static_cast<QComboBox *>(table->cellWidget(r, 2))->currentData().toInt());
            p.threshold = static_cast<QSpinBox *>(table->cellWidget(r, 3))->value();
            p.mark = static_cast<QLineEdit *>(table->cellWidget(r, 4))->text().trimmed();
            group.predicates << p;
        }
        return group;
    };
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto s = readGroup(success, successAll->isChecked()),
                   f = readGroup(failure, failureAll->isChecked());
        if (mode->currentData().toInt() == int(ScenarioWork::GoalMode::Objective) && s.predicates.isEmpty()) {
            QMessageBox::warning(
                &dialog, tr("Invalid goal"), tr("An objective needs at least one success predicate."));
            return;
        }
        for (const auto &group : { s, f })
            for (const auto &p : group.predicates)
                if (p.type == ScenarioWork::PredicateType::Mark && p.mark.isEmpty()) {
                    QMessageBox::warning(
                        &dialog, tr("Invalid goal"), tr("A mark predicate needs a mark name."));
                    return;
                }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    goal->mode = ScenarioWork::GoalMode(mode->currentData().toInt());
    goal->success = readGroup(success, successAll->isChecked());
    goal->failure = readGroup(failure, failureAll->isChecked());
    return true;
}
void ScenarioWorkEditorDialog::addScene()
{
    readFields();
    ScenarioWork::SceneDefinition scene;
    if (editSceneDefinition(&scene, true)) {
        m_work.scenes << scene;
        refreshScenes();
        m_scenes->setCurrentRow(m_work.scenes.size() - 1);
    }
}
void ScenarioWorkEditorDialog::editScene()
{
    const int row = m_scenes->currentRow();
    if (row < 0)
        return;
    auto draft = m_work.scenes.at(row);
    if (!editSceneDefinition(&draft, false) || draft.revision == m_work.scenes.at(row).revision)
        return;
    // An entry keeps the exact old revision until its author explicitly repins it.
    bool referenced = false;
    for (const auto &entry : m_work.entries)
        referenced |= entry.sceneId == draft.id && entry.sceneRevision == m_work.scenes.at(row).revision;
    if (referenced && !m_work.scenes.at(row).setup.isEmpty()) {
        bool exists = false;
        for (const auto &scene : m_work.scenes)
            exists |= scene.id == draft.id && scene.revision == draft.revision;
        if (!exists)
            m_work.scenes << draft;
        QMessageBox::information(this, tr("Scene revision saved"),
            tr("Existing entries keep their pinned revision. Use Edit entry / pinned revision to select the "
               "new revision."));
    } else {
        const auto old = m_work.scenes.at(row);
        m_work.scenes[row] = draft;
        // The initial empty draft has never been a playable snapshot.
        if (old.setup.isEmpty())
            for (auto &entry : m_work.entries)
                if (entry.sceneId == old.id && entry.sceneRevision == old.revision)
                    entry.sceneRevision = draft.revision;
    }
    refreshScenes();
    refreshEntries();
    if (m_scenes->count())
        m_scenes->setCurrentRow(0);
    if (m_entries->count())
        m_entries->setCurrentRow(0);
}
void ScenarioWorkEditorDialog::removeScene()
{
    const int row = m_scenes->currentRow();
    if (row < 0)
        return;
    const auto &scene = m_work.scenes.at(row);
    for (const auto &entry : m_work.entries)
        if (entry.sceneId == scene.id && entry.sceneRevision == scene.revision) {
            QMessageBox::warning(this, tr("Cannot remove scene"),
                tr("An entry still references this revision. Repin or remove that entry first."));
            return;
        }
    m_work.scenes.removeAt(row);
    refreshScenes();
}
void ScenarioWorkEditorDialog::importScene()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import existing scene"), QString(), tr("Scene or work files (*.txt *.qswork.json)"));
    if (path.isEmpty())
        return;
    if (path.endsWith(QStringLiteral(".qswork.json"), Qt::CaseInsensitive)) {
        ScenarioWork::WorkDefinition source;
        QString error;
        if (!ScenarioWork::readWork(path, &source, &error)) {
            QMessageBox::warning(this, tr("Cannot import scene"), error);
            return;
        }
        readFields();
        if (source.compatibility != m_work.compatibility) {
            QMessageBox::warning(this, tr("Incompatible work"),
                tr("Import this work into the library and explicitly rebind its compatibility before copying "
                   "scenes into this work."));
            return;
        }
        QStringList choices;
        for (int i = 0; i < source.scenes.size(); ++i)
            choices << QStringLiteral("%1. %2 [%3]")
                           .arg(i + 1)
                           .arg(source.scenes.at(i).title, source.scenes.at(i).revision.left(12));
        bool accepted = false;
        const QString choice = QInputDialog::getItem(
            this, tr("Import existing scene"), tr("Pinned scene"), choices, 0, false, &accepted);
        if (!accepted)
            return;
        const auto scene = source.scenes.at(choices.indexOf(choice));
        for (const auto &existing : m_work.scenes)
            if (existing.id == scene.id && existing.revision == scene.revision)
                return;
        m_work.scenes << scene;
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Cannot import scene"), file.errorString());
            return;
        }
        ScenarioWork::SceneDefinition scene;
        scene.setup = QString::fromUtf8(file.readAll());
        CustomAssignDialog validator(this, true);
        QString error;
        if (!validator.loadDocument(scene.setup, &error)) {
            QMessageBox::warning(this, tr("Invalid scene"), error);
            return;
        }
        scene.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        scene.title = QFileInfo(path).completeBaseName();
        scene.author = tr("Imported");
        scene.revision = ScenarioWork::computeSceneRevision(scene);
        m_work.scenes << scene;
    }
    refreshScenes();
    m_scenes->setCurrentRow(m_work.scenes.size() - 1);
}
void ScenarioWorkEditorDialog::addEntry()
{
    readFields();
    ScenarioWork::StageEntry entry;
    if (editEntryDefinition(&entry, true)) {
        m_work.entries << entry;
        refreshEntries();
    }
}
void ScenarioWorkEditorDialog::editEntry()
{
    const int row = m_entries->currentRow();
    if (row < 0)
        return;
    auto entry = m_work.entries.at(row);
    if (editEntryDefinition(&entry, false)) {
        m_work.entries[row] = entry;
        refreshEntries();
        m_entries->setCurrentRow(row);
    }
}
void ScenarioWorkEditorDialog::removeEntry()
{
    const int row = m_entries->currentRow();
    if (row >= 0) {
        m_work.entries.removeAt(row);
        refreshEntries();
    }
}
void ScenarioWorkEditorDialog::moveEntryUp()
{
    const int row = m_entries->currentRow();
    if (row > 0) {
        m_work.entries.swapItemsAt(row, row - 1);
        refreshEntries();
        m_entries->setCurrentRow(row - 1);
    }
}
void ScenarioWorkEditorDialog::moveEntryDown()
{
    const int row = m_entries->currentRow();
    if (row >= 0 && row + 1 < m_work.entries.size()) {
        m_work.entries.swapItemsAt(row, row + 1);
        refreshEntries();
        m_entries->setCurrentRow(row + 1);
    }
}
void ScenarioWorkEditorDialog::saveWork()
{
    readFields();
    QStringList errors;
    if (!ScenarioWork::validateWork(m_work, &errors)) {
        QMessageBox::warning(this, tr("Invalid work"), errors.join('\n'));
        return;
    }
    m_work.revision = ScenarioWork::computeRevision(m_work);
    setProperty("saveSucceeded", false);
    emit workSaved(m_work);
    if (property("saveSucceeded").toBool())
        accept();
}
void ScenarioWorkEditorDialog::rebindCompatibility()
{
    if (QMessageBox::warning(this, tr("Change compatibility"),
            tr("Rebinding changes the meaning of physical card IDs if the catalog changed. Review every "
               "initial scene and carried card before using this work. Scene symbols will be validated "
               "against the current runtime; existing progress remains tied to the old revision."),
            QMessageBox::Cancel | QMessageBox::Ok)
        != QMessageBox::Ok)
        return;
    for (const auto &scene : m_work.scenes) {
        CustomAssignDialog validator(this, true);
        QString error;
        if (!validator.loadDocument(scene.setup, &error)) {
            QMessageBox::warning(this, tr("Invalid scene"), scene.title + QLatin1Char('\n') + error);
            return;
        }
    }
    m_work.compatibility = m_runtimeCompatibility;
    m_requiredExtensions->setText(requiredExtensionNames(m_work.compatibility));
}
ScenarioWorkLibraryDialog::ScenarioWorkLibraryDialog(
    const QString &libraryRoot, const QJsonObject &compatibility, QWidget *parent)
    : QDialog(parent)
    , m_libraryRoot(libraryRoot)
    , m_runtimeCompatibility(compatibility)
{
    setWindowTitle(tr("Work library"));
    resize(900, 600);
    m_list = new QListWidget;
    auto *newScene = new QPushButton(tr("New scene work"));
    auto *newStage = new QPushButton(tr("New stage work"));
    m_edit = new QPushButton(tr("Edit"));
    m_duplicate = new QPushButton(tr("Duplicate"));
    auto *import = new QPushButton(tr("Import"));
    m_export = new QPushButton(tr("Export"));
    m_play = new QPushButton(tr("Play"));
    m_continue = new QPushButton(tr("Continue"));
    auto *history = new QPushButton(tr("History"));
    auto *close = new QPushButton(tr("Close"));
    auto *actions = new QHBoxLayout;
    for (auto *button :
        { newScene, newStage, m_edit, m_duplicate, import, m_export, m_play, m_continue, history })
        actions->addWidget(button);
    actions->addStretch();
    actions->addWidget(close);
    auto *details = new QTextEdit(this);
    details->setReadOnly(true);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_list, 2);
    layout->addWidget(details, 1);
    layout->addLayout(actions);
    connect(m_list, &QListWidget::currentRowChanged, this, [this, details] {
        bool ok = false;
        const auto work = currentWork(&ok);
        if (!ok) {
            details->clear();
            return;
        }
        QString text = tr("Title: %1\nAuthor: %2\nRevision: %3\n\n%4")
                           .arg(work.title, work.author, work.revision, work.intro);
        text += QStringLiteral("\n\n")
            + tr("Rule: %1\nRequired extensions: %2")
                  .arg(work.rule, requiredExtensionNames(work.compatibility));
        for (const auto &entry : work.entries) {
            text += QStringLiteral("\n\n") + entry.title + QLatin1Char('\n') + entry.intro;
            for (const auto &scene : work.scenes)
                if (scene.id == entry.sceneId && scene.revision == entry.sceneRevision) {
                    text += QLatin1Char('\n') + scene.intro;
                    break;
                }
        }
        details->setPlainText(text);
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(newScene, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::newSceneWork);
    connect(newStage, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::newStageWork);
    connect(m_edit, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::editWork);
    connect(m_duplicate, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::duplicateWork);
    connect(import, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::importWork);
    connect(m_export, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::exportWork);
    connect(m_play, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::playWork);
    connect(m_continue, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::continueWork);
    connect(history, &QPushButton::clicked, this, &ScenarioWorkLibraryDialog::chooseHistory);
    refresh();
}

void ScenarioWorkLibraryDialog::refresh()
{
    m_list->clear();
    QString error;
    for (const auto &info : ScenarioWork::listWorks(m_libraryRoot, &error)) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  [%2]  %3").arg(info.title, kindText(info.kind), info.revision.left(12)),
            m_list);
        item->setData(Qt::UserRole, info.path);
    }
}
QString ScenarioWorkLibraryDialog::selectedPath() const
{
    auto *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}
ScenarioWork::WorkDefinition ScenarioWorkLibraryDialog::currentWork(bool *ok) const
{
    ScenarioWork::WorkDefinition work;
    QString error;
    const bool good = !selectedPath().isEmpty() && ScenarioWork::readWork(selectedPath(), &work, &error);
    if (ok)
        *ok = good;
    return work;
}
bool ScenarioWorkLibraryDialog::writeWork(const ScenarioWork::WorkDefinition &work)
{
    QString error;
    if (!ScenarioWork::writeWork(m_libraryRoot, work, &error)) {
        QMessageBox::warning(this, tr("Cannot save work"), error);
        return false;
    }
    refresh();
    return true;
}
void ScenarioWorkLibraryDialog::openEditor(ScenarioWork::WorkDefinition work)
{
    ScenarioWorkEditorDialog dialog(this, m_runtimeCompatibility, work.rules);
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
void ScenarioWorkLibraryDialog::newSceneWork()
{
    auto work = ScenarioWork::defaultWork();
    work.compatibility = m_runtimeCompatibility;
    openEditor(work);
}
void ScenarioWorkLibraryDialog::newStageWork()
{
    auto work = ScenarioWork::defaultWork();
    work.compatibility = m_runtimeCompatibility;
    work.kind = ScenarioWork::WorkKind::Stage;
    openEditor(work);
}
void ScenarioWorkLibraryDialog::editWork()
{
    bool ok = false;
    auto work = currentWork(&ok);
    if (ok)
        openEditor(work);
}
void ScenarioWorkLibraryDialog::duplicateWork()
{
    bool ok = false;
    auto work = currentWork(&ok);
    if (!ok)
        return;
    work.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    work.revision.clear();
    work.title += tr(" (Copy)");
    writeWork(work);
}
void ScenarioWorkLibraryDialog::importWork()
{
    const QString path
        = QFileDialog::getOpenFileName(this, tr("Import work"), QString(), tr("Work files (*.qswork.json)"));
    if (path.isEmpty())
        return;
    ScenarioWork::WorkDefinition work;
    QString error;
    if (!ScenarioWork::readWork(path, &work, &error)) {
        QMessageBox::warning(this, tr("Invalid work"), error);
        return;
    }
    writeWork(work);
}
void ScenarioWorkLibraryDialog::exportWork()
{
    bool ok = false;
    auto work = currentWork(&ok);
    if (!ok)
        return;
    const QString path
        = QFileDialog::getSaveFileName(this, tr("Export work"), QString(), tr("Work files (*.qswork.json)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Cannot export work"), file.errorString());
        return;
    }
    const QByteArray bytes = QJsonDocument(ScenarioWork::workToJson(work)).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, tr("Cannot export work"), file.errorString());
}
QString ScenarioWorkLibraryDialog::compatibilityError(const ScenarioWork::WorkDefinition &work) const
{
    if (work.compatibility == m_runtimeCompatibility)
        return QString();
    return tr("This work was created for a different runtime manifest or card catalog. Saved "
              "compatibility:\n%1\nCurrent compatibility:\n%2")
        .arg(QString::fromUtf8(QJsonDocument(work.compatibility).toJson(QJsonDocument::Indented)),
            QString::fromUtf8(QJsonDocument(m_runtimeCompatibility).toJson(QJsonDocument::Indented)));
}
void ScenarioWorkLibraryDialog::launchWork(const ScenarioWork::WorkDefinition &work, const QString &entryId,
    const ScenarioWork::CarryState &carry, bool trial)
{
    if (entryId.isEmpty())
        return;
    ScenarioWork::WorkLaunch launch;
    launch.work = work;
    launch.entryId = entryId;
    launch.carry = carry;
    launch.trial = trial;
    hide();
    emit playRequested(launch);
}
void ScenarioWorkLibraryDialog::chooseEntryState(const ScenarioWork::WorkDefinition &work,
    const QString &entryId, const ScenarioWork::WorkProgress &progress)
{
    const QString reason = compatibilityError(work);
    if (!reason.isEmpty()) {
        QMessageBox::warning(this, tr("Incompatible work"), reason);
        return;
    }
    if (!ScenarioWork::canPlayEntry(work, progress, entryId)) {
        QMessageBox::warning(this, tr("Entry locked"), tr("Complete the preceding stage first."));
        return;
    }
    QStringList choices { tr("Original entry") };
    QList<ScenarioWork::CarryState> states { ScenarioWork::CarryState() };
    const ScenarioWork::ProgressSnapshot *latest = nullptr;
    for (const auto &snapshot : progress.snapshots)
        if (snapshot.entryId == entryId && (!latest || snapshot.createdAt >= latest->createdAt))
            latest = &snapshot;
    if (latest) {
        choices << tr("Latest snapshot (%1)").arg(latest->createdAt.toString(Qt::ISODate));
        states << latest->carry;
    }
    for (const auto &snapshot : progress.snapshots) {
        if (snapshot.entryId != entryId)
            continue;
        choices << QStringLiteral("%1  %2").arg(snapshot.createdAt.toString(Qt::ISODate), snapshot.id);
        states << snapshot.carry;
    }
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, tr("Play"),
        tr("Choose the initial state for this entry"), choices, latest ? 1 : 0, false, &accepted);
    if (!accepted)
        return;
    const int index = choices.indexOf(chosen);
    if (index >= 0)
        launchWork(work, entryId, states.at(index), false);
}
void ScenarioWorkLibraryDialog::playWork()
{
    bool ok = false;
    const auto work = currentWork(&ok);
    if (!ok)
        return;
    ScenarioWork::WorkProgress progress;
    QString error;
    if (!ScenarioWork::loadProgress(m_libraryRoot, work, &progress, &error)) {
        QMessageBox::warning(this, tr("Cannot read progress"), error);
        return;
    }
    QStringList choices;
    for (int i = 0; i < work.entries.size(); ++i)
        choices << QStringLiteral("%1. %2%3")
                       .arg(i + 1)
                       .arg(work.entries.at(i).title,
                           ScenarioWork::canPlayEntry(work, progress, work.entries.at(i).id)
                               ? QString()
                               : tr(" (Locked)"));
    bool accepted = false;
    const QString chosen
        = QInputDialog::getItem(this, tr("Play"), tr("Choose entry"), choices, 0, false, &accepted);
    if (!accepted)
        return;
    const int index = choices.indexOf(chosen);
    if (index >= 0)
        chooseEntryState(work, work.entries.at(index).id, progress);
}
void ScenarioWorkLibraryDialog::continueWork()
{
    bool ok = false;
    const auto work = currentWork(&ok);
    if (!ok)
        return;
    const QString reason = compatibilityError(work);
    if (!reason.isEmpty()) {
        QMessageBox::warning(this, tr("Incompatible work"), reason);
        return;
    }
    ScenarioWork::WorkProgress progress;
    QString error;
    if (!ScenarioWork::loadProgress(m_libraryRoot, work, &progress, &error)) {
        QMessageBox::warning(this, tr("Cannot read progress"), error);
        return;
    }
    if (progress.continuationEntryId.isEmpty()) {
        QMessageBox::information(this, tr("Continue"), tr("No continuation is available."));
        return;
    }
    const ScenarioWork::ProgressSnapshot *latest = nullptr;
    for (const auto &snapshot : progress.snapshots)
        if (snapshot.entryId == progress.continuationEntryId
            && (!latest || snapshot.createdAt >= latest->createdAt))
            latest = &snapshot;
    launchWork(
        work, progress.continuationEntryId, latest ? latest->carry : ScenarioWork::CarryState(), false);
}
void ScenarioWorkLibraryDialog::chooseHistory() { playWork(); }
void ScenarioWorkLibraryDialog::reload() { refresh(); }
void ScenarioWorkLibraryDialog::resumeTrialDraft()
{
    if (m_hasTrialDraft)
        openEditor(m_trialDraft);
}
