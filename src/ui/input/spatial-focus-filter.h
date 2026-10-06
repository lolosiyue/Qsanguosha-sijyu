#ifndef QSAN_SPATIAL_FOCUS_FILTER_H
#define QSAN_SPATIAL_FOCUS_FILTER_H

#include <QObject>
#include <QPointer>

class QDialog;
class QWidget;
class QEvent;
class QKeyEvent;

// Big-picture (10-foot) dialog navigation. Installed on QApplication only
// while big-picture mode is active and acts only inside modal QDialogs, so a
// normal desktop session is untouched. Contract: arrows move focus to the
// geometrically nearest focusable widget, Enter activates it (falling back
// to the dialog's default button), Escape/Backspace go "back" (reject), and
// PageUp/PageDown stand in for LB/RB as a coarse next/previous focus jump.
// The filter also draws a translucent scrim behind the active modal dialog.
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
