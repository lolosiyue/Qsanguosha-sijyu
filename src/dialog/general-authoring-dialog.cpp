#include "general-authoring-dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QPlainTextEdit *codeEdit(QWidget *parent = nullptr, bool readOnly = false)
{
    auto *edit = new QPlainTextEdit(parent); edit->setReadOnly(readOnly);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setFont(QFont(QStringLiteral("monospace"))); edit->setTabChangesFocus(true);
    return edit;
}
}
GeneralAuthoringDialog::GeneralAuthoringDialog(QWidget *parent, const QJsonObject &initialSpec,
    const QSet<QString> &occupied, const QStringList &kingdoms, const QByteArray &cardPng, QNetworkAccessManager *manager)
    : QDialog(parent), m_provider(this, manager), m_cardPng(cardPng)
{
    setWindowTitle(tr("Playable general authoring")); resize(1120, 860);
    m_document.spec = initialSpec; m_document.occupied = occupied;
    auto *layout = new QVBoxLayout(this);
    auto *notice = new QLabel(tr("Generate and review Lua for this engine. Static checks do not establish safety. Code is never executed here; exports remain outside the installed package roots."));
    notice->setWordWrap(true); layout->addWidget(notice);
    auto *provider = new QGroupBox(tr("OpenAI-compatible provider"));
    auto *providerLayout = new QGridLayout(provider);
    m_endpoint = new QLineEdit; m_endpoint->setPlaceholderText(QStringLiteral("https://your-provider.example/v1/chat/completions"));
    m_endpoint->setObjectName("authoringEndpoint");
    m_key = new QLineEdit; m_key->setEchoMode(QLineEdit::Password); m_key->setMaxLength(4096);
    m_key->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText);
    m_key->setObjectName("authoringCredential");
    m_model = new QLineEdit; m_model->setMaxLength(200); m_model->setObjectName("authoringModel");
    auto *endpointLabel = new QLabel(tr("HTTPS chat-completions endpoint")); endpointLabel->setBuddy(m_endpoint);
    auto *keyLabel = new QLabel(tr("API key (memory only)")); keyLabel->setBuddy(m_key);
    auto *modelLabel = new QLabel(tr("Model")); modelLabel->setBuddy(m_model);
    providerLayout->addWidget(endpointLabel, 0, 0); providerLayout->addWidget(m_endpoint, 0, 1, 1, 3);
    providerLayout->addWidget(keyLabel, 1, 0); providerLayout->addWidget(m_key, 1, 1);
    providerLayout->addWidget(modelLabel, 1, 2); providerLayout->addWidget(m_model, 1, 3);
    auto *privacy = new QLabel(tr("The key is masked, kept in memory for this dialog, and never saved in settings, projects, history or exports. OS credential storage is not available in this editor. Redirects are refused."));
    privacy->setWordWrap(true); providerLayout->addWidget(privacy, 2, 0, 1, 4); layout->addWidget(provider);
    connect(m_key, &QLineEdit::editingFinished, this, [this] { m_document.rememberSecret(m_key->text()); });

    m_tabs = new QTabWidget; layout->addWidget(m_tabs, 1);
    auto *specPage = new QWidget; auto *specLayout = new QVBoxLayout(specPage);
    auto *metadata = new QFormLayout;
    m_package = new QLineEdit; m_general = new QLineEdit; m_name = new QLineEdit;
    m_title = new QLineEdit; m_designer = new QLineEdit;
    m_package->setMaxLength(64); m_general->setMaxLength(64);
    for (auto *edit : {m_name, m_title, m_designer}) edit->setMaxLength(256);
    m_package->setObjectName("authoringPackageId"); m_general->setObjectName("authoringGeneralId");
    m_kingdom = new QComboBox; m_kingdom->addItems(kingdoms); m_kingdom->setEditable(true); m_kingdom->lineEdit()->setMaxLength(32);
    m_hp = new QSpinBox; m_hp->setRange(1, 20);
    m_startHp = new QSpinBox; m_startHp->setRange(1, 20);
    m_armor = new QSpinBox; m_armor->setRange(0, 20);
    m_male = new QCheckBox(tr("Male")); m_lord = new QCheckBox(tr("Lord"));
    metadata->addRow(tr("Package identifier"), m_package); metadata->addRow(tr("General identifier"), m_general);
    metadata->addRow(tr("Display name"), m_name); metadata->addRow(tr("Kingdom"), m_kingdom);
    auto *stats = new QHBoxLayout; stats->addWidget(new QLabel(tr("Maximum HP"))); stats->addWidget(m_hp);
    stats->addWidget(new QLabel(tr("Starting HP"))); stats->addWidget(m_startHp);
    stats->addWidget(new QLabel(tr("Armor"))); stats->addWidget(m_armor); stats->addWidget(m_male); stats->addWidget(m_lord);
    metadata->addRow(tr("General properties"), stats); metadata->addRow(tr("Title"), m_title); metadata->addRow(tr("Designer"), m_designer);
    specLayout->addLayout(metadata);
    auto *identifierHelp = new QLabel(tr("Use lowercase letters, digits and underscores. General identifiers start with package_id_; skill identifiers start with general_id_. Existing names are rejected. Imported card descriptions may combine several skills; assign each description to its matching row before sending."));
    identifierHelp->setWordWrap(true); specLayout->addWidget(identifierHelp);
    m_skills = new QTableWidget(0, 3); m_skills->setHorizontalHeaderLabels({tr("Skill identifier"), tr("Skill title"), tr("Skill description")});
    m_skills->setAccessibleName(tr("Skills"));
    m_skills->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); m_skills->setObjectName("authoringSkills");
    specLayout->addWidget(m_skills, 1);
    auto *skillButtons = new QHBoxLayout; auto *add = new QPushButton(tr("Add skill")); auto *remove = new QPushButton(tr("Remove selected skill"));
    skillButtons->addWidget(add); skillButtons->addWidget(remove); skillButtons->addStretch(); specLayout->addLayout(skillButtons);
    connect(add, &QPushButton::clicked, this, [this] {
        if (m_skills->rowCount() >= 8) return;
        const int row = m_skills->rowCount(); m_skills->insertRow(row);
        m_skills->setItem(row, 0, new QTableWidgetItem(m_general->text() + "_skill" + QString::number(row + 1)));
        m_skills->setItem(row, 1, new QTableWidgetItem(tr("New skill"))); m_skills->setItem(row, 2, new QTableWidgetItem);
        syncInputs(); m_skills->setCurrentCell(row, 2); m_skills->editItem(m_skills->item(row, 2));
    });
    auto *editDescription = new QPushButton(tr("Edit selected description")); skillButtons->insertWidget(2, editDescription);
    connect(editDescription, &QPushButton::clicked, this, [this] {
        const int row = m_skills->currentRow(); if (row < 0) return;
        QDialog dialog(this); dialog.setWindowTitle(tr("Skill description")); dialog.resize(700, 450);
        auto *layout = new QVBoxLayout(&dialog); auto *text = new QPlainTextEdit;
        text->setPlainText(m_skills->item(row, 2) ? m_skills->item(row, 2)->text() : QString());
        text->setTabChangesFocus(true); layout->addWidget(text);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) m_skills->setItem(row, 2, new QTableWidgetItem(text->toPlainText()));
    });
    connect(remove, &QPushButton::clicked, this, [this] { if (m_skills->currentRow() >= 0) { m_skills->removeRow(m_skills->currentRow()); syncInputs(); } });
    m_tabs->addTab(specPage, tr("Specification"));
    auto *reviewPage = new QWidget; auto *reviewLayout = new QVBoxLayout(reviewPage);
    auto *reviewHelp = new QLabel(tr("Edit skills between BEGIN SKILLS and END SKILLS. The specification owns metadata, registration and translations. Save a project to preserve your edits and versions."));
    reviewHelp->setWordWrap(true); reviewLayout->addWidget(reviewHelp);
    m_reviewed = codeEdit(); m_reviewed->setObjectName("authoringReviewedCode"); reviewLayout->addWidget(m_reviewed);
    m_reviewed->setAccessibleName(tr("Reviewed code"));
    m_tabs->addTab(reviewPage, tr("Reviewed code"));
    auto *candidatePage = new QWidget; auto *candidateLayout = new QVBoxLayout(candidatePage);
    auto *candidateHelp = new QLabel(tr("Review the candidate and comparison before applying. You can edit the candidate. Applying updates this document only and never installs or runs Lua."));
    candidateHelp->setWordWrap(true); candidateLayout->addWidget(candidateHelp);
    auto *split = new QSplitter; m_candidate = codeEdit(); m_candidate->setObjectName("authoringCandidate"); m_diff = codeEdit(nullptr, true);
    m_candidate->setAccessibleName(tr("Candidate code")); m_diff->setAccessibleName(tr("Code comparison"));
    split->addWidget(m_candidate); split->addWidget(m_diff); candidateLayout->addWidget(split, 1);
    m_tabs->addTab(candidatePage, tr("Candidate and diff"));
    auto *historyPage = new QWidget; auto *historyLayout = new QVBoxLayout(historyPage);
    auto *historyHelp = new QLabel(tr("The last 16 checkpoints are kept in projects. Restore or undo a version to recover reviewed manual edits.")); historyHelp->setWordWrap(true); historyLayout->addWidget(historyHelp);
    auto *historySplit = new QSplitter; m_history = new QListWidget; m_historyCode = codeEdit(nullptr, true); m_historyCode->setAccessibleName(tr("Version code"));
    historySplit->addWidget(m_history); historySplit->addWidget(m_historyCode); historyLayout->addWidget(historySplit, 1);
    auto *restore = new QPushButton(tr("Restore selected version")); historyLayout->addWidget(restore);
    connect(m_history, &QListWidget::currentRowChanged, this, [this](int row) { m_historyCode->setPlainText(row >= 0 && row < m_document.history.size() ? m_document.history[row].code : QString()); });
    connect(restore, &QPushButton::clicked, this, [this] {
        const int row = m_history->currentRow(); if (row < 0 || row >= m_document.history.size()) return;
        const auto version = m_document.history[row]; cancelRequest(); m_document.checkpoint();
        m_document.setSpec(version.spec); m_document.setReviewed(version.code); m_document.checkpoint(); loadInputs(); updateViews();
    });
    m_tabs->addTab(historyPage, tr("History"));

    auto *correctionLabel = new QLabel(tr("Generation or correction instructions"));
    m_instruction = new QPlainTextEdit; m_instruction->setMaximumHeight(75); m_instruction->setTabChangesFocus(true);
    correctionLabel->setBuddy(m_instruction); layout->addWidget(correctionLabel); layout->addWidget(m_instruction);
    m_instruction->setPlaceholderText(tr("Describe what to generate or correct. The request includes the original specification, current metadata, reviewed code and diagnostics."));
    m_diagnostics = new QPlainTextEdit; m_diagnostics->setReadOnly(true); m_diagnostics->setMaximumHeight(100);
    m_diagnostics->setTabChangesFocus(true); m_diagnostics->setObjectName("authoringDiagnostics"); m_diagnostics->setAccessibleName(tr("Validation and request status")); layout->addWidget(m_diagnostics);
    auto *actions = new QHBoxLayout;
    auto button = [&](const QString &text, const QString &shortcut) { auto *b = new QPushButton(text); b->setAutoDefault(false); if (!shortcut.isEmpty()) b->setShortcut(QKeySequence(shortcut)); actions->addWidget(b); return b; };
    m_send = button(tr("Preview request"), "Alt+G"); m_cancel = button(tr("Cancel request"), "Alt+C");
    auto *validate = button(tr("Validate code"), "Alt+V"); m_apply = button(tr("Apply reviewed candidate"), "Alt+A");
    auto *undo = button(tr("Undo version"), "Alt+U");
    connect(m_send, &QPushButton::clicked, this, &GeneralAuthoringDialog::previewRequest);
    connect(m_cancel, &QPushButton::clicked, this, &GeneralAuthoringDialog::cancelRequest);
    connect(validate, &QPushButton::clicked, this, [this] { syncInputs(); const auto errors = m_document.diagnostics(); showMessage(errors.isEmpty() ? tr("Static checks passed. This does not establish safety or gameplay correctness. Test manually in a disposable runtime after review.") : errors.join('\n')); });
    connect(m_apply, &QPushButton::clicked, this, [this] { QString error; if (!m_document.apply(&error)) showMessage(error); else { m_manualCheckpoint = false; updateViews(); showMessage(tr("Candidate applied to the document. Lua was not run or installed.")); m_tabs->setCurrentIndex(1); } });
    connect(undo, &QPushButton::clicked, this, [this] { cancelRequest(); if (m_document.undo()) { loadInputs(); updateViews(); } });
    layout->addLayout(actions);
    auto *files = new QHBoxLayout; auto *save = new QPushButton(tr("Save project")); auto *open = new QPushButton(tr("Open project")); auto *exportButton = new QPushButton(tr("Export disabled package"));
    m_includeArt = new QCheckBox(tr("Include current card artwork snapshot")); m_includeArt->setChecked(!cardPng.isEmpty());
    auto *close = new QPushButton(tr("Close")); for (auto *b : {save, open, exportButton, close}) b->setAutoDefault(false);
    files->addWidget(save); files->addWidget(open); files->addWidget(m_includeArt); files->addWidget(exportButton); files->addStretch(); files->addWidget(close); layout->addLayout(files);
    connect(save, &QPushButton::clicked, this, &GeneralAuthoringDialog::saveProject); connect(open, &QPushButton::clicked, this, &GeneralAuthoringDialog::openProject);
    connect(exportButton, &QPushButton::clicked, this, &GeneralAuthoringDialog::exportPackage); connect(close, &QPushButton::clicked, this, &GeneralAuthoringDialog::reject);
    for (auto *edit : {m_package, m_general, m_name, m_title, m_designer}) connect(edit, &QLineEdit::textChanged, this, [this] { syncInputs(); });
    for (auto *spin : {m_hp, m_startHp, m_armor}) connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { syncInputs(); });
    connect(m_kingdom, &QComboBox::currentTextChanged, this, [this] { syncInputs(); });
    connect(m_male, &QCheckBox::toggled, this, [this] { syncInputs(); }); connect(m_lord, &QCheckBox::toggled, this, [this] { syncInputs(); });
    connect(m_skills, &QTableWidget::cellChanged, this, [this] { syncInputs(); });
    connect(m_reviewed, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading) return;
        if (!m_manualCheckpoint) { m_document.checkpoint(); m_manualCheckpoint = true; }
        m_document.setReviewed(m_reviewed->toPlainText());
        m_diff->setPlainText(GeneralAuthoring::comparison(m_document.reviewed, m_document.candidate));
        if (m_document.busy()) showMessage(tr("Document changed; the pending response will be ignored to preserve your edits."));
        m_apply->setEnabled(false);
    });
    connect(m_candidate, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading) return;
        m_document.candidate = m_candidate->toPlainText(); m_diff->setPlainText(GeneralAuthoring::comparison(m_document.reviewed, m_document.candidate));
    });
    loadInputs(); updateViews(); m_endpoint->setFocus();
}
void GeneralAuthoringDialog::syncInputs()
{
    if (m_loading) return;
    QJsonArray skills;
    for (int row = 0; row < m_skills->rowCount(); ++row) {
        auto text = [&](int col) { const auto *item = m_skills->item(row, col); return item ? item->text() : QString(); };
        skills.append(QJsonObject{{"id", text(0)}, {"title", text(1)}, {"description", text(2)}});
    }
    QJsonObject spec{{"package_id", m_package->text()}, {"general_id", m_general->text()}, {"display_name", m_name->text()},
        {"kingdom", m_kingdom->currentText()}, {"max_hp", m_hp->value()}, {"start_hp", m_startHp->value()}, {"armor", m_armor->value()},
        {"male", m_male->isChecked()}, {"lord", m_lord->isChecked()}, {"title", m_title->text()}, {"designer", m_designer->text()}, {"skills", skills}};
    if (spec != m_document.spec) {
        if (!m_manualCheckpoint) { m_document.checkpoint(); m_manualCheckpoint = true; }
        m_document.setSpec(spec); m_apply->setEnabled(false);
        if (m_document.busy()) showMessage(tr("Document changed; the pending response will be ignored to preserve your edits."));
    }
}
void GeneralAuthoringDialog::loadInputs()
{
    m_loading = true; const auto s = m_document.spec;
    m_package->setText(s.value("package_id").toString()); m_general->setText(s.value("general_id").toString());
    m_name->setText(s.value("display_name").toString()); m_title->setText(s.value("title").toString()); m_designer->setText(s.value("designer").toString());
    m_kingdom->setCurrentText(s.value("kingdom").toString()); m_hp->setValue(s.value("max_hp").toInt());
    m_startHp->setValue(s.value("start_hp").toInt()); m_armor->setValue(s.value("armor").toInt()); m_male->setChecked(s.value("male").toBool()); m_lord->setChecked(s.value("lord").toBool());
    m_skills->setRowCount(0);
    for (const auto &v : s.value("skills").toArray()) {
        int row = m_skills->rowCount(); m_skills->insertRow(row); const auto skill = v.toObject();
        m_skills->setItem(row, 0, new QTableWidgetItem(skill.value("id").toString()));
        m_skills->setItem(row, 1, new QTableWidgetItem(skill.value("title").toString())); m_skills->setItem(row, 2, new QTableWidgetItem(skill.value("description").toString()));
    }
    if (m_skills->rowCount() > 0) m_skills->setCurrentCell(0, 0);
    m_manualCheckpoint = false; m_loading = false;
}
void GeneralAuthoringDialog::updateViews()
{
    m_loading = true;
    m_reviewed->setPlainText(m_document.reviewed); m_candidate->setPlainText(m_document.candidate);
    m_diff->setPlainText(GeneralAuthoring::comparison(m_document.reviewed, m_document.candidate));
    m_history->clear();
    for (int i = 0; i < m_document.history.size(); ++i) m_history->addItem(tr("Version %1 — %2").arg(i + 1).arg(m_document.history[i].spec.value("display_name").toString()));
    m_send->setEnabled(!m_document.busy()); m_cancel->setEnabled(m_document.busy()); m_apply->setEnabled(!m_document.candidate.isEmpty());
    m_loading = false;
}
void GeneralAuthoringDialog::showMessage(const QString &message)
{
    m_document.rememberSecret(m_key->text());
    m_diagnostics->setPlainText(m_document.containsSecret(message.toUtf8()) ? tr("A credential appears in authoring content. Remove it before continuing.") : message);
}
void GeneralAuthoringDialog::previewRequest()
{
    syncInputs(); m_document.rememberSecret(m_key->text());
    const QUrl endpoint(m_endpoint->text().trimmed());
    if (!GeneralAuthoring::validEndpoint(endpoint) || m_document.containsSecret(endpoint.toEncoded())) { showMessage(tr("Enter the final HTTPS chat-completions endpoint without credentials, query or fragment.")); return; }
    QString error; const auto payload = m_document.preview(m_model->text().trimmed(), m_instruction->toPlainText(), &error);
    if (payload.isEmpty()) { showMessage(error); return; }
    QDialog preview(this); preview.setWindowTitle(tr("Review data sent to the provider")); preview.resize(960, 720);
    auto *layout = new QVBoxLayout(&preview); auto *target = new QLabel(tr("Send to %1 using model %2. Only the text below is sent as model content; artwork and local file paths are excluded.").arg(endpoint.toDisplayString(), m_model->text()));
    target->setWordWrap(true); layout->addWidget(target);
    auto *content = codeEdit(nullptr, true); content->setPlainText(QString::fromUtf8(payload)); layout->addWidget(content);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel); auto *send = buttons->addButton(tr("Send request"), QDialogButtonBox::AcceptRole);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    send->setAutoDefault(false); connect(send, &QPushButton::clicked, &preview, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &preview, &QDialog::reject); layout->addWidget(buttons);
    content->setFocus(); if (preview.exec() != QDialog::Accepted) return;
    const auto id = m_document.beginRequest(); m_manualCheckpoint = false; updateViews(); showMessage(tr("Waiting for provider. You can cancel; edits made now will invalidate the response."));
    m_provider.send(endpoint, m_key->text(), payload, [this, id](const QByteArray &body, const QString &networkError) {
        QString message;
        if (!networkError.isEmpty()) { m_document.cancel(); message = networkError; }
        else if (m_document.receive(id, body, &message)) {
            const auto errors = GeneralAuthoring::validateCode(m_document.spec, m_document.candidate, m_document.context);
            message += (message.isEmpty() ? QString() : "\n") + (errors.isEmpty() ? tr("Candidate ready. Review the code and diff, then apply explicitly. Static checks do not establish safety.") : errors.join('\n'));
            m_tabs->setCurrentIndex(2);
        }
        const auto usage = m_document.lastUsage();
        auto tokens = [&usage](const char *name) {
            const auto value = usage.value(QLatin1String(name));
            return value.isDouble() ? QString::number(value.toInteger()) : tr("unknown");
        };
        message += "\n" + tr("Provider-reported tokens: input %1, output %2, cache read %3, cache write %4. This is not a bill.")
            .arg(tokens("input_tokens"), tokens("output_tokens"), tokens("cache_read_tokens"), tokens("cache_write_tokens"));
        updateViews(); showMessage(message);
    });
}
void GeneralAuthoringDialog::cancelRequest() { m_provider.cancel(); m_document.cancel(); updateViews(); showMessage(tr("Request cancelled. Reviewed edits are preserved.")); }
void GeneralAuthoringDialog::reject() { cancelRequest(); m_key->clear(); QDialog::reject(); }
void GeneralAuthoringDialog::saveProject()
{
    m_document.rememberSecret(m_key->text());
    syncInputs(); m_document.checkpoint(); QString error; const auto bytes = m_document.project(&error);
    if (bytes.isEmpty()) { showMessage(error); return; }
    const auto path = QFileDialog::getSaveFileName(this, tr("Save authoring project"), QString(), tr("Authoring projects (*.json)")); if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) showMessage(tr("Cannot save the project."));
    else { updateViews(); showMessage(tr("Project saved without credentials. Artwork remains in the card editor; export can include the current snapshot.")); }
}
void GeneralAuthoringDialog::openProject()
{
    m_document.rememberSecret(m_key->text());
    const auto path = QFileDialog::getOpenFileName(this, tr("Open authoring project"), QString(), tr("Authoring projects (*.json)")); if (path.isEmpty()) return;
    QFile file(path); QString error;
    if (!file.open(QIODevice::ReadOnly) || file.size() > GeneralAuthoring::MaxProjectBytes) { showMessage(tr("Cannot read the project or it exceeds the size limit.")); return; }
    const auto bytes = file.readAll();
    // Validate into a temporary document first, preserving the live document on failure.
    auto replacement = m_document;
    if (!replacement.importProject(bytes, &error)) { showMessage(error); return; }
    m_provider.cancel(); m_document = replacement; loadInputs(); updateViews(); showMessage(tr("Project opened as text. Lua was not run. Validate the reviewed code before exporting."));
}
void GeneralAuthoringDialog::exportPackage()
{
    m_document.rememberSecret(m_key->text());
    syncInputs(); m_document.checkpoint();
    const auto parent = QFileDialog::getExistingDirectory(this, tr("Choose a separate disabled export directory")); if (parent.isEmpty()) return;
    QString path, error;
    if (!m_document.exportDisabled(parent, m_includeArt->isChecked() ? m_cardPng : QByteArray(), &path, &error)) showMessage(error);
    else { updateViews(); showMessage(tr("Disabled export saved to %1. No package was installed or enabled. Review and test manually in a disposable runtime before explicitly installing the child package directory.").arg(path)); }
}
