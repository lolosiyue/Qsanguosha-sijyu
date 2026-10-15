#ifndef CORRECTION_INSPECTOR_H
#define CORRECTION_INSPECTOR_H
#include <QCoreApplication>
#include <QDialog>
#include "correction-trace.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTreeWidget;
class QCheckBox;

class CorrectionInspector : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(CorrectionInspector)
public:
    CorrectionInspector(std::shared_ptr<CorrectionTraceStore> store, const QString &sourceName, QWidget *parent);
    ~CorrectionInspector() override;
private:
    void refresh();
    void showRecord();
    std::shared_ptr<CorrectionTraceStore> m_store;
    QList<CorrectionTraceRecord> m_snapshot;
    QComboBox *m_source, *m_target, *m_category;
    QLineEdit *m_context;
    QTreeWidget *m_records;
    QTableWidget *m_rows;
    QLabel *m_status, *m_result;
    QCheckBox *m_capture;
};
#endif
