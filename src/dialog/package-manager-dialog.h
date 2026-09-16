#ifndef QSAN_PACKAGE_MANAGER_DIALOG_H
#define QSAN_PACKAGE_MANAGER_DIALOG_H

#include <QDialog>
#include <QSharedPointer>

#include <functional>

class QListWidget;
class QPushButton;
class PackageStore;
class QThread;

class PackageManagerDialog : public QDialog
{
public:
    explicit PackageManagerDialog(const QString &runtimeRoot, const QString &userDataRoot,
                                  QWidget *parent = nullptr);
    ~PackageManagerDialog() override;
    static void openManager(const QString &runtimeRoot, const QString &userDataRoot,
                            QWidget *parent = nullptr);

private:
    void refresh();
    void runOperation(const QString &workingText, const std::function<bool(QString *)> &operation);
    QString selectedId() const;

    QSharedPointer<PackageStore> m_store;
    QListWidget *m_packages = nullptr;
    QPushButton *m_importDirectory = nullptr;
    QPushButton *m_importArchive = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_rollback = nullptr;
    QThread *m_worker = nullptr;
};

#endif
