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
        QScreen *screen = dialog->screen();
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return {};

        QRect available = screen->availableGeometry();
#ifndef Q_OS_ANDROID
        // Preview uses the main window's viewport, not the whole desktop monitor.
        if (Config.responsiveUiEnabled()) {
            for (QWidget *owner = dialog->parentWidget(); owner; owner = owner->parentWidget()) {
                if (qobject_cast<QMainWindow *>(owner)) {
                    available = available.intersected(QRect(owner->mapToGlobal(QPoint()), owner->size()));
                    break;
                }
            }
        }
#endif
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
        const QSize preferred = dialog->sizeHint().expandedTo(dialog->minimumSizeHint());
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
        QSize bounded = (responsive ? dialog->sizeHint() : preferred).boundedTo(available.size());
        if (responsive && (available.width() < 600 || available.height() > available.width()))
            bounded.setWidth(available.width());
        bounded.setWidth(qMax(1, bounded.width()));
        bounded.setHeight(qMax(1, bounded.height()));
        dialog->setMaximumSize(available.size());
        dialog->resize(bounded);
        const int y = responsive && Config.oneHandedness() != 0
            ? available.bottom() - dialog->height() + 1
            : available.center().y() - dialog->height() / 2;
        const int x = responsive && Config.oneHandedness() == 1 ? available.left()
            : responsive && Config.oneHandedness() == 2 ? available.right() - dialog->width() + 1
            : available.center().x() - dialog->width() / 2;
        dialog->move(x - decor.left(), y - decor.top());

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
