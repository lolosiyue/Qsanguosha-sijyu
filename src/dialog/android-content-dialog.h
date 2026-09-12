#ifndef QSAN_ANDROID_CONTENT_DIALOG_H
#define QSAN_ANDROID_CONTENT_DIALOG_H

#include <QDialog>
#include <QList>
#include <atomic>
#include <functional>

class AndroidContentStore;
class QLabel;
class QListWidget;
class QPushButton;
class QProgressBar;
class QScrollArea;
class QThread;

// The store has one owner. While a worker mutates it the GUI only touches cancel.
class AndroidContentDialog : public QDialog
{
public:
    static void configure(AndroidContentStore *store);
    static bool prepareStartup(QString *error);
    static bool prepareForGame();
    static void openManager(QWidget *parent);
    explicit AndroidContentDialog(bool startup, QWidget *parent = nullptr);
    ~AndroidContentDialog() override;
    void reject() override;
private:
    void refresh();
    void revealStatus();
    void importFile(bool media, bool singleLua);
    void run(const std::function<bool(QString *)> &operation);
    void movePackage(int offset);
    void exportOrder();
    QString selectedPackage() const;
    bool m_startup;
    bool m_closeWhenFinished = false;
    std::atomic_bool m_cancel{false};
    QThread *m_worker = nullptr;
    QLabel *m_status;
    QScrollArea *m_bodyScroll;
    QListWidget *m_packages;
    QProgressBar *m_progress;
    QPushButton *m_continue;
    QPushButton *m_cancelButton;
    QPushButton *m_recoverButton;
    QList<QPushButton *> m_actions;
};
#endif
