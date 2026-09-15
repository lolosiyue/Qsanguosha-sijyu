#include "game-control-panel.h"

#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QListWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>
#include <limits>

class GameControlPanelTest : public QObject
{
    Q_OBJECT
private:
    static GameActionModel requestModel()
    {
        GameActionModel model;
        model.supported = true;
        model.sessionGeneration = 8;
        model.presentationRevision = 17;
        model.requestId = std::numeric_limits<quint64>::max() - 3;
        model.cards = {{QStringLiteral("7"), QStringLiteral("殺"), true, false, {}},
                       {QStringLiteral("8"), QStringLiteral("閃"), false, false, QStringLiteral("不可使用")}};
        return model;
    }
private slots:
    void keyboardIntentPreservesIdentity()
    {
        GameControlPanel panel;
        auto model = requestModel();
        panel.setModel(model);
        QSignalSpy intents(&panel, &GameControlPanel::intentRequested);
        panel.openPanel();
        auto *cards = panel.findChild<QListWidget *>(QStringLiteral("cardList"));
        QVERIFY(cards);
        cards->setCurrentRow(0);
        QTest::keyClick(cards, Qt::Key_Space);
        QCOMPARE(intents.size(), 1);
        const auto args = intents.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("card"));
        QCOMPARE(args.at(1).toString(), QStringLiteral("7"));
        QVERIFY(args.at(2).toBool());
        QCOMPARE(args.at(3).toULongLong(), model.sessionGeneration);
        QCOMPARE(args.at(4).toULongLong(), model.presentationRevision);
        QCOMPARE(args.at(5).toULongLong(), model.requestId);
        cards->setCurrentRow(1);
        QTest::keyClick(cards, Qt::Key_Space);
        QCOMPARE(intents.size(), 0); // Disabled candidates never become submissions.

        // Trigger instances may share the same translated label. Keyboard
        // input must keep the exact ID, while mandatory requests expose no cancel.
        model.cards.clear();
        model.actionContext = QStringLiteral("trigger-order");
        model.actions = {{QStringLiteral("skill#3:self:self"), QStringLiteral("技能"), true, false, {}},
                         {QStringLiteral("skill#4:self:self"), QStringLiteral("技能"), true, false, {}}};
        model.canCancel = false;
        ++model.presentationRevision;
        panel.setModel(model);
        auto *options = panel.findChild<QListWidget *>(QStringLiteral("optionList"));
        auto *cancel = panel.findChild<QPushButton *>(QStringLiteral("cancelAction"));
        QVERIFY(options && cancel);
        options->setCurrentRow(1);
        QTest::keyClick(options, Qt::Key_Space);
        QCOMPARE(intents.size(), 1);
        QCOMPARE(intents.takeFirst().at(1).toString(), QStringLiteral("skill#4:self:self"));
        QVERIFY(!cancel->isEnabled());
        QTest::keyClick(cancel, Qt::Key_Space);
        QCOMPARE(intents.size(), 0);

        // A disclosed read-only Gongxin display still has an explicit way to finish.
        model.actions.clear();
        model.actionContext = QStringLiteral("gongxin");
        model.prompt = QStringLiteral("已揭示的牌：殺；這次只能檢視");
        model.cards = {{QStringLiteral("7"), QStringLiteral("殺"), false, false, QStringLiteral("僅供檢視")}};
        model.canConfirm = true;
        ++model.requestId;
        ++model.presentationRevision;
        panel.setModel(model);
        auto *confirm = panel.findChild<QPushButton *>(QStringLiteral("confirmAction"));
        auto *prompt = panel.findChild<QLabel *>(QStringLiteral("interactionPrompt"));
        QVERIFY(prompt);
        auto *promptInterface = QAccessible::queryAccessibleInterface(prompt);
        QVERIFY(promptInterface && promptInterface->text(QAccessible::Name).contains(model.prompt));
        QVERIFY(confirm && confirm->isEnabled());
        QTest::keyClick(confirm, Qt::Key_Space);
        QCOMPARE(intents.size(), 1);
        const auto finishView = intents.takeFirst();
        QCOMPARE(finishView.at(0).toString(), QStringLiteral("confirm"));
        QCOMPARE(finishView.at(5).toULongLong(), model.requestId);
    }

    void publicationKeepsItemAndDoesNotSendInput()
    {
        GameControlPanel panel;
        auto model = requestModel();
        panel.setModel(model);
        auto *cards = panel.findChild<QListWidget *>(QStringLiteral("cardList"));
        auto *original = cards->item(0);
        cards->setCurrentRow(0);
        QSignalSpy intents(&panel, &GameControlPanel::intentRequested);
        std::swap(model.cards[0], model.cards[1]);
        model.cards[1].selected = true;
        ++model.presentationRevision;
        panel.setModel(model);
        QCOMPARE(cards->item(1), original);
        QCOMPARE(cards->currentItem(), original);
        QCOMPARE(original->checkState(), Qt::Checked);
        QCOMPARE(intents.size(), 0); // Mouse-side publication is not a second user action.
        auto *iface = QAccessible::queryAccessibleInterface(cards);
        QVERIFY(iface);
        QVERIFY(!iface->text(QAccessible::Name).isEmpty());

        // The same publication contract applies when a keyboard move crosses
        // piles: retain the card cursor, never turn publication into a reply.
        model.cards.clear();
        model.arrangingCards = true;
        model.canMoveToTop = model.canMoveToBottom = true;
        model.topCards = {{QStringLiteral("7"), QStringLiteral("殺"), true, false, {}},
                          {QStringLiteral("9"), QStringLiteral("桃"), true, false, {}}};
        ++model.presentationRevision;
        panel.setModel(model);
        panel.openPanel();
        auto *top = panel.findChild<QListWidget *>(QStringLiteral("topList"));
        auto *bottom = panel.findChild<QListWidget *>(QStringLiteral("bottomList"));
        auto *toBottom = panel.findChild<QPushButton *>(QStringLiteral("orderToBottom"));
        auto *toTop = panel.findChild<QPushButton *>(QStringLiteral("orderToTop"));
        QVERIFY(top && bottom && toBottom && toTop);
        QVERIFY(!top->item(0)->flags().testFlag(Qt::ItemIsUserCheckable));
        top->setCurrentRow(1);
        // Tab past an empty destination must retain the source card's actions.
        QTest::keyClick(top, Qt::Key_Tab);
        QVERIFY(QApplication::focusWidget() != bottom);
        QVERIFY(toBottom->isEnabled());
        QTest::keyClick(toBottom, Qt::Key_Space);
        QCOMPARE(intents.size(), 1);
        const auto move = intents.takeFirst();
        QCOMPARE(move.at(0).toString(), QStringLiteral("order-bottom"));
        QCOMPARE(move.at(1).toString(), QStringLiteral("9"));
        QCOMPARE(move.at(4).toULongLong(), model.presentationRevision);
        panel.setModel(model); // Adapter's pre-dispatch refresh must not lose the pending cursor.
        model.bottomCards.append(model.topCards.takeLast());
        ++model.presentationRevision;
        panel.setModel(model);
        QCOMPARE(bottom->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("9"));
        QVERIFY(toTop->isEnabled());
        QVERIFY(!toBottom->isEnabled());
        QCOMPARE(intents.size(), 0);
    }

    void closingDoesNotCancelRequest()
    {
        GameControlPanel panel;
        panel.setModel(requestModel());
        QSignalSpy intents(&panel, &GameControlPanel::intentRequested);
        panel.openPanel();
        QTest::keyClick(&panel, Qt::Key_Escape);
        QVERIFY(!panel.isVisible());
        QCOMPARE(intents.size(), 0);
    }

    void snapshotIsExplicitAndCopyable()
    {
        GameTextSnapshotDialog snapshot;
        snapshot.showSnapshot(QStringLiteral("手牌：殺、閃"));
        auto *text = snapshot.findChild<QPlainTextEdit *>();
        QVERIFY(text && text->isReadOnly());
        QSignalSpy refresh(&snapshot, &GameTextSnapshotDialog::refreshRequested);
        // Locate actions by stable identity, independently of the active catalog.
        auto *copy = snapshot.findChild<QPushButton *>(QStringLiteral("copySnapshot"));
        auto *update = snapshot.findChild<QPushButton *>(QStringLiteral("refreshSnapshot"));
        QVERIFY(copy && update);
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), text->toPlainText());
        update->click();
        QCOMPARE(refresh.size(), 1);
        QCOMPARE(text->toPlainText(), QStringLiteral("手牌：殺、閃"));
        snapshot.showSnapshot(QStringLiteral("手牌：桃"));
        QCOMPARE(text->toPlainText(), QStringLiteral("手牌：桃"));
    }
};

QTEST_MAIN(GameControlPanelTest)
#include "game-control-panel-test.moc"
