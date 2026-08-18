import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import QSanguosha.HomeFx 1.0
import "."

Item {
    id: root

    focus: true

    Keys.onPressed: function(event) {
        if (root.generalsOpen)
            return
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

    property string visualMode: Config ? Config.getValue("VisualMode", "normal") : "normal"
    readonly property bool generalsOpen: homeController.currentPage === "generals"
    readonly property bool generalPageBusy: {
        if (!generalsOpen)
            return false
        if (generalPage.status !== Loader.Ready || generalPage.item === null)
            return true
        return generalPage.item.catalogPending === true
    }

    Item {
        id: contentHost
        anchors.fill: parent

        HomeBackground {
            id: backgroundLayer
            anchors.fill: parent
        }

        // 固定 1920x1080 設計畫布：依視窗尺寸等比縮放並置中，
        // 使 150% 縮放（邏輯 1280x720）下所有元件等比例縮小而不擁擠
        Item {
            id: uiCanvas

            anchors.centerIn: parent

            width: 1920
            height: 1080

            scale: Math.min(contentHost.width / 1920, contentHost.height / 1080)

            // 角色：佔滿上下，放大且底緣下沉，下半身疊入底部導覽列，略偏中間
            CharacterLayer {
                id: characterLayer

                visible: !root.generalsOpen

                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.horizontalCenterOffset: -parent.width * 0.15

                width: Math.min(parent.width * 0.5, 1300)
            }

            // 底部導覽列：唯一受置中內容區（safe area）限制的元素
            Item {
                id: safeArea

                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter

                width: Math.min(parent.width, 1760)

                HomeBottomBar {
                    id: bottomBar

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12

                    width: 1440
                    height: 136
                    opacity: 0

                    transform: Translate {
                        id: bottomEnter
                        y: 180
                    }

                    onHomeClicked: homeController.openHome()
                    onGeneralsClicked: homeController.openGenerals()
                    onCardsClicked: homeController.openCards()
                    onReplaysClicked: homeController.openReplays()
                    onSettingsClicked: homeController.openSettings()
                }
            }

            // 左上角玩家資訊：頭像＋名稱（與快速加入對話框一致）
            HomePlayerInfo {
                id: playerInfo

                visible: !root.generalsOpen

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 32
                anchors.topMargin: 24
                opacity: 0

                transform: Translate {
                    id: playerEnter
                    x: -180
                }
            }

            // LOGO：移至右側三個主按鈕上方
            Image {
                id: logo

                visible: !root.generalsOpen && source.toString() !== "" && status === Image.Ready

                anchors.right: actionPanel.right
                anchors.bottom: actionPanel.top
                anchors.rightMargin: 4
                anchors.bottomMargin: 20

                width: Math.min(parent.width * 0.12, 220)
                height: width * 0.58

                source: homeController.logoImage
                fillMode: Image.PreserveAspectFit
                mipmap: false
                opacity: 0

                transform: Translate {
                    id: logoEnter
                    y: -20
                }
            }

            MainActionPanel {
                id: actionPanel

                visible: !root.generalsOpen

                anchors.right: sideBar.left
                anchors.rightMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -25

                width: Math.min(520, parent.width * 0.3)
                opacity: 0

                transform: Translate {
                    id: actionEnter
                    x: 250
                }

                onQuickJoinClicked: homeController.quickJoin()
                onJoinGameClicked: homeController.joinGame()
                onStartServerClicked: homeController.startServer()
            }

            HomeSideBar {
                id: sideBar

                visible: !root.generalsOpen

                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                opacity: 0

                transform: Translate {
                    id: sideEnter
                    x: 150
                }

                onSettingsClicked: homeController.openSettings()
                onAboutClicked: homeController.openAbout()
                onUpdateClicked: homeController.checkUpdates()
            }

            Loader {
                id: generalPage

                anchors.fill: parent
                anchors.bottomMargin: 148
                z: 40
                asynchronous: true
                active: root.generalsOpen
                source: "GeneralScene.qml"
                visible: root.generalsOpen && !root.generalPageBusy
                onStatusChanged: {
                    if (status === Loader.Ready && generalPage.item) {
                        root.applyGeneralsNavGraph()
                        if (root.generalsOpen)
                            generalPage.item.takeKeyboard()
                    }
                }
            }

            // 點擊當幀先畫與載入後相同的面板框架，內容格用 skeleton 佔位
            Item {
                id: generalPageSkeleton
                anchors.fill: generalPage
                z: 41
                visible: root.generalsOpen && root.generalPageBusy

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: HomeTheme.generalPageHMargin
                    anchors.rightMargin: HomeTheme.generalPageHMargin
                    anchors.topMargin: HomeTheme.generalPageTopMargin
                    anchors.bottomMargin: HomeTheme.generalPageBottomMargin
                    spacing: HomeTheme.generalPanelGap

                    BASlantedPanel {
                        width: parent.width
                        height: HomeTheme.generalHeaderHeight
                        slant: -0.08
                        cornerRadius: 10
                        shadowBlur: 0
                        shadowOffset: 0
                        topColor: HomeTheme.baDockTop
                        bottomColor: HomeTheme.baDockBottom
                        borderColor: HomeTheme.baDockBorder
                        shadowColor: HomeTheme.baDockShadow

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 28
                            anchors.rightMargin: 28
                            anchors.bottomMargin: 8
                            spacing: 18

                            Text {
                                text: homeController.qtTranslate("GeneralOverview", "General Overview")
                                color: HomeTheme.btnSecondaryText
                                font.pixelSize: 28
                                font.bold: true
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.maximumWidth: 480
                                Layout.preferredHeight: 48
                                radius: 24
                                color: HomeTheme.btnSecondary
                                border.color: HomeTheme.btnSecondaryBorder
                            }

                            Rectangle {
                                Layout.preferredWidth: 220
                                Layout.preferredHeight: 48
                                radius: 24
                                color: HomeTheme.btnSecondary
                                border.color: HomeTheme.btnSecondaryBorder
                            }

                            Rectangle {
                                Layout.preferredWidth: 140
                                Layout.preferredHeight: 48
                                radius: 8
                                color: HomeTheme.btnSecondary
                                border.color: HomeTheme.btnSecondaryBorder
                            }

                            Text {
                                text: "0"
                                color: HomeTheme.btnSecondaryText
                                font.pixelSize: 22
                                font.bold: true
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        height: parent.height - HomeTheme.generalHeaderHeight - HomeTheme.generalPanelGap
                        spacing: HomeTheme.generalPanelGap

                        BASlantedPanel {
                            width: Math.round((parent.width - HomeTheme.generalPanelGap) * HomeTheme.generalListShare)
                            height: parent.height
                            slant: 0
                            cornerRadius: 10
                            shadowBlur: 0
                            shadowOffset: 0
                            topColor: HomeTheme.baDockTop
                            bottomColor: HomeTheme.baDockBottom
                            borderColor: HomeTheme.baDockBorder
                            shadowColor: HomeTheme.baDockShadow

                            Item {
                                id: skGrid
                                anchors.fill: parent
                                anchors.margins: HomeTheme.generalGridMargin
                                property int cellW: HomeTheme.generalCellWidth(width)
                                property int cellH: HomeTheme.generalCellHeight(width)
                                property int cols: HomeTheme.generalCellColumns(width)

                                Grid {
                                    anchors.fill: parent
                                    columns: skGrid.cols

                                    Repeater {
                                        model: skGrid.cols * 3
                                        Item {
                                            width: skGrid.cellW
                                            height: skGrid.cellH
                                            SkeletonBlock {
                                                anchors.fill: parent
                                                anchors.margins: HomeTheme.generalCellInset
                                                radius: 8
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        BASlantedPanel {
                            width: Math.round((parent.width - HomeTheme.generalPanelGap) * (1.0 - HomeTheme.generalListShare))
                            height: parent.height
                            slant: 0
                            cornerRadius: 10
                            shadowBlur: 0
                            shadowOffset: 0
                            topColor: HomeTheme.baDockTop
                            bottomColor: HomeTheme.baDockBottom
                            borderColor: HomeTheme.baDockBorder
                            clip: true

                            Row {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 12

                                SkeletonBlock {
                                    width: Math.round(parent.width * 0.33)
                                    height: Math.min(parent.height - 28, Math.round(parent.width * 0.33 * 1.45))
                                    radius: 10
                                }

                                Column {
                                    width: parent.width - parent.children[0].width - 12
                                    spacing: 8

                                    SkeletonBlock { width: 140; height: 16 }
                                    SkeletonBlock { width: 220; height: 32 }
                                    Row {
                                        spacing: 10
                                        Repeater {
                                            model: 5
                                            SkeletonBlock { width: 22; height: 22; radius: 4 }
                                        }
                                    }
                                    Row {
                                        spacing: 8
                                        Repeater {
                                            model: 3
                                            SkeletonBlock { width: 88; height: 24; radius: 4 }
                                        }
                                    }
                                    Row {
                                        spacing: 8
                                        SkeletonBlock { width: 88; height: 36; radius: 8 }
                                        SkeletonBlock { width: 88; height: 36; radius: 8 }
                                    }
                                    SkeletonBlock { width: parent.width; height: 14 }
                                    SkeletonBlock { width: parent.width * 0.88; height: 14 }
                                    SkeletonBlock { width: parent.width * 0.62; height: 14 }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    MultiEffect {
        id: visualEffect

        anchors.fill: contentHost
        visible: enabled
        enabled: root.visualMode !== "normal"
        source: enabled ? contentHost : null

        saturation: root.visualMode === "grayscale" ? 0.0 : 1.0
        contrast: root.visualMode === "highcontrast" ? 0.35 : 0.0
    }

    HomePointerFx {
        anchors.fill: parent
        z: 200
    }

    // 鍵盤方向鍵導航圖：各面板按鈕之間的上下左右連線
    function applyHomeNavGraph() {
        actionPanel.quickJoinBtn.KeyNavigation.right = sideBar.settingsBtn
        actionPanel.joinGameBtn.KeyNavigation.right = sideBar.aboutBtn
        actionPanel.startServerBtn.KeyNavigation.right = sideBar.updateBtn
        sideBar.settingsBtn.KeyNavigation.left = actionPanel.quickJoinBtn
        sideBar.aboutBtn.KeyNavigation.left = actionPanel.joinGameBtn
        sideBar.updateBtn.KeyNavigation.left = actionPanel.startServerBtn

        actionPanel.quickJoinBtn.KeyNavigation.down = actionPanel.joinGameBtn
        actionPanel.joinGameBtn.KeyNavigation.down = actionPanel.startServerBtn
        actionPanel.startServerBtn.KeyNavigation.down = bottomBar.replaysBtn
        actionPanel.startServerBtn.KeyNavigation.up = actionPanel.joinGameBtn
        actionPanel.joinGameBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        actionPanel.quickJoinBtn.KeyNavigation.up = bottomBar.homeBtn

        sideBar.settingsBtn.KeyNavigation.down = sideBar.aboutBtn
        sideBar.aboutBtn.KeyNavigation.down = sideBar.updateBtn
        sideBar.updateBtn.KeyNavigation.down = sideBar.settingsBtn
        sideBar.updateBtn.KeyNavigation.up = sideBar.aboutBtn
        sideBar.aboutBtn.KeyNavigation.up = sideBar.settingsBtn
        sideBar.settingsBtn.KeyNavigation.up = sideBar.updateBtn

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

        bottomBar.homeBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        bottomBar.generalsBtn.KeyNavigation.up = actionPanel.joinGameBtn
        bottomBar.cardsBtn.KeyNavigation.up = actionPanel.startServerBtn
        bottomBar.replaysBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        bottomBar.settingsBtn.KeyNavigation.up = actionPanel.joinGameBtn

        bottomBar.homeBtn.KeyNavigation.tab = bottomBar.generalsBtn
        bottomBar.generalsBtn.KeyNavigation.tab = bottomBar.cardsBtn
        bottomBar.cardsBtn.KeyNavigation.tab = bottomBar.replaysBtn
        bottomBar.replaysBtn.KeyNavigation.tab = bottomBar.settingsBtn
        bottomBar.settingsBtn.KeyNavigation.tab = bottomBar.homeBtn
        bottomBar.homeBtn.KeyNavigation.backtab = bottomBar.settingsBtn
        bottomBar.generalsBtn.KeyNavigation.backtab = bottomBar.homeBtn
        bottomBar.cardsBtn.KeyNavigation.backtab = bottomBar.generalsBtn
        bottomBar.replaysBtn.KeyNavigation.backtab = bottomBar.cardsBtn
        bottomBar.settingsBtn.KeyNavigation.backtab = bottomBar.replaysBtn
    }

    function applyGeneralsNavGraph() {
        var g = generalPage.item
        if (!g)
            return
        g.banBtn.KeyNavigation.tab = bottomBar.generalsBtn
        g.searchField.KeyNavigation.backtab = bottomBar.settingsBtn
        bottomBar.homeBtn.KeyNavigation.up = g.searchField
        bottomBar.generalsBtn.KeyNavigation.up = g.searchField
        bottomBar.cardsBtn.KeyNavigation.up = g.searchField
        bottomBar.replaysBtn.KeyNavigation.up = g.searchField
        bottomBar.settingsBtn.KeyNavigation.up = g.searchField
        bottomBar.settingsBtn.KeyNavigation.tab = g.searchField
        bottomBar.homeBtn.KeyNavigation.backtab = g.banBtn
    }

    function restoreHomeKeyboard() {
        applyHomeNavGraph()
        if (actionPanel.visible)
            actionPanel.quickJoinBtn.forceActiveFocus()
        else
            bottomBar.homeBtn.forceActiveFocus()
    }

    Component.onCompleted: {
        applyHomeNavGraph()
        actionPanel.quickJoinBtn.forceActiveFocus()
        enterAnim.start()
    }

    Connections {
        target: homeController
        function onCurrentPageChanged() {
            bottomBar.currentIndex = root.generalsOpen ? 1 : 0
            if (root.generalsOpen) {
                if (generalPage.item) {
                    root.applyGeneralsNavGraph()
                    generalPage.item.takeKeyboard()
                }
            } else {
                Qt.callLater(root.restoreHomeKeyboard)
            }
        }
    }

    ParallelAnimation {
        id: enterAnim

        NumberAnimation {
            target: bottomEnter
            property: "y"
            to: 0
            duration: 320
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: bottomBar
            property: "opacity"
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: actionEnter
            property: "x"
            to: 0
            duration: 300
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: actionPanel
            property: "opacity"
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: sideEnter
            property: "x"
            to: 0
            duration: 260
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: sideBar
            property: "opacity"
            to: 1
            duration: 180
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: playerEnter
            property: "x"
            to: 0
            duration: 280
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: playerInfo
            property: "opacity"
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: logoEnter
            property: "y"
            to: 0
            duration: 300
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: logo
            property: "opacity"
            to: 1
            duration: 220
            easing.type: Easing.OutCubic
        }
    }
}
