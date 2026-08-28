pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "."

Item {
    id: root
    objectName: "cardScene"

    property real uiScale: 1.0
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
    readonly property var lastControl: pagination.lastEnabledButton
                                       || details.lastVisibleAction || cardGrid
    signal navigationEndpointChanged()

    focus: true
    Keys.onEscapePressed: homeController.openHome()
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
        filters.searchField.forceActiveFocus()
    }

    function transferKeyboardFocus(target, event, reason) {
        if (!target) {
            event.accepted = false
            return
        }
        target.forceActiveFocus(reason)
        event.accepted = true
    }

    function applyFilter(values) {
        cardModel.applyFilter(values)
        Qt.callLater(selectFirst)
    }

    function applyInternalNavGraph() {
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
        anchors.leftMargin: HomeTheme.cardPageHMargin
        anchors.rightMargin: HomeTheme.cardPageHMargin
        anchors.topMargin: HomeTheme.cardPageTopMargin
        anchors.bottomMargin: HomeTheme.cardPageBottomMargin
        spacing: HomeTheme.cardPanelGap

        BASlantedPanel {
            id: headerPanel
            width: parent.width
            height: HomeTheme.cardHeaderHeight
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

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: HomeTheme.cardHeaderPadding
                anchors.rightMargin: HomeTheme.cardHeaderPadding
                spacing: HomeTheme.cardPanelGap

                BAToolButton {
                    id: backButton
                    Layout.preferredWidth: HomeTheme.cardHeaderButtonWidth
                    Layout.preferredHeight: HomeTheme.cardActionButtonExtent
                    text: homeController.qtTranslate("CardScene", "Back")
                    onClicked: homeController.openHome()
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: HomeTheme.cardHeaderTitleGap
                    Text {
                        text: homeController.qtTranslate("CardScene", "Card Overview")
                        color: HomeTheme.cardTextPrimary
                        font.pixelSize: HomeTheme.cardTitleFontSize
                        font.bold: true
                    }
                    Text {
                        text: homeController.qtTranslate("CardScene", "Browse card types and their physical variants")
                        color: HomeTheme.cardTextSecondary
                        font.pixelSize: HomeTheme.cardCaptionFontSize
                    }
                }

                Rectangle {
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
                    Layout.preferredWidth: HomeTheme.cardSortWidth
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
                    Layout.preferredWidth: HomeTheme.cardHeaderButtonWidth
                    Layout.preferredHeight: HomeTheme.cardActionButtonExtent
                    text: homeController.qtTranslate("CardScene", "Reload")
                    onClicked: {
                        root.cardModel.reload()
                        Qt.callLater(root.selectFirst)
                    }
                }
            }
        }

        Row {
            width: parent.width
            height: parent.height - HomeTheme.cardHeaderHeight - HomeTheme.cardPanelGap
            spacing: HomeTheme.cardPanelGap

            CardFilterPanel {
                id: filters
                width: HomeTheme.cardFilterWidth
                height: parent.height
                cardModel: root.cardModel
                sortKey: root.sortKey
                transformOrigin: Item.TopLeft
                scale: root.uiScale
                onFiltersChanged: function(values) { root.applyFilter(values) }
            }

            Item {
                id: gridPanel
                width: parent.width - HomeTheme.cardFilterWidth - HomeTheme.cardDetailWidth
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
                    cellWidth: Math.floor(width / 4)
                    cellHeight: Math.floor(height / 3)
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
                        root.transferKeyboardFocus(details.firstVisibleAction
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
                    Keys.onReturnPressed: root.selectCard(root.cardModel.cardIdAt(currentIndex))
                    Keys.onEnterPressed: root.selectCard(root.cardModel.cardIdAt(currentIndex))
                    Keys.onSpacePressed: function(event) {
                        root.selectCard(root.cardModel.cardIdAt(currentIndex))
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
                                root.selectCard(cardId)
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
                width: HomeTheme.cardDetailWidth
                height: parent.height
                cardModel: root.cardModel
                detail: root.selectedDetail
                transformOrigin: Item.TopRight
                scale: root.uiScale
            }
        }
    }
}
