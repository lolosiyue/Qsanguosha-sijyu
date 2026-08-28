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
        if (root.subPageOpen)
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

    property string visualMode: homeController ? homeController.visualMode : "normal"
    property real uiScale: 1.0
    readonly property bool generalsOpen: homeController.currentPage === "generals"
    readonly property bool cardsOpen: homeController.currentPage === "cards"
    readonly property bool subPageOpen: generalsOpen || cardsOpen
    property bool generalsMounted: false
    property bool cardsMounted: false
    readonly property bool generalPageBusy: {
        if (!generalsOpen)
            return false
        return generalPage.status !== Loader.Ready || generalPage.item === null
    }
    readonly property bool cardPageBusy: {
        if (!cardsOpen)
            return false
        return cardPage.status !== Loader.Ready || cardPage.item === null
    }
    readonly property bool cardsReadyForSmoke: cardsOpen && cardPage.status === Loader.Ready
                                                && cardPage.item !== null
                                                && cardPage.item.readyForSmoke
    readonly property int cardsModelCount: cardPage.item ? cardPage.item.modelCount : 0
    readonly property int cardsDetailCardId: cardPage.item ? cardPage.item.detailCardId : -1

    onGeneralsOpenChanged: {
        if (generalsOpen)
            generalsMounted = true
    }

    onCardsOpenChanged: {
        if (cardsOpen)
            cardsMounted = true
    }

    Item {
        id: contentHost
        anchors.fill: parent
        clip: true
        // 僅在灰階/高對比時離屏合成；Qt 6 saturation -1.0 才是去色（0.0 為不變）
        layer.enabled: root.visualMode !== "normal"
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            saturation: root.visualMode === "grayscale" ? -1.0 : 0.0
            contrast: root.visualMode === "highcontrast" ? 0.35 : 0.0
        }

        HomeBackground {
            id: backgroundLayer
            anchors.fill: parent
        }

        // 固定 1920×1080 設計畫布：永遠完整 fit 進視窗（框架不動）。
        // UIScale 只作用在各元素自己的 transform，不缩放整張畫布。
        Item {
            id: uiCanvas

            anchors.centerIn: parent

            width: 1920
            height: 1080

            scale: Math.min(contentHost.width / 1920, contentHost.height / 1080)

            // 角色：佔滿上下，放大且底緣下沉，下半身疊入底部導覽列，略偏中間
            CharacterLayer {
                id: characterLayer

                visible: !root.subPageOpen

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

                    transformOrigin: Item.Bottom
                    scale: root.uiScale

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

                visible: !root.subPageOpen

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 32
                anchors.topMargin: 24
                opacity: 0

                transformOrigin: Item.TopLeft
                scale: root.uiScale

                transform: Translate {
                    id: playerEnter
                    x: -180
                }
            }

            // LOGO：移至右側三個主按鈕上方
            Image {
                id: logo

                visible: !root.subPageOpen && source.toString() !== "" && status === Image.Ready

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

                transformOrigin: Item.BottomRight
                scale: root.uiScale

                transform: Translate {
                    id: logoEnter
                    y: -20
                }
            }

            MainActionPanel {
                id: actionPanel

                visible: !root.subPageOpen

                anchors.right: sideBar.left
                anchors.rightMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -25

                width: Math.min(520, parent.width * 0.3)
                opacity: 0

                transformOrigin: Item.Right
                scale: root.uiScale

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

                visible: !root.subPageOpen

                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                opacity: 0

                transformOrigin: Item.Right
                scale: root.uiScale

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
                active: root.generalsMounted
                source: "GeneralScene.qml"
                visible: root.generalsOpen && !root.generalPageBusy
                onStatusChanged: {
                    if (status === Loader.Ready && generalPage.item) {
                        generalPage.item.uiScale = root.uiScale
                        if (root.generalsOpen) {
                            root.applyGeneralsNavGraph()
                            generalPage.item.takeKeyboard()
                        }
                    }
                }
            }

            Binding {
                target: generalPage.item
                property: "uiScale"
                value: root.uiScale
                when: generalPage.item !== null
            }

            // Loader 編譯期間先畫面板骨架；Ready 後揭 GeneralScene，立繪再分幀載入
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

            Loader {
                id: cardPage

                anchors.fill: parent
                anchors.bottomMargin: 148
                z: 40
                asynchronous: true
                active: root.cardsMounted
                source: "CardScene.qml"
                visible: root.cardsOpen && !root.cardPageBusy
                onStatusChanged: {
                    if (status === Loader.Ready && cardPage.item) {
                        cardPage.item.uiScale = root.uiScale
                        if (root.cardsOpen) {
                            root.applyCardsNavGraph()
                            cardPage.item.takeKeyboard()
                        }
                    }
                }
            }

            Binding {
                target: cardPage.item
                property: "uiScale"
                value: root.uiScale
                when: cardPage.item !== null
            }

            Connections {
                target: cardPage.item
                ignoreUnknownSignals: true
                function onNavigationEndpointChanged() {
                    if (root.cardsOpen)
                        Qt.callLater(root.applyCardsNavGraph)
                }
            }

            Item {
                anchors.fill: cardPage
                z: 41
                visible: root.cardsOpen && root.cardPageBusy

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: HomeTheme.cardPageHMargin
                    anchors.rightMargin: HomeTheme.cardPageHMargin
                    anchors.topMargin: HomeTheme.cardPageTopMargin
                    anchors.bottomMargin: HomeTheme.cardPageBottomMargin
                    spacing: HomeTheme.cardPanelGap

                    BASlantedPanel {
                        width: parent.width
                        height: HomeTheme.cardHeaderHeight
                        slant: -0.05
                        cornerRadius: HomeTheme.cardPanelRadius
                        shadowBlur: 0
                        shadowOffset: 0
                        topColor: HomeTheme.cardPanelTop
                        bottomColor: HomeTheme.cardPanelBottom
                        borderColor: HomeTheme.cardPanelBorder
                        transformOrigin: Item.Top
                        scale: root.uiScale

                        Row {
                            anchors.fill: parent
                            anchors.margins: HomeTheme.cardSkeletonHeaderPadding
                            spacing: HomeTheme.cardPanelGap
                            SkeletonBlock {
                                width: HomeTheme.cardHeaderButtonWidth
                                height: HomeTheme.cardActionButtonExtent
                                radius: HomeTheme.cardControlRadius
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: HomeTheme.cardDetailMetaGap
                                SkeletonBlock {
                                    width: HomeTheme.cardSkeletonTitleWidth
                                    height: HomeTheme.cardSkeletonTitleHeight
                                }
                                SkeletonBlock {
                                    width: HomeTheme.cardSkeletonSubtitleWidth
                                    height: HomeTheme.cardSkeletonSubtitleHeight
                                }
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        height: parent.height - HomeTheme.cardHeaderHeight - HomeTheme.cardPanelGap
                        spacing: HomeTheme.cardPanelGap

                        SkeletonBlock {
                            width: HomeTheme.cardFilterWidth
                            height: parent.height
                            radius: HomeTheme.cardPanelRadius
                            transformOrigin: Item.TopLeft
                            scale: root.uiScale
                        }

                        Item {
                            width: parent.width - HomeTheme.cardFilterWidth - HomeTheme.cardDetailWidth
                                   - HomeTheme.cardPanelGap * 2
                            height: parent.height
                            transformOrigin: Item.Top
                            scale: root.uiScale

                            Grid {
                                id: cardSkeletonGrid
                                anchors.fill: parent
                                anchors.margins: HomeTheme.cardGridGap
                                columns: 4
                                rows: 3
                                property real tileWidth: width / columns
                                property real tileHeight: height / rows
                                Repeater {
                                    model: 12
                                    Item {
                                        width: cardSkeletonGrid.tileWidth
                                        height: cardSkeletonGrid.tileHeight
                                        SkeletonBlock {
                                            anchors.fill: parent
                                            anchors.margins: HomeTheme.cardGridGap / 2
                                            radius: HomeTheme.cardPanelRadius
                                        }
                                    }
                                }
                            }
                        }

                        SkeletonBlock {
                            width: HomeTheme.cardDetailWidth
                            height: parent.height
                            radius: HomeTheme.cardPanelRadius
                            transformOrigin: Item.TopRight
                            scale: root.uiScale
                        }
                    }
                }
            }
        }
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

    function applyCardsNavGraph() {
        var c = cardPage.item
        if (!c)
            return
        c.backButton.KeyNavigation.tab = c.sortControl
        c.backButton.KeyNavigation.backtab = bottomBar.settingsBtn
        c.sortControl.KeyNavigation.tab = c.themeButton
        c.sortControl.KeyNavigation.backtab = c.backButton
        c.themeButton.KeyNavigation.tab = c.reloadButton
        c.themeButton.KeyNavigation.backtab = c.sortControl
        c.reloadButton.KeyNavigation.tab = c.searchField
        c.reloadButton.KeyNavigation.backtab = c.themeButton
        c.searchField.KeyNavigation.backtab = c.reloadButton
        c.lastControl.KeyNavigation.tab = bottomBar.cardsBtn
        c.lastControl.KeyNavigation.down = bottomBar.cardsBtn
        bottomBar.homeBtn.KeyNavigation.up = c.searchField
        bottomBar.generalsBtn.KeyNavigation.up = c.searchField
        bottomBar.cardsBtn.KeyNavigation.up = c.lastControl
        bottomBar.replaysBtn.KeyNavigation.up = c.searchField
        bottomBar.settingsBtn.KeyNavigation.up = c.searchField
        bottomBar.settingsBtn.KeyNavigation.tab = c.backButton
        bottomBar.cardsBtn.KeyNavigation.backtab = c.lastControl
    }

    function restoreHomeKeyboard() {
        applyHomeNavGraph()
        if (actionPanel.visible)
            actionPanel.quickJoinBtn.forceActiveFocus()
        else
            bottomBar.homeBtn.forceActiveFocus()
    }

    // 首頁站穩後再偷載：800ms 空等，避免跟進場動畫搶 IO／解碼。
    Item {
        id: generalArtPrefetch
        x: -4000
        y: -4000
        width: 1
        height: 1
        opacity: 0
        enabled: false
        z: -1

        readonly property int gridInnerWidth: {
            var colW = 1920 - HomeTheme.generalPageHMargin * 2
            var listW = Math.round((colW - HomeTheme.generalPanelGap)
                                   * HomeTheme.generalListShare)
            return Math.max(1, listW - HomeTheme.generalGridMargin * 2)
        }
        readonly property int cols: {
            var saved = homeController.generalGridColumns()
            var v = saved > 0 ? saved : HomeTheme.generalGridMinColumns
            return Math.max(HomeTheme.generalGridMinColumns,
                            Math.min(v, HomeTheme.generalGridMaxColumns - 1))
        }
        readonly property int cellW: HomeTheme.generalCellWidth(gridInnerWidth, cols)
        readonly property int cellH: HomeTheme.generalCellHeight(gridInnerWidth, cols)
        readonly property int artW: Math.max(1, cellW - HomeTheme.generalCellInset * 2)
        readonly property int artH: Math.max(1, cellH - HomeTheme.generalCellInset * 2)
        property int mounted: 0
        property int target: 0

        Repeater {
            model: generalArtPrefetch.mounted
            Image {
                width: generalArtPrefetch.artW
                height: generalArtPrefetch.artH
                asynchronous: true
                cache: true
                sourceSize.width: Math.ceil(width)
                sourceSize.height: Math.ceil(height)
                source: homeController.prefetchArtUrl(index)
            }
        }
    }

    Timer {
        id: generalPrefetchStart
        interval: 800
        repeat: false
        onTriggered: root.startGeneralPrefetch()
    }

    Timer {
        id: generalPrefetchTick
        interval: 32
        repeat: true
        onTriggered: {
            if (generalArtPrefetch.mounted >= generalArtPrefetch.target) {
                stop()
                return
            }
            generalArtPrefetch.mounted += 1
        }
    }

    function startGeneralPrefetch() {
        homeController.warmGeneralCatalog()
        var n = homeController.generalModel ? homeController.generalModel.count : 0
        generalArtPrefetch.target = Math.min(16, n)
        if (generalArtPrefetch.target > 0) {
            generalArtPrefetch.mounted = 1
            if (generalArtPrefetch.target > 1)
                generalPrefetchTick.start()
        }
        generalsMounted = true
    }

    Component.onCompleted: {
        applyHomeNavGraph()
        actionPanel.quickJoinBtn.forceActiveFocus()
        enterAnim.start()
        generalPrefetchStart.start()
    }

    Connections {
        target: homeController
        function onCurrentPageChanged() {
            bottomBar.currentIndex = root.generalsOpen ? 1 : (root.cardsOpen ? 2 : 0)
            if (root.generalsOpen) {
                if (generalPage.item) {
                    root.applyGeneralsNavGraph()
                    generalPage.item.takeKeyboard()
                }
            } else if (root.cardsOpen) {
                if (cardPage.item) {
                    root.applyCardsNavGraph()
                    cardPage.item.takeKeyboard()
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
