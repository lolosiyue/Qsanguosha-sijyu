pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "."

Item {
    id: root
    objectName: "cardScene"

    property real uiScale: 1.0
    property bool compact: Config.responsiveUiEnabled && (width < 900 || height > width)
    property int compactPane: 0
    readonly property var navigationEntry: compact ? sortBox : backButton
    function showCompactPane(pane) {
        compactPane = pane
        Qt.callLater(function() {
            var target = pane === 0 ? cardGrid : pane === 1 ? details.firstVisibleAction : filters.searchField
            if (target) target.forceActiveFocus()
        })
    }
    function openCard(cardId) {
        selectCard(cardId)
        if (compact && selectedCardId >= 0) showCompactPane(1)
    }
    readonly property var cardModel: homeController.cardModel
    property int selectedCardId: -1
    property var selectedDetail: ({})
    property string sortKey: "engine"
    readonly property int modelCount: cardModel ? cardModel.filteredCount : 0
    readonly property int physicalCount: cardModel ? cardModel.physicalCount : 0
    readonly property int detailCardId: selectedDetail && selectedDetail.cardId !== undefined
                                         ? selectedDetail.cardId : -1
    readonly property bool readyForSmoke: cardModel && cardModel.loaded
                                          && cardModel.filteredCount > 0
                                          && detailCardId === cardModel.cardIdAt(0)
    property alias searchField: filters.searchField
    property alias backButton: backButton
    property alias sortControl: sortBox
    property alias themeButton: themeButton
    property alias reloadButton: reloadButton
    readonly property var lastControl: compact
        ? (compactPane === 2 ? filters.resetButton : compactPane === 1
           ? (details.lastVisibleAction || compactBar.detailButton) : (pagination.lastEnabledButton || cardGrid))
        : (pagination.lastEnabledButton || details.lastVisibleAction || cardGrid)
    signal navigationEndpointChanged()

    focus: true
    Keys.onEscapePressed: {
        if (compact && compactPane !== 0) showCompactPane(0)
        else homeController.openHome()
    }
    onLastControlChanged: navigationEndpointChanged()

    function selectCard(cardId) {
        if (!cardModel || cardId < 0)
            return
        var detail = cardModel.cardDetails(cardId)
        if (!detail || detail.cardId === undefined)
            return
        selectedCardId = cardId
        selectedDetail = detail
    }

    function selectFirst() {
        if (!cardModel || cardModel.count < 1) {
            selectedCardId = -1
            selectedDetail = ({})
            return
        }
        cardGrid.currentIndex = 0
        selectCard(cardModel.cardIdAt(0))
    }

    function takeKeyboard() {
        if (compact) showCompactPane(compactPane)
        else filters.searchField.forceActiveFocus()
    }

    function transferKeyboardFocus(target, event, reason) {
        // A compact pane must never transfer focus into a hidden sibling pane.
        if (compact && target === filters.resetButton) target = compactBar.filterButton
        if (compact && target === details.firstVisibleAction) target = compactBar.detailButton
        if (!target) {
            event.accepted = false
            return
        }
        target.forceActiveFocus(reason)
        event.accepted = true
    }

    function applyFilter(values) {
        // 結果沒變就保留目前選取，不跳回第一張。
        if (cardModel.applyFilter(values))
            Qt.callLater(selectFirst)
    }

    function applyInternalNavGraph() {
        if (compact) {
            sortBox.tabTarget = themeButton
            sortBox.backtabTarget = compactBar.listButton
            themeButton.KeyNavigation.tab = reloadButton
            reloadButton.KeyNavigation.tab = compactBar.listButton
            compactBar.listButton.KeyNavigation.tab = compactBar.detailButton
            compactBar.detailButton.KeyNavigation.tab = compactBar.filterButton
            compactBar.filterButton.KeyNavigation.tab = compactPane === 2 ? filters.searchField
                : compactPane === 1 ? (details.firstVisibleAction || compactBar.listButton) : cardGrid
            filters.searchField.KeyNavigation.backtab = compactBar.filterButton
            filters.resetButton.KeyNavigation.tab = compactBar.listButton
            var compactActions = details.visibleActions
            for (var j = 0; j < compactActions.length; ++j) {
                compactActions[j].KeyNavigation.backtab = j > 0 ? compactActions[j - 1] : compactBar.detailButton
                compactActions[j].KeyNavigation.tab = j + 1 < compactActions.length
                    ? compactActions[j + 1] : compactBar.listButton
            }
            navigationEndpointChanged()
            return
        }
        backButton.KeyNavigation.right = sortBox
        sortBox.leftTarget = backButton
        sortBox.rightTarget = themeButton
        themeButton.KeyNavigation.left = sortBox
        themeButton.KeyNavigation.right = reloadButton
        reloadButton.KeyNavigation.left = themeButton
        reloadButton.KeyNavigation.down = filters.searchField
        filters.searchField.KeyNavigation.up = reloadButton

        filters.resetButton.KeyNavigation.tab = cardGrid
        filters.resetButton.KeyNavigation.right = cardGrid
        filters.resetButton.KeyNavigation.down = cardGrid
        var pageFirst = pagination.firstEnabledButton
        var actions = details.visibleActions
        if (actions.length > 0) {
            actions[0].KeyNavigation.backtab = cardGrid
            for (var i = 0; i < actions.length; ++i) {
                actions[i].KeyNavigation.left = i > 0 ? actions[i - 1] : cardGrid
                actions[i].KeyNavigation.right = i + 1 < actions.length
                                                   ? actions[i + 1] : (pageFirst || cardGrid)
                actions[i].KeyNavigation.up = cardGrid
                actions[i].KeyNavigation.down = pageFirst || cardGrid
                if (i + 1 < actions.length) {
                    actions[i].KeyNavigation.tab = actions[i + 1]
                    actions[i + 1].KeyNavigation.backtab = actions[i]
                }
            }
            if (pageFirst) {
                actions[actions.length - 1].KeyNavigation.tab = pageFirst
                pageFirst.KeyNavigation.backtab = actions[actions.length - 1]
            }
        } else {
            if (pageFirst)
                pageFirst.KeyNavigation.backtab = cardGrid
        }
        pagination.previousButton.KeyNavigation.up = actions.length > 0
                                                       ? actions[actions.length - 1] : cardGrid
        pagination.nextButton.KeyNavigation.up = actions.length > 0
                                                   ? actions[actions.length - 1] : cardGrid
        pagination.previousButton.KeyNavigation.right = pagination.nextButton.enabled
                                                          ? pagination.nextButton : cardGrid
        pagination.nextButton.KeyNavigation.left = pagination.previousButton.enabled
                                                     ? pagination.previousButton : cardGrid
    }

    onSelectedDetailChanged: Qt.callLater(applyInternalNavGraph)
    onCompactPaneChanged: Qt.callLater(applyInternalNavGraph)
    onCompactChanged: Qt.callLater(applyInternalNavGraph)

    Connections {
        target: filters
        function onNavigationChanged() {
            Qt.callLater(filters.applyLocalNavGraph)
            Qt.callLater(root.applyInternalNavGraph)
        }
    }

    Component.onCompleted: {
        cardModel.ensureLoaded()
        Qt.callLater(selectFirst)
        Qt.callLater(applyInternalNavGraph)
    }

    Connections {
        target: root.cardModel
        function onPageChanged() {
            Qt.callLater(root.selectFirst)
            Qt.callLater(root.applyInternalNavGraph)
        }
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 0 : HomeTheme.cardPageHMargin
        anchors.rightMargin: root.compact ? 0 : HomeTheme.cardPageHMargin
        anchors.topMargin: root.compact ? 0 : HomeTheme.cardPageTopMargin
        anchors.bottomMargin: HomeTheme.cardPageBottomMargin
        spacing: HomeTheme.cardPanelGap

        BASlantedPanel {
            id: headerPanel
            width: parent.width
            height: root.compact ? HomeTheme.catalogCompactHeaderHeight : HomeTheme.cardHeaderHeight
            transformOrigin: Item.Top
            scale: root.uiScale
            slant: -0.05
            cornerRadius: HomeTheme.cardPanelRadius
            shadowBlur: 0
            shadowOffset: 0
            topColor: HomeTheme.cardPanelTop
            bottomColor: HomeTheme.cardPanelBottom
            borderColor: HomeTheme.cardPanelBorder
            shadowColor: HomeTheme.baDockShadow
            accentVisible: true
            accentColor: HomeTheme.cardAccent

            GridLayout {
                columns: root.compact ? 3 : 6
                anchors.fill: parent
                anchors.leftMargin: root.compact ? HomeTheme.compactMargin : HomeTheme.cardHeaderPadding
                anchors.rightMargin: root.compact ? HomeTheme.compactMargin : HomeTheme.cardHeaderPadding
                columnSpacing: root.compact ? HomeTheme.compactGap : HomeTheme.cardPanelGap
                rowSpacing: HomeTheme.compactGap

                BAToolButton {
                    id: backButton
                    visible: !root.compact
                    Layout.preferredWidth: HomeTheme.cardHeaderButtonWidth
                    Layout.preferredHeight: HomeTheme.cardActionButtonExtent
                    text: homeController.qtTranslate("CardScene", "Back")
                    onClicked: homeController.openHome()
                }

                ColumnLayout {
                    Layout.columnSpan: root.compact ? 3 : 1
                    Layout.fillWidth: true
                    spacing: HomeTheme.cardHeaderTitleGap
                    Text {
                        Layout.fillWidth: true
                        text: homeController.qtTranslate("CardScene", "Card Overview")
                        color: HomeTheme.cardTextPrimary
                        font.pixelSize: root.compact ? HomeTheme.cardSectionTitleFontSize : HomeTheme.cardTitleFontSize
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: root.compact ? homeController.qtTranslate("CardScene", "%1 card types · %2 physical cards").arg(root.modelCount).arg(root.physicalCount)
                             : homeController.qtTranslate("CardScene", "Browse card types and their physical variants")
                        color: HomeTheme.cardTextSecondary
                        font.pixelSize: HomeTheme.cardCaptionFontSize
                    }
                }

                Rectangle {
                    visible: !root.compact
                    Layout.preferredWidth: countText.implicitWidth + HomeTheme.cardCountBadgeHPadding * 2
                    Layout.preferredHeight: HomeTheme.cardCountBadgeHeight
                    radius: HomeTheme.cardCountBadgeHeight / 2
                    color: HomeTheme.cardInteractiveSoft
                    border.width: HomeTheme.cardBorderWidth
                    border.color: HomeTheme.cardInteractive
                    Text {
                        id: countText
                        anchors.centerIn: parent
                        text: homeController.qtTranslate("CardScene", "%1 card types · %2 physical cards")
                              .arg(root.modelCount).arg(root.physicalCount)
                        color: HomeTheme.cardTextPrimary
                        font.pixelSize: HomeTheme.cardBodyFontSize
                        font.bold: true
                    }
                }

                CardComboBox {
                    id: sortBox
                    Layout.preferredWidth: root.compact ? HomeTheme.catalogCardTileWidth : HomeTheme.cardSortWidth
                    Layout.fillWidth: root.compact
                    Layout.preferredHeight: HomeTheme.cardControlHeight
                    textRole: "label"
                    valueRole: "key"
                    accessibleLabel: homeController.qtTranslate("CardScene", "Sort cards")
                    model: [
                        { "key": "engine", "label": homeController.qtTranslate("CardScene", "Default order") },
                        { "key": "name", "label": homeController.qtTranslate("CardScene", "Name order") },
                        { "key": "number", "label": homeController.qtTranslate("CardScene", "Number order") }
                    ]
                    activeFocusOnTab: true
                    onActivated: {
                        root.sortKey = currentValue || "engine"
                        filters.applyNow()
                    }
                }

                BAToolButton {
                    id: themeButton
                    Layout.preferredWidth: HomeTheme.cardActionButtonExtent
                    Layout.preferredHeight: HomeTheme.cardActionButtonExtent
                    Accessible.name: qsTranslate("HomeScene", "Toggle theme")
                    iconSource: homeController.isDarkTheme
                                ? "qrc:/QSanguosha/Home/icons/moon.svg"
                                : "qrc:/QSanguosha/Home/icons/sun.svg"
                    onClicked: homeController.toggleTheme()
                }

                BAToolButton {
                    id: reloadButton
                    Layout.preferredWidth: root.compact ? HomeTheme.cardActionButtonExtent : HomeTheme.cardHeaderButtonWidth
                    Layout.preferredHeight: HomeTheme.cardActionButtonExtent
                    Accessible.name: homeController.qtTranslate("CardScene", "Reload")
                    // Four CJK glyphs do not fit a touch-sized button; compact uses the reload glyph.
                    text: root.compact ? "" : Accessible.name
                    iconSource: root.compact ? "qrc:/QSanguosha/Home/icons/update.svg" : ""
                    onClicked: {
                        root.cardModel.reload()
                        Qt.callLater(root.selectFirst)
                    }
                }
            }
        }

        CatalogPaneBar {
            id: compactBar
            width: parent.width
            height: visible ? HomeTheme.compactTouch : 0
            visible: root.compact
            currentIndex: root.compactPane
            itemCount: root.modelCount
            detailsEnabled: root.selectedCardId >= 0
            onActivated: function(index) { root.showCompactPane(index) }
        }

        Row {
            width: parent.width
            height: Math.max(0, parent.height - headerPanel.height - HomeTheme.cardPanelGap
                             - (root.compact ? compactBar.height + HomeTheme.cardPanelGap : 0))
            spacing: HomeTheme.cardPanelGap

            CardFilterPanel {
                id: filters
                visible: !root.compact || root.compactPane === 2
                width: root.compact ? parent.width : HomeTheme.cardFilterWidth
                height: parent.height
                cardModel: root.cardModel
                sortKey: root.sortKey
                transformOrigin: Item.TopLeft
                scale: root.uiScale
                onFiltersChanged: function(values) { root.applyFilter(values) }
            }

            Item {
                id: gridPanel
                visible: !root.compact || root.compactPane === 0
                width: root.compact ? parent.width : parent.width - HomeTheme.cardFilterWidth - HomeTheme.cardDetailWidth
                       - HomeTheme.cardPanelGap * 2
                height: parent.height
                transformOrigin: Item.Top
                scale: root.uiScale

                BASlantedPanel {
                    anchors.fill: parent
                    slant: 0
                    cornerRadius: HomeTheme.cardPanelRadius
                    shadowBlur: 0
                    shadowOffset: 0
                    topColor: HomeTheme.cardPanelTop
                    bottomColor: HomeTheme.cardPanelBottom
                    borderColor: HomeTheme.cardPanelBorder
                    shadowColor: HomeTheme.baDockShadow
                }

                GridView {
                    id: cardGrid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: pagination.top
                    anchors.margins: HomeTheme.cardGridGap
                    anchors.bottomMargin: HomeTheme.cardGridBottomInset
                    clip: true
                    model: root.cardModel
                    cellWidth: Math.floor(width / (root.compact ? Math.max(2, Math.floor(width / HomeTheme.catalogCardTileWidth)) : 4))
                    cellHeight: root.compact ? cellWidth * 1.4 + HomeTheme.catalogCardMetaHeight : Math.floor(height / 3)
                    keyNavigationWraps: false
                    activeFocusOnTab: true
                    reuseItems: false
                    highlightFollowsCurrentItem: false
                    boundsBehavior: Flickable.StopAtBounds
                    Accessible.role: Accessible.List
                    Accessible.name: homeController.qtTranslate("CardScene", "Card grid")
                    // GridView consumes navigation keys itself, so only boundary exits
                    // are accepted here; interior arrows continue to move the selection.
                    Keys.priority: Keys.BeforeItem
                    Keys.onTabPressed: function(event) {
                        root.transferKeyboardFocus(root.compact ? compactBar.detailButton : details.firstVisibleAction
                                                   || pagination.firstEnabledButton,
                                                   event, Qt.TabFocusReason)
                    }
                    Keys.onBacktabPressed: function(event) {
                        root.transferKeyboardFocus(filters.resetButton, event,
                                                   Qt.BacktabFocusReason)
                    }
                    Keys.onLeftPressed: function(event) {
                        var columns = Math.max(1, Math.round(width / cellWidth))
                        if (currentIndex >= 0 && currentIndex % columns === 0)
                            root.transferKeyboardFocus(filters.resetButton, event,
                                                       Qt.BacktabFocusReason)
                        else
                            event.accepted = false
                    }
                    Keys.onRightPressed: function(event) {
                        var columns = Math.max(1, Math.round(width / cellWidth))
                        if (currentIndex >= 0
                                && (currentIndex % columns === columns - 1
                                    || currentIndex === count - 1)) {
                            root.transferKeyboardFocus(details.firstVisibleAction
                                                       || pagination.firstEnabledButton,
                                                       event, Qt.TabFocusReason)
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onUpPressed: function(event) {
                        var columns = Math.max(1, Math.round(width / cellWidth))
                        if (currentIndex >= 0 && currentIndex < columns)
                            root.transferKeyboardFocus(filters.resetButton, event,
                                                       Qt.BacktabFocusReason)
                        else
                            event.accepted = false
                    }
                    Keys.onDownPressed: function(event) {
                        var columns = Math.max(1, Math.round(width / cellWidth))
                        if (currentIndex >= 0 && currentIndex + columns >= count) {
                            root.transferKeyboardFocus(pagination.firstEnabledButton
                                                       || details.firstVisibleAction,
                                                       event, Qt.TabFocusReason)
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onReturnPressed: root.openCard(root.cardModel.cardIdAt(currentIndex))
                    Keys.onEnterPressed: root.openCard(root.cardModel.cardIdAt(currentIndex))
                    Keys.onSpacePressed: function(event) {
                        root.openCard(root.cardModel.cardIdAt(currentIndex))
                        event.accepted = true
                    }

                    onCurrentIndexChanged: {
                        if (currentIndex >= 0)
                            root.selectCard(root.cardModel.cardIdAt(currentIndex))
                    }

                    delegate: Item {
                        id: cardDelegate
                        required property int index
                        required property int cardId
                        required property string overviewDisplayName
                        required property string baseDisplayName
                        required property string typeKey
                        required property string typeDisplay
                        required property string packageSummary
                        required property int physicalCount
                        required property int variantCount
                        required property var tagLabels
                        required property url imageUrl
                        width: cardGrid.cellWidth
                        height: cardGrid.cellHeight

                        CardBrowserTile {
                            compact: root.compact
                            anchors.fill: parent
                            anchors.margins: Math.floor(HomeTheme.cardGridGap / 2)
                            cardId: cardDelegate.cardId
                            overviewDisplayName: cardDelegate.overviewDisplayName
                            baseDisplayName: cardDelegate.baseDisplayName
                            typeKey: cardDelegate.typeKey
                            typeDisplay: cardDelegate.typeDisplay
                            packageSummary: cardDelegate.packageSummary
                            physicalCount: cardDelegate.physicalCount
                            variantCount: cardDelegate.variantCount
                            tagLabels: cardDelegate.tagLabels
                            imageUrl: cardDelegate.imageUrl
                            selected: root.selectedCardId === cardDelegate.cardId
                            keyboardFocus: cardGrid.activeFocus
                                           && cardGrid.currentIndex === cardDelegate.index
                            onActivated: function(cardId) {
                                cardGrid.currentIndex = cardDelegate.index
                                root.openCard(cardId)
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: root.cardModel && root.cardModel.filteredCount === 0
                        width: parent.width * 0.72
                        horizontalAlignment: Text.AlignHCenter
                        text: homeController.qtTranslate("CardScene", "No cards match the current filters")
                        color: HomeTheme.cardTextMuted
                        font.pixelSize: HomeTheme.cardEmptyFontSize
                        wrapMode: Text.WordWrap
                    }
                }

                // Compact rows scroll; fade the cut-off row so it reads as more content.
                Rectangle {
                    visible: root.compact && !cardGrid.atYEnd
                    anchors.left: cardGrid.left
                    anchors.right: cardGrid.right
                    anchors.bottom: cardGrid.bottom
                    height: HomeTheme.cardGridGap * 4
                    gradient: Gradient {
                        GradientStop {
                            position: 0
                            color: Qt.rgba(HomeTheme.cardPanelBottom.r, HomeTheme.cardPanelBottom.g,
                                           HomeTheme.cardPanelBottom.b, 0)
                        }
                        GradientStop { position: 1; color: HomeTheme.cardPanelBottom }
                    }
                }

                CardPagination {
                    id: pagination
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: HomeTheme.cardGridGap
                    anchors.rightMargin: HomeTheme.cardGridGap
                    anchors.bottomMargin: HomeTheme.cardPaginationBottomInset
                    height: HomeTheme.cardPaginationHeight
                    pageIndex: root.cardModel ? root.cardModel.pageIndex : 0
                    pageCount: root.cardModel ? root.cardModel.pageCount : 1
                    onPageRequested: function(pageIndex) {
                        root.cardModel.setPageIndex(pageIndex)
                    }
                }
            }

            CardDetailPanel {
                id: details
                visible: !root.compact || root.compactPane === 1
                compact: root.compact
                width: root.compact ? parent.width : HomeTheme.cardDetailWidth
                height: parent.height
                cardModel: root.cardModel
                detail: root.selectedDetail
                transformOrigin: Item.TopRight
                scale: root.uiScale
            }
        }
    }
}
