#ifndef GENERAL_AUTHORING_DIALOG_H
#define GENERAL_AUTHORING_DIALOG_H
#include "general-authoring.h"
#include "general-authoring-provider.h"
#include <QDialog>
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QTableWidget;
class QPushButton;
class QTabWidget;
class QListWidget;
class GeneralAuthoringDialog : public QDialog {
    Q_OBJECT
public:
    GeneralAuthoringDialog(QWidget *parent, const QJsonObject &initialSpec, const QSet<QString> &occupied,
                           const QStringList &kingdoms, const QByteArray &cardPng, QNetworkAccessManager *manager = nullptr);
    void reject() override;
private:
    GeneralAuthoring::Document m_document;
    GeneralAuthoring::Provider m_provider;
    QByteArray m_cardPng;
    QLineEdit *m_endpoint, *m_key, *m_model, *m_package, *m_general, *m_name, *m_title, *m_designer;
    QComboBox *m_kingdom;
    QSpinBox *m_hp, *m_startHp, *m_armor;
    QCheckBox *m_male, *m_lord, *m_includeArt;
    QTableWidget *m_skills;
    QPlainTextEdit *m_reviewed, *m_candidate, *m_diff, *m_diagnostics, *m_instruction, *m_historyCode;
    QTabWidget *m_tabs;
    QListWidget *m_history;
    QPushButton *m_send, *m_cancel, *m_apply;
    bool m_loading = false, m_manualCheckpoint = false;
    void syncInputs();
    void loadInputs();
    void updateViews();
    void showMessage(const QString &message);
    void previewRequest();
    void cancelRequest();
    void saveProject();
    void openProject();
    void exportPackage();
};
#endif
