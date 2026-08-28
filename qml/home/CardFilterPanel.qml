import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

pragma ComponentBehavior: Bound

Item {
    id: root

    property var cardModel
    property string sortKey: "engine"
    property var selectedTagKeys: []
    property alias searchField: searchInput
    property alias typeControl: typeBox
    property alias kindControl: kindBox
    property alias suitControl: suitBox
    property alias packageControl: packageBox
    property alias resetButton: resetButton
    readonly property var firstTag: tagRepeater.count > 0 ? tagRepeater.itemAt(0) : null
    readonly property var lastTag: tagRepeater.count > 0 ? tagRepeater.itemAt(tagRepeater.count - 1) : null
    signal filtersChanged(var filters)
    signal navigationChanged()

    function applyNow() {
        filtersChanged({
            "query": searchInput.text,
            "type": typeBox.currentValue || "all",
            "kind": kindBox.currentValue || "all",
            "suit": suitBox.currentValue || "all",
            "package": packageBox.currentValue || "all",
            "tags": selectedTagKeys,
            "sort": root.sortKey
        })
    }

    function reset() {
        searchInput.clear()
        typeBox.currentIndex = 0
        kindBox.currentIndex = 0
        suitBox.currentIndex = 0
        packageBox.currentIndex = 0
        selectedTagKeys = []
        applyNow()
    }

    function toggleTag(tagKey, checked) {
        var next = selectedTagKeys.slice()
        var index = next.indexOf(tagKey)
        if (checked && index < 0)
            next.push(tagKey)
        else if (!checked && index >= 0)
            next.splice(index, 1)
        selectedTagKeys = next
        applyNow()
    }

    function revealItem(item) {
        if (!item)
            return
        var point = item.mapToItem(filterFlick.contentItem, 0, 0)
        var top = point.y - HomeTheme.cardSectionGap
        var bottom = point.y + item.height + HomeTheme.cardSectionGap
        if (top < filterFlick.contentY)
            filterFlick.contentY = Math.max(0, top)
        else if (bottom > filterFlick.contentY + filterFlick.height)
            filterFlick.contentY = Math.min(
                        Math.max(0, filterFlick.contentHeight - filterFlick.height),
                        bottom - filterFlick.height)
    }

    function applyLocalNavGraph() {
        searchInput.KeyNavigation.tab = typeBox
        typeBox.backtabTarget = searchInput
        typeBox.tabTarget = kindBox
        kindBox.backtabTarget = typeBox
        kindBox.tabTarget = suitBox
        suitBox.backtabTarget = kindBox
        suitBox.tabTarget = packageBox
        packageBox.backtabTarget = suitBox
        packageBox.tabTarget = firstTag || resetButton
        resetButton.KeyNavigation.backtab = lastTag || packageBox

        searchInput.KeyNavigation.down = typeBox
        typeBox.KeyNavigation.up = searchInput
        typeBox.leftTarget = searchInput
        typeBox.rightTarget = kindBox
        kindBox.KeyNavigation.up = typeBox
        kindBox.leftTarget = typeBox
        kindBox.rightTarget = suitBox
        suitBox.KeyNavigation.up = kindBox
        suitBox.leftTarget = kindBox
        suitBox.rightTarget = packageBox
        packageBox.KeyNavigation.up = suitBox
        packageBox.leftTarget = suitBox
        packageBox.rightTarget = firstTag || resetButton
        resetButton.KeyNavigation.up = lastTag || packageBox
        resetButton.KeyNavigation.left = lastTag || packageBox
    }

    onFirstTagChanged: Qt.callLater(applyLocalNavGraph)
    onLastTagChanged: Qt.callLater(applyLocalNavGraph)
    Component.onCompleted: Qt.callLater(applyLocalNavGraph)

    Timer {
        id: searchDebounce
        interval: 130
        repeat: false
        onTriggered: root.applyNow()
    }

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

    Flickable {
        id: filterFlick
        anchors.fill: parent
        anchors.margins: HomeTheme.cardPanelContentPadding
        contentHeight: form.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: form
            width: parent.width
            spacing: HomeTheme.cardSectionGap

            Text {
                text: homeController.qtTranslate("CardScene", "Filter cards")
                color: HomeTheme.cardTextPrimary
                font.pixelSize: HomeTheme.cardSectionTitleFontSize
                font.bold: true
            }

            TextField {
                id: searchInput
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                activeFocusOnTab: true
                placeholderText: homeController.qtTranslate("CardScene", "Name, effect, or package")
                color: HomeTheme.cardTextPrimary
                placeholderTextColor: HomeTheme.cardTextMuted
                selectByMouse: true
                Accessible.name: homeController.qtTranslate("CardScene", "Search cards")
                onTextChanged: searchDebounce.restart()
                onActiveFocusChanged: if (activeFocus) root.revealItem(searchInput)
                background: Rectangle {
                    radius: HomeTheme.cardControlRadius
                    color: HomeTheme.cardInputFill
                    border.width: searchInput.activeFocus
                                  ? (homeController.visualMode === "highcontrast"
                                     ? HomeTheme.cardHighContrastFocusBorderWidth
                                     : HomeTheme.cardSelectedBorderWidth)
                                  : HomeTheme.cardBorderWidth
                    border.color: searchInput.activeFocus ? HomeTheme.focusBorderHigh
                                                               : HomeTheme.cardPanelBorder
                }
            }

            Text { text: homeController.qtTranslate("CardScene", "Type"); color: HomeTheme.cardTextSecondary; font.pixelSize: HomeTheme.cardCaptionFontSize }
            CardComboBox {
                id: typeBox
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                model: root.cardModel ? root.cardModel.typeOptions : []
                textRole: "label"
                valueRole: "key"
                accessibleLabel: homeController.qtTranslate("CardScene", "Type")
                activeFocusOnTab: true
                onActivated: root.applyNow()
                onActiveFocusChanged: if (activeFocus) root.revealItem(typeBox)
            }

            Text { text: homeController.qtTranslate("CardScene", "Kind"); color: HomeTheme.cardTextSecondary; font.pixelSize: HomeTheme.cardCaptionFontSize }
            CardComboBox {
                id: kindBox
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                model: root.cardModel ? root.cardModel.kindOptions : []
                textRole: "label"
                valueRole: "key"
                accessibleLabel: homeController.qtTranslate("CardScene", "Kind")
                activeFocusOnTab: true
                onActivated: root.applyNow()
                onActiveFocusChanged: if (activeFocus) root.revealItem(kindBox)
            }

            Text { text: homeController.qtTranslate("CardScene", "Suit"); color: HomeTheme.cardTextSecondary; font.pixelSize: HomeTheme.cardCaptionFontSize }
            CardComboBox {
                id: suitBox
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                model: root.cardModel ? root.cardModel.suitOptions : []
                textRole: "label"
                valueRole: "key"
                accessibleLabel: homeController.qtTranslate("CardScene", "Suit")
                activeFocusOnTab: true
                onActivated: root.applyNow()
                onActiveFocusChanged: if (activeFocus) root.revealItem(suitBox)
            }

            Text { text: homeController.qtTranslate("CardScene", "Package"); color: HomeTheme.cardTextSecondary; font.pixelSize: HomeTheme.cardCaptionFontSize }
            CardComboBox {
                id: packageBox
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                model: root.cardModel ? root.cardModel.packageOptions : []
                textRole: "label"
                valueRole: "key"
                accessibleLabel: homeController.qtTranslate("CardScene", "Package")
                activeFocusOnTab: true
                onActivated: root.applyNow()
                onActiveFocusChanged: if (activeFocus) root.revealItem(packageBox)
            }

            Text {
                text: homeController.qtTranslate("CardScene", "Tags")
                color: HomeTheme.cardTextSecondary
                font.pixelSize: HomeTheme.cardCaptionFontSize
            }

            Flow {
                id: tagFlow
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: HomeTheme.cardTagGap

                Repeater {
                    id: tagRepeater
                    model: root.cardModel ? root.cardModel.tagOptions : []
                    onItemAdded: Qt.callLater(root.navigationChanged)
                    onItemRemoved: Qt.callLater(root.navigationChanged)

                    CardTagChip {
                        required property int index
                        required property var modelData
                        tagKey: modelData.key
                        text: modelData.label
                        count: modelData.count
                        checked: root.selectedTagKeys.indexOf(tagKey) >= 0
                        KeyNavigation.left: index > 0 ? tagRepeater.itemAt(index - 1) : packageBox
                        KeyNavigation.right: index + 1 < tagRepeater.count
                                             ? tagRepeater.itemAt(index + 1) : resetButton
                        KeyNavigation.up: packageBox
                        KeyNavigation.down: resetButton
                        KeyNavigation.tab: index + 1 < tagRepeater.count
                                           ? tagRepeater.itemAt(index + 1) : resetButton
                        KeyNavigation.backtab: index > 0
                                               ? tagRepeater.itemAt(index - 1) : packageBox
                        onToggled: function(key, checkedValue) {
                            root.toggleTag(key, checkedValue)
                        }
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardDividerHeight
                Layout.topMargin: HomeTheme.cardFilterDividerTopMargin
                color: HomeTheme.cardPanelBorder
            }

            Text {
                Layout.fillWidth: true
                text: homeController.qtTranslate("CardScene", "%1 card types found")
                      .arg(root.cardModel ? root.cardModel.filteredCount : 0)
                color: HomeTheme.cardTextSecondary
                font.pixelSize: HomeTheme.cardBodyFontSize
            }

            BAToolButton {
                id: resetButton
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardFilterResetHeight
                text: homeController.qtTranslate("CardScene", "Reset filters")
                onClicked: root.reset()
                onActiveFocusChanged: if (activeFocus) root.revealItem(resetButton)
            }
        }
    }
}
