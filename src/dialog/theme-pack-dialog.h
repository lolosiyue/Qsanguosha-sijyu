#ifndef QSAN_THEME_PACK_DIALOG_H
#define QSAN_THEME_PACK_DIALOG_H

#include <QDialog>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Theme pack manager, laid out like a resource-pack screen: installed packs on the left,
// enabled packs on the right with the top one winning. Apply saves the order at once;
// art already on the table keeps its look until the next room is opened.
class ThemePackDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ThemePackDialog(QWidget *parent = nullptr);
    static void openManager(QWidget *parent = nullptr);

private:
    void populate();
    void showDetails(QListWidgetItem *item);
    void moveSelected(QListWidget *from, QListWidget *to);
    void shiftSelected(int delta);
    void updateButtons();
    QStringList pendingOrder() const;
    bool apply();

    QListWidget *m_available = nullptr;
    QListWidget *m_enabled = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_enable = nullptr;
    QPushButton *m_disable = nullptr;
    QPushButton *m_up = nullptr;
    QPushButton *m_down = nullptr;
    QPushButton *m_apply = nullptr;
};

#endif
