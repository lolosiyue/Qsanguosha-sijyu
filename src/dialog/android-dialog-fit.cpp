#include "android-dialog-fit.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLayout>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace {

class AndroidDialogFitFilter final : public QObject
{
public:
    explicit AndroidDialogFitFilter(QApplication *application)
        : QObject(application), m_application(application)
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *dialog = qobject_cast<QDialog *>(watched);
        if (!dialog || !isEligible(dialog))
            return QObject::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::WindowStateChange:
        case QEvent::SafeAreaMarginsChange:
            schedule(dialog);
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

    void refitAll()
    {
        for (QWidget *widget : m_application->topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && isEligible(dialog))
                fit(dialog);
        }
    }

private:
    static bool isEligible(QDialog *dialog)
    {
        if (qobject_cast<QFileDialog *>(dialog))
            return false; // Native Android picker owns its own geometry.
        if (dialog->property("nativeQFileDialog").toBool())
            return false;
        return true;
    }

    void schedule(QDialog *dialog)
    {
        if (m_pending == dialog)
            return;
        m_pending = dialog;
        QTimer::singleShot(0, dialog, [this, dialog] {
            if (m_pending == dialog)
                m_pending.clear();
            if (dialog->isVisible() && isEligible(dialog))
                fit(dialog);
        });
    }

    static QRect availableGeometry(QDialog *dialog)
    {
        QScreen *screen = dialog->screen();
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return {};

        QRect available = screen->availableGeometry();
        if (QWindow *window = dialog->windowHandle()) {
            const QMargins safe = window->safeAreaMargins();
            available.adjust(safe.left(), safe.top(), -safe.right(), -safe.bottom());
        }

        const QInputMethod *inputMethod = QGuiApplication::inputMethod();
        if (inputMethod && inputMethod->isVisible()) {
            QRect keyboard = inputMethod->keyboardRectangle().toRect();
            // QInputMethod reports window coordinates; available is global.
            if (QWindow *focus = QGuiApplication::focusWindow())
                keyboard.translate(focus->mapToGlobal(QPoint(0, 0)));
            if (keyboard.intersects(available))
                available.setBottom(qMin(available.bottom(), keyboard.top() - 1));
        }
        return available;
    }

    static void fit(QDialog *dialog)
    {
        const QRect available = availableGeometry(dialog);
        if (available.isEmpty())
            return;

        if (dialog->property("androidContentDialogOwnScroll").toBool()) {
            // This dialog already scrolls its body and keeps its footer fixed.
            // Use one available rectangle for both origin and extent; Android
            // fullscreen extents can otherwise extend past the system bars.
            dialog->setMinimumSize(0, 0);
            dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
            dialog->setMaximumSize(available.size());
            dialog->setGeometry(available);
            return;
        }

        const QSize preferred = dialog->sizeHint().expandedTo(dialog->minimumSizeHint());
        if ((preferred.height() > available.height() || preferred.width() > available.width())
            && !dialog->property("androidDialogFitWrapped").toBool())
            wrapContents(dialog);
        // Preserve the original content layout's minimum inside the scroll
        // area; the outer dialog must be allowed to fit the visible keyboard area.
        if (dialog->property("androidDialogFitWrapped").toBool()) {
            dialog->setMinimumSize(0, 0);
            dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        }
        QSize bounded = preferred.boundedTo(available.size());
        bounded.setWidth(qMax(1, bounded.width()));
        bounded.setHeight(qMax(1, bounded.height()));
        dialog->setMaximumSize(available.size());
        dialog->resize(bounded);
        dialog->move(available.center() - QPoint(dialog->width() / 2, dialog->height() / 2));

    }

    static void wrapContents(QDialog *dialog)
    {
        QLayout *oldLayout = dialog->layout();
        if (!oldLayout || dialog->property("androidDialogFitWrapped").toBool())
            return;

        auto *body = new QWidget;
        // QWidget transfers the existing layout from its previous widget.
        // Keep grid/form placement, spans, stretches and button-box structure.
        body->setLayout(oldLayout);

        auto *scroll = new QScrollArea(dialog);
        scroll->setWidgetResizable(true);
        scroll->setWidget(body);
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(scroll);
        dialog->setProperty("androidDialogFitWrapped", true);
    }

    QPointer<QApplication> m_application;
    QPointer<QDialog> m_pending;
};

} // namespace

void installAndroidDialogFit(QApplication *application)
{
    if (!application || application->property("androidDialogFitInstalled").toBool())
        return;
    auto *filter = new AndroidDialogFitFilter(application);
    application->installEventFilter(filter);
    application->setProperty("androidDialogFitInstalled", true);
    if (QInputMethod *inputMethod = QGuiApplication::inputMethod()) {
        QObject::connect(inputMethod, &QInputMethod::keyboardRectangleChanged,
                         application, [filter] { filter->refitAll(); });
        QObject::connect(inputMethod, &QInputMethod::visibleChanged,
                         application, [filter] { filter->refitAll(); });
    }
}
