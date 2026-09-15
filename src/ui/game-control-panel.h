#ifndef GAME_CONTROL_PANEL_H
#define GAME_CONTROL_PANEL_H

#include "game-action-model.h"
#include <QDialog>
#include <QPointer>
#include <QMap>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QGroupBox;
class QVBoxLayout;

// A standard-widget view of the same selection used by the graphical table.
// All intent carries the displayed request/revision; this widget sends no replies.
class GameControlPanel : public QDialog
{
    Q_OBJECT
public:
    explicit GameControlPanel(QWidget *parent = nullptr);
    void setModel(const GameActionModel &model);
    void openPanel();

signals:
    void intentRequested(const QString &kind, const QString &id, bool selected,
                         quint64 generation, quint64 revision, quint64 requestId);

private:
    bool eventFilter(QObject *object, QEvent *event) override;
    QListWidget *makeList(const QString &title, const QString &kind,
                         QVBoxLayout *section = nullptr, bool checkable = true);
    void updateList(QListWidget *list, const QList<GameActionEntry> &entries, bool checkable = true);
    void updateOrderButtons();
    void moveOrderCard(const QString &kind);
    void focusPrimaryControl();
    void sendIntent(const QString &kind, const QString &id = QString(), bool selected = false);
    GameActionModel m_model;
    QVBoxLayout *m_contentLayout;
    QLabel *m_prompt;
    QLabel *m_reason;
    QListWidget *m_actions;
    QListWidget *m_skills;
    QListWidget *m_cards;
    QListWidget *m_players;
    QMap<QListWidget *, QLabel *> m_listLabels;
    QGroupBox *m_orderGroup;
    QListWidget *m_topCards;
    QListWidget *m_bottomCards;
    QListWidget *m_orderList = nullptr;
    QString m_pendingOrderCard;
    QPushButton *m_earlier;
    QPushButton *m_later;
    QPushButton *m_toTop;
    QPushButton *m_toBottom;
    QPushButton *m_confirm;
    QPushButton *m_cancel;
    QPushButton *m_finish;
    QPointer<QWidget> m_returnFocus;
};

class GameTextSnapshotDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GameTextSnapshotDialog(QWidget *parent = nullptr);
    void showSnapshot(const QString &text);
signals:
    void refreshRequested();
private:
    QPlainTextEdit *m_text;
    QPointer<QWidget> m_returnFocus;
};

#endif
