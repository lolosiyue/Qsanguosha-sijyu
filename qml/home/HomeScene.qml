import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: root

    width: 1920
    height: 1080
    focus: true

    Keys.onPressed: function(event) {
        switch (event.key) {
        case Qt.Key_1:
            homeController.quickJoin();
            event.accepted = true;
            break;
        case Qt.Key_2:
            homeController.joinGame();
            event.accepted = true;
            break;
        case Qt.Key_3:
            homeController.startServer();
            event.accepted = true;
            break;
        }
    }

    property string visualMode: Config.getValue("VisualMode", "normal")

    Item {
        id: contentHost
        anchors.fill: parent

        HomeBackground {
            id: backgroundLayer
            anchors.fill: parent
        }

        Item {
            id: safeArea

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter

            width: Math.min(parent.width, 1760)

            Image {
                id: logo

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 40
                anchors.topMargin: 28

                width: Math.min(parent.width * 0.13, 230)
                height: width * 0.58

                source: homeController.logoImage
                fillMode: Image.PreserveAspectFit
                mipmap: true
            }

            CharacterLayer {
                id: characterLayer

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: bottomBar.top

                width: parent.width * 0.54
            }

            MainActionPanel {
                id: actionPanel

                anchors.right: parent.right
                anchors.rightMargin: 170
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -25

                width: Math.min(520, parent.width * 0.32)

                onQuickJoinClicked: homeController.quickJoin()
                onJoinGameClicked: homeController.joinGame()
                onStartServerClicked: homeController.startServer()
            }

            HomeSideBar {
                id: sideBar

                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.verticalCenter: parent.verticalCenter

                onSettingsClicked: homeController.openSettings()
                onAboutClicked: homeController.openAbout()
                onUpdateClicked: homeController.checkUpdates()
            }

            HomeBottomBar {
                id: bottomBar

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: parent.width * 0.12
                anchors.rightMargin: parent.width * 0.12
                anchors.bottomMargin: 12

                height: 116

                onHomeClicked: {}
                onGeneralsClicked: homeController.openGenerals()
                onCardsClicked: homeController.openCards()
                onReplaysClicked: homeController.openReplays()
                onSettingsClicked: homeController.openSettings()
            }
        }
    }

    MultiEffect {
        id: visualEffect

        anchors.fill: contentHost
        source: contentHost

        enabled: root.visualMode !== "normal"

        saturation: root.visualMode === "grayscale" ? 0.0 : 1.0
        contrast: root.visualMode === "highcontrast" ? 0.35 : 0.0
    }

    // 鍵盤方向鍵導航圖：各面板按鈕之間的上下左右連線
    Component.onCompleted: {
        // 行動面板 ↔ 右側欄：左右切換（同行對應）
        actionPanel.quickJoinBtn.KeyNavigation.right = sideBar.settingsBtn
        actionPanel.joinGameBtn.KeyNavigation.right = sideBar.aboutBtn
        actionPanel.startServerBtn.KeyNavigation.right = sideBar.updateBtn
        sideBar.settingsBtn.KeyNavigation.left = actionPanel.quickJoinBtn
        sideBar.aboutBtn.KeyNavigation.left = actionPanel.joinGameBtn
        sideBar.updateBtn.KeyNavigation.left = actionPanel.startServerBtn

        // 行動面板：上下（上端連底部列、下端連底部列）
        actionPanel.quickJoinBtn.KeyNavigation.down = actionPanel.joinGameBtn
        actionPanel.joinGameBtn.KeyNavigation.down = actionPanel.startServerBtn
        actionPanel.startServerBtn.KeyNavigation.down = bottomBar.replaysBtn
        actionPanel.startServerBtn.KeyNavigation.up = actionPanel.joinGameBtn
        actionPanel.joinGameBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        actionPanel.quickJoinBtn.KeyNavigation.up = bottomBar.homeBtn

        // 右側欄：上下循環
        sideBar.settingsBtn.KeyNavigation.down = sideBar.aboutBtn
        sideBar.aboutBtn.KeyNavigation.down = sideBar.updateBtn
        sideBar.updateBtn.KeyNavigation.down = sideBar.settingsBtn
        sideBar.updateBtn.KeyNavigation.up = sideBar.aboutBtn
        sideBar.aboutBtn.KeyNavigation.up = sideBar.settingsBtn
        sideBar.settingsBtn.KeyNavigation.up = sideBar.updateBtn

        // 底部導航列：左右循環
        bottomBar.homeBtn.KeyNavigation.right = bottomBar.generalsBtn
        bottomBar.generalsBtn.KeyNavigation.right = bottomBar.cardsBtn
        bottomBar.cardsBtn.KeyNavigation.right = bottomBar.replaysBtn
        bottomBar.replaysBtn.KeyNavigation.right = bottomBar.settingsBtn
        bottomBar.settingsBtn.KeyNavigation.right = bottomBar.homeBtn
        bottomBar.settingsBtn.KeyNavigation.left = bottomBar.replaysBtn
        bottomBar.replaysBtn.KeyNavigation.left = bottomBar.cardsBtn
        bottomBar.cardsBtn.KeyNavigation.left = bottomBar.generalsBtn
        bottomBar.generalsBtn.KeyNavigation.left = bottomBar.homeBtn
        bottomBar.homeBtn.KeyNavigation.left = bottomBar.settingsBtn

        // 底部導航列 → 行動面板：向上
        bottomBar.homeBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        bottomBar.generalsBtn.KeyNavigation.up = actionPanel.joinGameBtn
        bottomBar.cardsBtn.KeyNavigation.up = actionPanel.startServerBtn
        bottomBar.replaysBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        bottomBar.settingsBtn.KeyNavigation.up = actionPanel.joinGameBtn

        // 初始焦點：主按鈕
        actionPanel.quickJoinBtn.forceActiveFocus()
    }
}
