#ifndef QSAN_SPATIAL_FOCUS_FILTER_H
#define QSAN_SPATIAL_FOCUS_FILTER_H

#include <QObject>
#include <QPointer>

class QDialog;
class QWidget;
class QEvent;
class QKeyEvent;

// BP physical-keyboard dialog navigation and modal scrim. SDL actions remain
// exclusively owned by ControllerRouter; synthetic keys bypass this filter.
// Text/list controls keep their native keys and dialog acceptance/cancellation
// always follows the existing native mandatory-choice contract.
class SpatialFocusFilter : public QObject
{
    Q_OBJECT

public:
    explicit SpatialFocusFilter(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool handleDialogKey(QDialog *dialog, QKeyEvent *event);
    QWidget *nearestFocusable(QWidget *root, QWidget *from, Qt::Key direction) const;
    void queueScrimUpdate();
    void updateModalScrim();
    void removeModalScrim();

    QPointer<QWidget> m_scrim;
    QPointer<QWidget> m_scrimHost;
    bool m_scrimUpdateQueued = false;
};

#endif
