#include "android-dialog-fit.h"
#include "settings.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QMainWindow>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QTabWidget>
#include <QScopedValueRollback>
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
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#include <QDesktopWidget>
#endif

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
        if (qobject_cast<QMainWindow *>(watched)
            && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
            QTimer::singleShot(0, this, [this] { refitAll(); });
        auto *dialog = qobject_cast<QDialog *>(watched);
        if (!dialog || !isEligible(dialog))
            return QObject::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::WindowStateChange:
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        case QEvent::SafeAreaMarginsChange:
#endif
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
            if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible() && isEligible(dialog))
                fit(dialog);
        }
    }

private:
    static bool isEligible(QDialog *dialog)
    {
#ifndef Q_OS_ANDROID
        if (!Config.responsiveUiEnabled() && !dialog->property("androidDialogFitWrapped").toBool())
            return false;
#endif
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
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        QScreen *screen = dialog->screen();
#else
        QScreen *screen = QGuiApplication::screens().value(QApplication::desktop()->screenNumber(dialog));
#endif
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return {};

        QWidget *mainWindow = nullptr;
        for (QWidget *owner = dialog->parentWidget(); owner; owner = owner->parentWidget()) {
            if (qobject_cast<QMainWindow *>(owner)) {
                mainWindow = owner;
                break;
            }
        }
        // A dialog already nudged onto the next monitor must not follow itself.
        // Fit against the main window's screen, then clip to that window.
        if (mainWindow && mainWindow->screen())
            screen = mainWindow->screen();
        QRect available = screen->availableGeometry();
#ifndef Q_OS_ANDROID
        if (Config.responsiveUiEnabled() && mainWindow) {
            available = available.intersected(QRect(mainWindow->mapToGlobal(QPoint()), mainWindow->size()));
        }
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        if (QWindow *window = dialog->windowHandle()) {
            const QMargins safe = window->safeAreaMargins();
            available.adjust(safe.left(), safe.top(), -safe.right(), -safe.bottom());
        }
#endif

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
        // Layout transfer can deliver Resize synchronously; one pass owns geometry.
        static bool fitting = false;
        if (fitting) return;
        QScopedValueRollback<bool> guard(fitting, true);
        // resize()/setGeometry() size the client area while move() places the
        // frame; keep the desktop title bar and borders inside the viewport too.
        const QRect frame = dialog->frameGeometry(), client = dialog->geometry();
        const QMargins decor(client.left() - frame.left(), client.top() - frame.top(),
            frame.right() - client.right(), frame.bottom() - client.bottom());
        const QRect available = availableGeometry(dialog).marginsRemoved(decor);
        if (available.isEmpty())
            return;

#ifndef Q_OS_ANDROID
        // A wide desktop keeps each dialog's designed size. The fitter restacks
        // controls and resizes from sizeHint, which is what makes every dialog
        // look wrong on a normal monitor. Narrow and portrait previews still fit.
        const bool narrowViewport = available.width() < 600 || available.height() > available.width();
        if (!narrowViewport)
            return;
#endif

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

        const bool responsive = Config.responsiveUiEnabled();
        QSize preferred = dialog->sizeHint().expandedTo(dialog->minimumSizeHint());
        // Remember a real content size before wrapping clears the outer minimum.
        // A later pass can see a 0x0 scroll-area hint and must not replace this.
        if (preferred.width() > 1 && preferred.height() > 1)
            dialog->setProperty("androidDialogFitContentSize", preferred);
        else {
            const QSize saved = dialog->property("androidDialogFitContentSize").toSize();
            if (saved.width() > 1 && saved.height() > 1)
                preferred = saved;
        }
        if ((responsive || preferred.height() > available.height() || preferred.width() > available.width())
            && !dialog->property("androidDialogFitWrapped").toBool())
            wrapContents(dialog);
        // Preserve the original content layout's minimum inside the scroll
        // area; the outer dialog must be allowed to fit the visible keyboard area.
        if (dialog->property("androidDialogFitWrapped").toBool()) {
            dialog->setMinimumSize(0, 0);
            dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        }
        QWidget *footer = dialog->findChild<QWidget *>(QStringLiteral("responsiveDialogFooter"));
        if (footer) {
            const Qt::Alignment side = responsive && Config.oneHandedness() == 1 ? Qt::AlignLeft
                : responsive && Config.oneHandedness() == 2 ? Qt::AlignRight : Qt::AlignHCenter;
            dialog->layout()->setAlignment(footer, side);
            const bool narrow = responsive && available.width() < 600;
            if (auto *box = qobject_cast<QDialogButtonBox *>(footer))
                box->setOrientation(narrow ? Qt::Vertical : Qt::Horizontal);
            else if (auto *box = qobject_cast<QBoxLayout *>(footer->layout()))
                box->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        }
        QSize hint = responsive ? dialog->sizeHint().expandedTo(dialog->minimumSizeHint()) : preferred;
        // After the scroll wrap, sizeHint() can be 0x0 or 1x1 until polish.
        // qMax(1, 0) then asks Windows for a 1px window, which a bottom-right
        // one-handed place pushes onto the neighbouring monitor.
        if (hint.width() <= 1 || hint.height() <= 1)
            hint = preferred;
        if (hint.width() <= 1 || hint.height() <= 1)
            return;
        QSize bounded = hint.boundedTo(available.size());
        if (responsive && (available.width() < 600 || available.height() > available.width()))
            bounded.setWidth(available.width());
        if (bounded.width() < 1 || bounded.height() < 1)
            return;
        dialog->setMaximumSize(available.size().expandedTo(bounded));
        dialog->resize(bounded);
        // Place the size we just requested. dialog->height() can still be the
        // rejected 1px frame if Windows has not applied the resize yet.
        const int placedWidth = bounded.width();
        const int placedHeight = bounded.height();
        // Thumb-corner placement is for a phone or a narrow portrait preview.
        // On a wide desktop the same right-hand setting parks every dialog on
        // the monitor edge, which is the next display on a multi-monitor desk.
        const bool narrow = available.width() < 600 || available.height() > available.width();
#ifdef Q_OS_ANDROID
        const bool thumbAnchor = Config.oneHandedness() != 0;
#else
        const bool thumbAnchor = responsive && narrow && Config.oneHandedness() != 0;
#endif
        const int y = thumbAnchor
            ? available.bottom() - placedHeight + 1
            : available.center().y() - placedHeight / 2;
        const int x = !thumbAnchor ? available.center().x() - placedWidth / 2
            : Config.oneHandedness() == 1 ? available.left()
            : Config.oneHandedness() == 2 ? available.right() - placedWidth + 1
            : available.center().x() - placedWidth / 2;
        QScreen *screen = dialog->screen();
        for (QWidget *owner = dialog->parentWidget(); owner; owner = owner->parentWidget()) {
            if (auto *window = qobject_cast<QMainWindow *>(owner); window && window->screen()) {
                screen = window->screen();
                break;
            }
        }
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        int frameX = x - decor.left();
        int frameY = y - decor.top();
        if (screen) {
            const QRect screenBounds = screen->availableGeometry();
            const int frameW = placedWidth + decor.left() + decor.right();
            const int frameH = placedHeight + decor.top() + decor.bottom();
            frameX = qBound(screenBounds.left(), frameX, qMax(screenBounds.left(), screenBounds.right() - frameW + 1));
            frameY = qBound(screenBounds.top(), frameY, qMax(screenBounds.top(), screenBounds.bottom() - frameH + 1));
        }
        dialog->move(frameX, frameY);

    }

    static void wrapContents(QDialog *dialog)
    {
        QLayout *oldLayout = dialog->layout();
        if (!oldLayout || dialog->property("androidDialogFitWrapped").toBool())
            return;

        QWidget *footer = nullptr;
        // Detach only a recognised bottom action row. Confirmation stays outside scrolling.
        if (auto *column = qobject_cast<QVBoxLayout *>(oldLayout); column && column->count()) {
            QLayoutItem *last = column->itemAt(column->count() - 1);
            if (qobject_cast<QDialogButtonBox *>(last->widget())) {
                last = column->takeAt(column->count() - 1);
                footer = last->widget();
                delete last;
            } else if (auto *row = qobject_cast<QHBoxLayout *>(last->layout())) {
                bool buttonsOnly = true;
                for (int i = 0; i < row->count(); ++i) {
                    auto *item = row->itemAt(i);
                    if (!item->spacerItem() && !qobject_cast<QAbstractButton *>(item->widget()))
                        buttonsOnly = false;
                }
                if (buttonsOnly) {
                    column->takeAt(column->count() - 1);
                    row->setParent(nullptr);
                    footer = new QWidget(dialog);
                    footer->setLayout(row);
                }
            }
        }
        if (footer) {
            footer->setObjectName(QStringLiteral("responsiveDialogFooter"));
            for (auto *button : footer->findChildren<QAbstractButton *>())
                button->setMinimumHeight(48);
        }
        if (Config.responsiveUiEnabled()) {
            for (QWidget *control : dialog->findChildren<QWidget *>()) {
                if (qobject_cast<QAbstractButton *>(control) || qobject_cast<QLineEdit *>(control)
                    || qobject_cast<QComboBox *>(control) || qobject_cast<QAbstractSpinBox *>(control))
                    control->setMinimumHeight(48);
            }
        }
        for (auto *form : dialog->findChildren<QFormLayout *>())
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        for (auto *tabs : dialog->findChildren<QTabWidget *>())
            tabs->setUsesScrollButtons(true);
        auto *body = new QWidget;
        // QWidget transfers the existing layout from its previous widget.
        // Keep grid/form placement, spans, stretches and button-box structure.
        body->setLayout(oldLayout);

        auto *scroll = new QScrollArea(dialog);
        scroll->setWidgetResizable(true);
        scroll->setWidget(body);
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(scroll, 1);
        if (footer) layout->addWidget(footer);
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
    QObject::connect(&Config, &Settings::uiLayoutChanged, filter, [filter] {
        QTimer::singleShot(0, filter, [filter] { filter->refitAll(); });
    });
    QObject::connect(application, &QApplication::focusChanged, filter,
        [](QWidget *, QWidget *focused) {
            for (QWidget *parent = focused; parent; parent = parent->parentWidget()) {
                if (auto *scroll = qobject_cast<QScrollArea *>(parent))
                    scroll->ensureWidgetVisible(focused);
            }
        });
    application->setProperty("androidDialogFitInstalled", true);
    if (QInputMethod *inputMethod = QGuiApplication::inputMethod()) {
        QObject::connect(inputMethod, &QInputMethod::keyboardRectangleChanged,
                         application, [filter] { filter->refitAll(); });
        QObject::connect(inputMethod, &QInputMethod::visibleChanged,
                         application, [filter] { filter->refitAll(); });
    }
}
