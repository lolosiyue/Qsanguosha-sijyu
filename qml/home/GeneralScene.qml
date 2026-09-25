import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "."

Item {
    id: root
    focus: true

    component ThemeCheckBox: Basic.CheckBox {
        id: box
        implicitHeight: root.compact ? HomeTheme.compactTouch : Math.max(implicitContentHeight, implicitIndicatorHeight) + topPadding + bottomPadding
        padding: 4
        contentItem: Text {
            text: box.text
            color: HomeTheme.btnSecondaryText
            leftPadding: box.indicator.width + 8
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 15
            font.bold: box.font.bold
        }
        indicator: Rectangle {
            implicitWidth: 20
            implicitHeight: 20
            x: box.leftPadding
            y: parent ? (parent.height - height) / 2 : 0
            radius: 4
            color: HomeTheme.btnSecondary
            border.color: HomeTheme.baDockBorder
            Rectangle {
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 2
                visible: box.checked
                color: HomeTheme.baSky
            }
        }
        Keys.onShortcutOverride: function(event) {
            if (event.key === Qt.Key_Space)
                event.accepted = true
        }
    }

    component MetaBadge: Rectangle {
        property string label: ""
        property color accent: HomeTheme.baDockBorder
        implicitWidth: badgeText.implicitWidth + 16
        width: Math.min(implicitWidth, parent ? parent.width : implicitWidth)
        implicitHeight: 24
        radius: 4
        color: HomeTheme.btnSecondary
        border.width: 1
        border.color: accent
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: parent.label
            width: Math.max(0, parent.width - 16)
            elide: Text.ElideRight
            color: HomeTheme.btnSecondaryText
            font.pixelSize: 12
        }
    }

    component CopyableText: TextEdit {
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        color: HomeTheme.btnSecondaryText
        selectedTextColor: "#FFFFFF"
        selectionColor: HomeTheme.baSky
        font.pixelSize: 15
        activeFocusOnPress: true
        onActiveFocusChanged: if (activeFocus) root.revealDetailControl(this)
    }

    component ParallelogramPlate: Item {
        property string label: ""
        property bool related: false
        implicitWidth: plateText.implicitWidth + 28
        implicitHeight: 30

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: related ? HomeTheme.skillPlateRelated : HomeTheme.skillPlate
            border.width: related ? 1 : 0
            border.color: HomeTheme.skillNameRelated
            transform: Shear {
                origin.x: width / 2
                origin.y: height / 2
                xFactor: -0.22
            }
        }

        CopyableText {
            id: plateText
            anchors.centerIn: parent
            text: "【" + parent.label + "】"
            color: parent.related ? HomeTheme.skillNameRelated : HomeTheme.skillName
            font.pixelSize: 18
            font.bold: true
            wrapMode: TextEdit.NoWrap
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
        }
    }

    component HiddenMark: Item {
        implicitWidth: 28
        implicitHeight: 18

        Rectangle {
            anchors.fill: parent
            radius: 2
            color: HomeTheme.hiddenBadge
            transform: Shear {
                origin.x: width / 2
                origin.y: height / 2
                xFactor: -0.28
            }
        }

        Text {
            anchors.centerIn: parent
            text: qsTranslate("GeneralOverview", "Hidden mark")
            color: HomeTheme.hiddenBadgeText
            font.pixelSize: 11
            font.bold: true
        }
    }

    component ThemeField: Basic.TextField {
        implicitHeight: root.compact ? HomeTheme.compactTouch : 32
        color: HomeTheme.btnSecondaryText
        placeholderTextColor: HomeTheme.pillText
        font.pixelSize: 16
        verticalAlignment: Text.AlignVCenter
        topPadding: 0
        bottomPadding: 0
        background: Rectangle {
            radius: 8
            color: HomeTheme.btnSecondary
            border.color: parent.activeFocus ? HomeTheme.focusBorderHigh : HomeTheme.btnSecondaryBorder
        }
    }

    // 武將包導覽欄：直式放在列表左側，窄版改成橫條。
    component PackageNavList: ListView {
        id: navList
        property var entries: []
        property string currentKey: ""
        property bool horizontal: false
        signal picked(string key)
        orientation: horizontal ? ListView.Horizontal : ListView.Vertical
        clip: true
        spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        activeFocusOnTab: true
        model: visible ? entries : []
        ScrollBar.vertical: HomeScrollBar { }

        Keys.onPressed: function(event) {
            var back = horizontal ? Qt.Key_Left : Qt.Key_Up
            var fwd = horizontal ? Qt.Key_Right : Qt.Key_Down
            var delta = event.key === back ? -1 : (event.key === fwd ? 1 : 0)
            if (delta === 0 || entries.length === 0)
                return
            var idx = 0
            for (var i = 0; i < entries.length; ++i) {
                if (String(entries[i].key) === currentKey)
                    idx = i
            }
            var next = Math.max(0, Math.min(entries.length - 1, idx + delta))
            if (next !== idx) {
                picked(String(entries[next].key))
                positionViewAtIndex(next, ListView.Contain)
            }
            event.accepted = true
        }

        delegate: Rectangle {
            id: navItem
            required property var modelData
            readonly property bool current: String(modelData.key) === navList.currentKey
            width: navList.horizontal ? navLabel.implicitWidth + 28 : navList.width
            height: navList.horizontal ? navList.height : 44
            radius: 8
            color: current ? HomeTheme.navBgActive
                           : (navMouse.containsMouse ? HomeTheme.navBgHover : "transparent")
            border.width: current ? (navList.activeFocus ? 2 : 1) : 0
            border.color: HomeTheme.focusBorderHigh

            Text {
                id: navLabel
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                text: String(navItem.modelData.label || "")
                elide: navList.horizontal ? Text.ElideNone : Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: HomeTheme.btnSecondaryText
                font.pixelSize: 15
                font.bold: navItem.current
            }

            MouseArea {
                id: navMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: navList.picked(String(navItem.modelData.key))
            }
        }
    }

    component ListMetaOverlay: Column {
        id: overlay
        property string kingdoms: ""
        property int maxHp: 0
        property int startHp: 0
        property bool lord: false
        spacing: 2

        Row {
            spacing: 2
            Repeater {
                model: overlay.kingdoms.length > 0 ? overlay.kingdoms.split("+") : []
                delegate: Image {
                    required property var modelData
                    width: 18
                    height: 18
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    source: homeController.kingdomIcon(String(modelData))
                }
            }
        }

        // 一排放不下全部勾玉時，改成單一圖示加數字，避免體力多的武將蓋滿立繪。
        readonly property bool hpCompact: overlay.maxHp > Math.floor((overlay.width + 1) / 12)

        Flow {
            visible: !overlay.hpCompact
            width: overlay.width
            spacing: 1
            Repeater {
                model: overlay.hpCompact ? 0 : overlay.maxHp
                delegate: Image {
                    required property int modelData
                    width: 11
                    height: 11
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    source: homeController.magatamaImage(
                                modelData < overlay.startHp ? 5 : 0)
                }
            }
        }

        Row {
            visible: overlay.hpCompact
            spacing: 2
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                source: overlay.hpCompact ? homeController.magatamaImage(5) : ""
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "×" + root.hpText(overlay.startHp, overlay.maxHp)
                color: HomeTheme.onArtText
                style: Text.Outline
                styleColor: HomeTheme.onArtScrim
                font.pixelSize: 13
                font.bold: true
            }
        }
    }

    property var kingdomOptions: []
    property var packageGroups: []
    property var details: ({})
    property string selectedName: ""
    property string searchText: ""
    property string kingdomFilter: "all"
    property string nicknameFilter: ""
    property string sameNameFilter: ""
    property bool includeHidden: true
    property int hpMin: 0
    property int hpMax: 0
    property var genderFilter: []
    property string navGroup: ""
    property string navPackage: ""
    readonly property var navPackageEntries: {
        for (var i = 0; i < packageGroups.length; ++i) {
            if (packageGroups[i].key === navGroup)
                return packageGroups[i].entries
        }
        return []
    }
    // 搜尋與同名篩選跨全部武將包；其餘時候只列導覽選中的那一包，網格才不會一次塞進上千張。
    readonly property bool navScoped: navPackage.length > 0
                                      && searchText.length === 0 && sameNameFilter.length === 0
    property int detailTab: 0
    property real uiScale: 1.0
    property bool compact: Config.responsiveUiEnabled && (width < 900 || height > width)
    property int compactPane: 0
    readonly property int visibleColumns: compact
        ? Math.max(2, Math.floor(generalGrid.width / HomeTheme.catalogGeneralTileWidth)) : gridColumns
    readonly property var navigationEntry: searchField
    readonly property var navigationExit: compact && compactPane === 0 ? generalGrid : banBtn
    signal navigationEndpointChanged()
    onNavigationExitChanged: navigationEndpointChanged()
    function showCompactPane(pane) {
        compactPane = pane
        Qt.callLater(function() {
            if (pane === 0) generalGrid.forceActiveFocus()
            else detailFlick.forceActiveFocus()
        })
    }
    function revealDetailControl(item) {
        if (!compact || compactPane !== 1 || !item) return
        var ancestor = item.parent
        while (ancestor && ancestor !== detailLayout) ancestor = ancestor.parent
        if (!ancestor) return
        var point = item.mapToItem(detailLayout, 0, 0)
        if (point.y < detailFlick.contentY) detailFlick.contentY = Math.max(0, point.y)
        else if (point.y + item.height > detailFlick.contentY + detailFlick.height)
            detailFlick.contentY = Math.min(Math.max(0, detailFlick.contentHeight - detailFlick.height),
                                          point.y + item.height - detailFlick.height)
    }
    property int gridColumns: 5
    property bool gridColsReady: false
    readonly property bool tableMode: !compact && gridColumns >= HomeTheme.generalGridMaxColumns
    property bool keyboardReady: false
    property alias searchField: searchField
    property alias banBtn: banBtn

    onTableModeChanged: {
        if (!keyboardReady)
            return
        if (tableMode) {
            generalTable.forceActiveFocus()
            Qt.callLater(function() {
                if (!root.tableMode || !catalog || catalog.count <= 0)
                    return
                var idx = catalog.indexOfName(root.selectedName)
                if (idx < 0)
                    idx = 0
                generalTable.positionViewAtIndex(idx, ListView.Contain)
            })
        } else {
            generalGrid.forceActiveFocus()
        }
    }

    readonly property int maxGridColumns: HomeTheme.generalGridMaxColumns

    readonly property var catalog: homeController.generalModel
    readonly property bool catalogPending: !catalog || !catalog.loaded
    // 切換選中時先保留上一位武將的詳情，新資料到了才替換，避免整塊面板閃回骨架。
    readonly property string shownName: String(details.name || "")
    readonly property bool detailsReady: shownName.length > 0
    readonly property bool detailsPending: selectedName.length > 0 && !detailsReady
    readonly property var kingdomKeys: {
        var raw = String((details && (details.kingdoms || details.kingdom)) || "")
        return raw.length > 0 ? raw.split("+") : []
    }

    function ui(context, source) {
        return homeController.qtTranslate(context, source)
    }

    function rebuild() {
        homeController.applyGeneralFilter({
            kingdom: kingdomFilter,
            search: searchText,
            nickname: nicknameFilter,
            sameName: sameNameFilter,
            includeHidden: includeHidden,
            hpMin: hpMin,
            hpMax: hpMax,
            genders: genderFilter,
            packages: navScoped ? [navPackage] : []
        })
        syncSelection()
    }

    function pickNavGroup(key) {
        navGroup = key
        var entries = navPackageEntries
        pickNavPackage(entries.length > 0 ? String(entries[0].key) : "")
    }

    function pickNavPackage(key) {
        navPackage = key
        // 點武將包代表回到分包瀏覽，清掉會跨包的搜尋與同名篩選。
        searchField.clear()
        searchDebounce.stop()
        searchText = ""
        sameNameFilter = ""
        rebuild()
    }

    function syncSelection() {
        if (!catalog || catalog.count === 0) {
            selectedName = ""
            details = ({})
            return
        }
        if (selectedName === "" || !catalog.containsName(selectedName))
            selectGeneral(catalog.nameAt(0))
    }

    function selectGeneral(name) {
        selectedName = name
        detailsTimer.restart()
    }

    function reloadDetails() {
        if (root.selectedName.length > 0)
            root.details = homeController.generalDetails(root.selectedName)
    }

    function applyGridColumns(n) {
        var v = Math.max(HomeTheme.generalGridMinColumns,
                         Math.min(Math.round(n), HomeTheme.generalGridMaxColumns))
        root.gridColumns = v
        homeController.setGeneralGridColumns(v)
    }

    function hpText(startHp, maxHp) {
        if (Number(startHp) !== Number(maxHp))
            return String(startHp) + "/" + String(maxHp)
        return String(maxHp)
    }

    function generalNameWithCompanions(name, companions, companionLabel) {
        var companionText = String(companions || "")
        var label = String(companionLabel || "")
        return companionText.length > 0
                ? String(name || "") + "  " + label + ": " + companionText
                : String(name || "")
    }

    function takeKeyboard() {
        if (skinPanel.visible)
            skinPanel.forceActiveFocus()
        else
            searchField.forceActiveFocus()
    }

    // 依 generalPackages() 的 group 欄位把武將包切成與伺服器設定相同的分類。
    function groupPackages(entries) {
        var groups = []
        var byKey = {}
        for (var i = 0; i < entries.length; ++i) {
            var entry = entries[i]
            var key = String(entry.group || "")
            if (byKey[key] === undefined) {
                byKey[key] = groups.length
                groups.push({ key: key, label: String(entry.groupLabel || key), entries: [], keys: [] })
            }
            var group = groups[byKey[key]]
            group.entries.push(entry)
            group.keys.push(String(entry.key))
        }
        return groups
    }

    function toggleIn(list, key) {
        var copy = (list || []).slice()
        var idx = copy.indexOf(key)
        if (idx >= 0)
            copy.splice(idx, 1)
        else
            copy.push(key)
        return copy
    }

    function isTextEditing() {
        return searchField.activeFocus || nicknameField.activeFocus
               || hpMinBox.activeFocus || hpMaxBox.activeFocus
    }

    function listViewItem() {
        return root.tableMode ? generalTable : generalGrid
    }

    function moveSelection(delta) {
        if (!catalog || catalog.count <= 0)
            return
        var idx = catalog.indexOfName(root.selectedName)
        if (idx < 0)
            idx = 0
        var next = Math.max(0, Math.min(catalog.count - 1, idx + delta))
        if (next === idx)
            return
        root.selectGeneral(catalog.nameAt(next))
        var view = root.listViewItem()
        if (view && view.positionViewAtIndex)
            view.positionViewAtIndex(next, view.Contain)
    }

    function handleListKeys(event) {
        if (!catalog || catalog.count <= 0)
            return
        var cols = root.tableMode ? 1 : Math.max(1, root.visibleColumns)
        var page = root.tableMode ? 12 : cols * 3
        if (root.compact && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            root.showCompactPane(1)
        } else if (event.key === Qt.Key_Left)
            root.moveSelection(root.tableMode ? 0 : -1)
        else if (event.key === Qt.Key_Right)
            root.moveSelection(root.tableMode ? 0 : 1)
        else if (event.key === Qt.Key_Up)
            root.moveSelection(-cols)
        else if (event.key === Qt.Key_Down)
            root.moveSelection(cols)
        else if (event.key === Qt.Key_PageUp)
            root.moveSelection(-page)
        else if (event.key === Qt.Key_PageDown)
            root.moveSelection(page)
        else if (event.key === Qt.Key_Home)
            root.moveSelection(-catalog.count)
        else if (event.key === Qt.Key_End)
            root.moveSelection(catalog.count)
        else
            return
        event.accepted = true
    }

    Timer {
        id: detailsTimer
        interval: 48
        onTriggered: {
            if (root.selectedName.length > 0)
                root.details = homeController.generalDetails(root.selectedName)
        }
    }

    Component.onCompleted: {
        kingdomOptions = [{
            key: "all",
            label: ui("GeneralSearch", "Select All"),
            color: HomeTheme.btnPrimary,
            icon: ""
        }].concat(homeController.kingdoms())
        var packageEntries = homeController.generalPackages()
        packageGroups = groupPackages(packageEntries)
        if (packageGroups.length > 0) {
            navGroup = packageGroups[0].key
            navPackage = String(packageGroups[0].keys[0])
        }
        var saved = homeController.generalGridColumns()
        root.gridColumns = saved > 0
                ? Math.max(HomeTheme.generalGridMinColumns,
                           Math.min(saved, HomeTheme.generalGridMaxColumns))
                : HomeTheme.generalGridMinColumns
        root.gridColsReady = true
        catalogLoadTimer.start()
        root.keyboardReady = true
        if (root.visible)
            searchField.forceActiveFocus()
    }

    Timer {
        id: searchDebounce
        interval: 160
        onTriggered: {
            root.searchText = searchField.text
            root.rebuild()
        }
    }

    Timer {
        id: catalogLoadTimer
        interval: 32
        onTriggered: root.rebuild()
    }

    Keys.onEscapePressed: {
        if (skinPanel.visible)
            skinPanel.visible = false
        else if (filterPanel.visible)
            filterPanel.visible = false
        else if (root.compact && root.compactPane !== 0)
            root.showCompactPane(0)
        else
            homeController.openHome()
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Space && !root.isTextEditing())
            event.accepted = true
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 0 : HomeTheme.generalPageHMargin
        anchors.rightMargin: root.compact ? 0 : HomeTheme.generalPageHMargin
        anchors.topMargin: root.compact ? 0 : HomeTheme.generalPageTopMargin
        anchors.bottomMargin: HomeTheme.generalPageBottomMargin
        spacing: HomeTheme.generalPanelGap

        BASlantedPanel {
            id: header
            width: parent.width
            height: root.compact ? HomeTheme.catalogCompactHeaderHeight : HomeTheme.generalHeaderHeight
            slant: -0.08
            cornerRadius: 10
            shadowBlur: 0
            shadowOffset: 0
            topColor: HomeTheme.baDockTop
            bottomColor: HomeTheme.baDockBottom
            borderColor: HomeTheme.baDockBorder
            shadowColor: HomeTheme.baDockShadow

            GridLayout {
                columns: root.compact ? 2 : 6
                anchors.fill: parent
                anchors.leftMargin: root.compact ? HomeTheme.compactMargin : 28
                anchors.rightMargin: root.compact ? HomeTheme.compactMargin : 28
                anchors.bottomMargin: 8
                rowSpacing: HomeTheme.compactGap
                columnSpacing: root.compact ? HomeTheme.compactGap : 18

                Text {
                    visible: !root.compact
                    text: root.ui("GeneralOverview", "General Overview")
                    color: HomeTheme.btnSecondaryText
                    font.pixelSize: 28
                    font.bold: true
                }

                Rectangle {
                    Layout.columnSpan: root.compact ? 2 : 1
                    Layout.fillWidth: true
                    Layout.maximumWidth: root.compact ? Number.POSITIVE_INFINITY : 480
                    Layout.preferredHeight: 48
                    radius: 24
                    color: HomeTheme.btnSecondary
                    border.color: searchField.activeFocus
                                  ? HomeTheme.focusBorderHigh
                                  : HomeTheme.btnSecondaryBorder

                    Basic.TextField {
                        id: searchField
                        anchors.left: parent.left
                        anchors.right: clearSearch.visible ? clearSearch.left : parent.right
                        anchors.leftMargin: 18
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        height: parent.height
                        placeholderText: root.ui("GeneralOverview", "Search...")
                        color: HomeTheme.btnSecondaryText
                        placeholderTextColor: HomeTheme.pillText
                        font.pixelSize: 18
                        verticalAlignment: Text.AlignVCenter
                        topPadding: 0
                        bottomPadding: 0
                        leftPadding: 0
                        rightPadding: 0
                        background: Item {}
                        // 連續輸入只在停頓後重篩一次，避免每個字都重排整張網格。
                        onTextChanged: searchDebounce.restart()
                        KeyNavigation.tab: kingdomCombo
                        KeyNavigation.backtab: banBtn
                    }

                    Rectangle {
                        id: clearSearch
                        visible: searchField.text.length > 0
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22
                        height: 22
                        radius: 11
                        color: HomeTheme.btnSecondaryDown

                        Text {
                            anchors.centerIn: parent
                            text: "×"
                            color: HomeTheme.btnSecondaryText
                            font.pixelSize: 16
                            font.bold: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                searchField.clear()
                                searchDebounce.stop()
                                root.searchText = ""
                                root.rebuild()
                            }
                        }
                    }
                }

                Basic.ComboBox {
                    id: kingdomCombo
                    Layout.columnSpan: root.compact ? 2 : 1
                    Layout.preferredWidth: root.compact ? 140 : 220
                    Layout.fillWidth: root.compact
                    Layout.preferredHeight: 48
                    model: root.kingdomOptions
                    textRole: "label"
                    font.pixelSize: 16
                    Keys.onShortcutOverride: function(event) {
                        if (event.key === Qt.Key_Space)
                            event.accepted = true
                    }
                    onActivated: function(index) {
                        root.kingdomFilter = String(root.kingdomOptions[index].key)
                        root.rebuild()
                    }
                    KeyNavigation.tab: filterBtn
                    KeyNavigation.backtab: searchField

                    background: Rectangle {
                        radius: 24
                        color: HomeTheme.btnSecondary
                        border.color: kingdomCombo.down || kingdomCombo.activeFocus
                                      ? HomeTheme.focusBorderHigh
                                      : HomeTheme.btnSecondaryBorder
                    }

                    contentItem: Row {
                        leftPadding: 14
                        rightPadding: 28
                        spacing: 8

                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22
                            height: 22
                            visible: status === Image.Ready
                            fillMode: Image.PreserveAspectFit
                            source: (kingdomCombo.currentIndex >= 0
                                     && root.kingdomOptions[kingdomCombo.currentIndex])
                                    ? (root.kingdomOptions[kingdomCombo.currentIndex].icon || "")
                                    : ""
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: kingdomCombo.displayText
                            color: HomeTheme.btnSecondaryText
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                    }

                    indicator: Item {
                        x: kingdomCombo.width - 22
                        y: (kingdomCombo.height - 8) / 2
                        width: 8
                        height: 8
                        Text {
                            anchors.centerIn: parent
                            text: "▾"
                            color: HomeTheme.pillText
                            font.pixelSize: 12
                        }
                    }

                    popup: Popup {
                        y: kingdomCombo.height + 6
                        width: kingdomCombo.width
                        padding: 6
                        implicitHeight: Math.min(420, contentItem.implicitHeight + 12)

                        background: Rectangle {
                            radius: 12
                            color: HomeTheme.baDockTop
                            border.color: HomeTheme.baDockBorder
                        }

                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: kingdomCombo.popup.visible ? kingdomCombo.delegateModel : null
                            currentIndex: kingdomCombo.highlightedIndex
                            ScrollBar.vertical: HomeScrollBar { }
                        }
                    }

                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: kingdomCombo.width
                        height: 42
                        highlighted: kingdomCombo.highlightedIndex === index

                        background: Rectangle {
                            radius: 8
                            color: parent.highlighted ? HomeTheme.navBgActive : "transparent"
                        }

                        contentItem: Row {
                            spacing: 8
                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 22
                                height: 22
                                fillMode: Image.PreserveAspectFit
                                source: modelData.icon || ""
                                visible: status === Image.Ready
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label || ""
                                color: HomeTheme.btnSecondaryText
                                font.pixelSize: 15
                            }
                        }
                    }
                }

                BAToolButton {
                    id: filterBtn
                    // Compact opens the same panel from the pane bar's filter tab.
                    visible: !root.compact
                    text: root.ui("GeneralOverview", "Search...")
                    implicitWidth: root.compact ? 100 : 140
                    Layout.fillWidth: root.compact
                    onClicked: filterPanel.visible = !filterPanel.visible
                    KeyNavigation.tab: colSlider
                    KeyNavigation.backtab: kingdomCombo
                }

                Text {
                    visible: !root.compact
                    text: String(catalog ? catalog.count : 0)
                    color: HomeTheme.btnSecondaryText
                    font.pixelSize: 22
                    font.bold: true
                }

                BASlantedPanel {
                    id: colSliderPanel
                    visible: !root.compact
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                    implicitWidth: 204
                    implicitHeight: 44
                    slant: -0.10
                    cornerRadius: 8
                    shadowBlur: 6
                    shadowOffset: 3
                    borderWidth: 1
                    topColor: HomeTheme.baToolTop
                    bottomColor: HomeTheme.baToolBottom
                    borderColor: HomeTheme.baDockBorder
                    shadowColor: HomeTheme.baDockShadow

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 6

                    Basic.Slider {
                        id: colSlider
                        anchors.verticalCenter: parent.verticalCenter
                        from: HomeTheme.generalGridMinColumns
                        to: HomeTheme.generalGridMaxColumns
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        live: true
                        value: root.gridColumns
                        implicitWidth: 148
                        implicitHeight: 36
                        onMoved: root.applyGridColumns(value)
                        KeyNavigation.tab: groupNav
                        KeyNavigation.backtab: filterBtn
                        Keys.onShortcutOverride: function(event) {
                            if (event.key === Qt.Key_Space)
                                event.accepted = true
                        }

                        background: Rectangle {
                            x: colSlider.leftPadding
                            y: colSlider.topPadding + colSlider.availableHeight / 2 - height / 2
                            implicitWidth: 148
                            implicitHeight: 6
                            width: colSlider.availableWidth
                            height: implicitHeight
                            radius: 3
                            color: HomeTheme.btnSecondary
                            Rectangle {
                                width: colSlider.visualPosition * parent.width
                                height: parent.height
                                color: HomeTheme.baSky
                                radius: 3
                            }
                        }

                        handle: Rectangle {
                            x: colSlider.leftPadding + colSlider.visualPosition * (colSlider.availableWidth - width)
                            y: colSlider.topPadding + colSlider.availableHeight / 2 - height / 2
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            color: HomeTheme.baWhite
                            border.color: HomeTheme.baSky
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: String(root.gridColumns)
                        color: HomeTheme.btnSecondaryText
                        font.pixelSize: 16
                        font.bold: true
                        width: 24
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
            }
        }

            CatalogPaneBar {
                id: compactBar
                visible: root.compact
                width: parent.width
                height: visible ? HomeTheme.compactTouch : 0
                currentIndex: filterPanel.visible ? 2 : root.compactPane
                itemCount: root.catalog ? root.catalog.count : 0
                detailsEnabled: root.selectedName.length > 0 || root.sameNameFilter.length > 0
                onActivated: function(index) {
                    if (index === 2) filterPanel.visible = true
                    else root.showCompactPane(index)
                }
            }

            Column {
                id: compactNav
                visible: root.compact && root.compactPane === 0
                width: parent.width
                height: visible ? HomeTheme.compactTouch * 2 + HomeTheme.compactGap : 0
                spacing: HomeTheme.compactGap
                opacity: root.navScoped ? 1 : 0.55

                PackageNavList {
                    horizontal: true
                    width: parent.width
                    height: HomeTheme.compactTouch
                    entries: root.packageGroups
                    currentKey: root.navGroup
                    onPicked: function(key) { root.pickNavGroup(key) }
                }

                PackageNavList {
                    horizontal: true
                    width: parent.width
                    height: HomeTheme.compactTouch
                    entries: root.navPackageEntries
                    currentKey: root.navPackage
                    onPicked: function(key) { root.pickNavPackage(key) }
                }
            }

            Row {
                width: parent.width
                height: Math.max(0, parent.height - header.height - HomeTheme.generalPanelGap
                                 - (root.compact ? compactBar.height + HomeTheme.generalPanelGap : 0)
                                 - (compactNav.visible ? compactNav.height + HomeTheme.generalPanelGap : 0))
                spacing: HomeTheme.generalPanelGap

            BASlantedPanel {
                id: navPanel
                visible: !root.compact
                width: HomeTheme.generalPackageNavWidth
                height: parent.height
                slant: 0
                cornerRadius: 10
                shadowBlur: 0
                shadowOffset: 0
                topColor: HomeTheme.baDockTop
                bottomColor: HomeTheme.baDockBottom
                borderColor: HomeTheme.baDockBorder
                shadowColor: HomeTheme.baDockShadow
                opacity: root.navScoped ? 1 : 0.55

                PackageNavList {
                    id: groupNav
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    width: Math.round((parent.width - 30) * 0.45)
                    entries: root.packageGroups
                    currentKey: root.navGroup
                    onPicked: function(key) { root.pickNavGroup(key) }
                    KeyNavigation.tab: packageNav
                    KeyNavigation.backtab: colSlider
                }

                Rectangle {
                    anchors.left: groupNav.right
                    anchors.leftMargin: 4
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 14
                    anchors.bottomMargin: 14
                    width: 1
                    color: HomeTheme.baDockBorder
                }

                PackageNavList {
                    id: packageNav
                    anchors.left: groupNav.right
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 10
                    entries: root.navPackageEntries
                    currentKey: root.navPackage
                    onPicked: function(key) { root.pickNavPackage(key) }
                    KeyNavigation.tab: root.tableMode ? generalTable : generalGrid
                    KeyNavigation.backtab: groupNav
                }
            }

            BASlantedPanel {
                id: listPanel
                visible: !root.compact || root.compactPane === 0
                width: root.compact ? parent.width : Math.round((parent.width - HomeTheme.generalPanelGap) * HomeTheme.generalListShare)
                height: parent.height
                slant: 0
                cornerRadius: 10
                shadowBlur: 0
                shadowOffset: 0
                topColor: HomeTheme.baDockTop
                bottomColor: HomeTheme.baDockBottom
                borderColor: HomeTheme.baDockBorder
                shadowColor: HomeTheme.baDockShadow

                GridView {
                    id: generalGrid
                    visible: !root.tableMode
                    anchors.fill: parent
                    anchors.margins: HomeTheme.generalGridMargin
                    anchors.bottomMargin: 22
                    clip: true
                    focus: !root.tableMode
                    keyNavigationEnabled: false
                    activeFocusOnTab: true
                    cellWidth: root.compact ? Math.floor(width / root.visibleColumns) : HomeTheme.generalCellWidth(width, root.gridColumns)
                    cellHeight: root.compact ? cellWidth * HomeTheme.generalCellAspect : HomeTheme.generalCellHeight(width, root.gridColumns)
                    cacheBuffer: cellHeight * 2
                    reuseItems: false
                    model: (root.tableMode || root.catalogPending) ? null : root.catalog
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: HomeScrollBar { }
                    KeyNavigation.tab: root.compact ? compactBar.detailButton : sameNameBtn
                    KeyNavigation.backtab: root.compact ? compactBar.listButton : packageNav
                    Keys.onPressed: function(event) { root.handleListKeys(event) }
                    onWidthChanged: {
                        if (root.gridColsReady || width <= 1)
                            return
                        var saved = homeController.generalGridColumns()
                        root.gridColumns = saved > 0
                                ? Math.max(HomeTheme.generalGridMinColumns,
                                           Math.min(saved, HomeTheme.generalGridMaxColumns))
                                : HomeTheme.generalGridMinColumns
                        root.gridColsReady = true
                    }

                    delegate: Item {
                        id: delegateRoot
                        required property int index
                        required property string name
                        required property string displayName
                        required property string companions
                        required property string companionLabel
                        required property string kingdoms
                        required property int maxHp
                        required property int startHp
                        required property bool lord
                        required property bool hidden

                        width: generalGrid.cellWidth
                        height: generalGrid.cellHeight
                        readonly property int artRev: homeController.artRevision
                        property bool artEnabled: false

                        function restartArt() {
                            artEnabled = false
                            artEnableTimer.interval = 16 + (Math.max(0, index) % 12) * 24
                            if (name && root.visible)
                                artEnableTimer.restart()
                            else
                                artEnableTimer.stop()
                        }

                        Timer {
                            id: artEnableTimer
                            repeat: false
                            onTriggered: {
                                if (!root.visible) {
                                    interval = 32
                                    start()
                                    return
                                }
                                delegateRoot.artEnabled = true
                            }
                        }

                        Component.onCompleted: restartArt()
                        GridView.onReused: restartArt()
                        GridView.onPooled: {
                            artEnableTimer.stop()
                            artEnabled = false
                        }

                        Connections {
                            target: root
                            function onVisibleChanged() {
                                if (root.visible && !delegateRoot.artEnabled && delegateRoot.name)
                                    delegateRoot.restartArt()
                                else if (!root.visible)
                                    artEnableTimer.stop()
                            }
                        }

                        Rectangle {
                            id: cardFrame
                            anchors.fill: parent
                            anchors.margins: HomeTheme.generalCellInset
                            radius: 8
                            clip: true
                            transformOrigin: Item.Center
                            scale: root.uiScale
                            color: root.selectedName === delegateRoot.name
                                   ? HomeTheme.navBgActive
                                   : HomeTheme.btnSecondary
                            border.width: root.selectedName === delegateRoot.name ? 2 : 1
                            border.color: root.selectedName === delegateRoot.name
                                          ? HomeTheme.focusBorderHigh
                                          : HomeTheme.btnSecondaryBorder

                            Image {
                                id: listFullskin
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                visible: true
                                opacity: status === Image.Ready ? (delegateRoot.hidden ? 0.62 : 1) : 0
                                sourceSize.width: width > 1 ? Math.ceil(width) : 0
                                sourceSize.height: height > 1 ? Math.ceil(height) : 0
                                source: (delegateRoot.artEnabled && delegateRoot.name && delegateRoot.artRev >= 0)
                                        ? homeController.generalFullImage(delegateRoot.name) : ""
                            }

                            SkeletonBlock {
                                anchors.fill: parent
                                radius: 8
                                visible: listFullskin.status !== Image.Ready
                            }

                            HiddenMark {
                                visible: delegateRoot.hidden
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.rightMargin: 6
                                anchors.topMargin: 6
                                z: 2
                            }

                            ListMetaOverlay {
                                visible: delegateRoot.width >= 100
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.leftMargin: 5
                                anchors.topMargin: 5
                                anchors.rightMargin: 5
                                kingdoms: delegateRoot.kingdoms
                                maxHp: delegateRoot.maxHp
                                startHp: delegateRoot.startHp
                                lord: delegateRoot.lord
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: nameRow.height + 8
                                color: HomeTheme.onArtScrim

                                Row {
                                    id: nameRow
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 6
                                    width: parent.width - 12
                                    spacing: 4

                                    Image {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        height: 14
                                        visible: delegateRoot.lord
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: true
                                        source: homeController.lordIcon()
                                    }

                                    Text {
                                        width: Math.max(0, parent.width - (delegateRoot.lord ? 18 : 0)
                                                         - (companionText.visible ? companionText.width + parent.spacing : 0))
                                        text: delegateRoot.displayName
                                        elide: Text.ElideRight
                                        color: HomeTheme.onArtText
                                        font.pixelSize: Math.max(10, Math.round(delegateRoot.width / 10))
                                        font.bold: true
                                    }

                                    Text {
                                        id: companionText
                                        visible: delegateRoot.companions.length > 0
                                        width: Math.min(implicitWidth, parent.width * 0.45)
                                        text: delegateRoot.companionLabel + ": " + delegateRoot.companions
                                        elide: Text.ElideRight
                                        color: HomeTheme.onArtText
                                        opacity: 0.9
                                        font.pixelSize: Math.max(9, Math.round(delegateRoot.width / 12))
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.selectGeneral(delegateRoot.name)
                                    if (root.compact) root.showCompactPane(1)
                                }
                            }
                        }
                    }
                }

                Column {
                    id: tablePane
                    visible: root.tableMode
                    anchors.fill: parent
                    anchors.margins: HomeTheme.generalGridMargin
                    anchors.bottomMargin: 22
                    spacing: 0

                    Row {
                        id: tableHeader
                        width: parent.width
                        height: HomeTheme.generalTableHeaderHeight

                        Repeater {
                            model: [
                                { share: 0.20, label: qsTranslate("GeneralOverview", "Nick") },
                                { share: 0.22, label: qsTranslate("GeneralOverview", "General") },
                                { share: 0.16, label: qsTranslate("GeneralOverview", "Kingdom") },
                                { share: 0.12, label: qsTranslate("GeneralOverview", "Gender") },
                                { share: 0.10, label: qsTranslate("GeneralOverview", "MaxHP") },
                                { share: 0.20, label: qsTranslate("GeneralOverview", "Package") }
                            ]
                            delegate: Text {
                                required property var modelData
                                width: tableHeader.width * modelData.share
                                height: tableHeader.height
                                text: modelData.label
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: HomeTheme.pillText
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: HomeTheme.baDockBorder
                    }

                    ListView {
                        id: generalTable
                        width: parent.width
                        height: parent.height - tableHeader.height - 1
                        clip: true
                        focus: root.tableMode
                        keyNavigationEnabled: false
                        activeFocusOnTab: true
                        reuseItems: false
                        cacheBuffer: HomeTheme.generalTableRowHeight * 8
                        model: root.catalogPending ? null : root.catalog
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: HomeScrollBar { }
                        KeyNavigation.tab: sameNameBtn
                        KeyNavigation.backtab: packageNav
                        Keys.onPressed: function(event) { root.handleListKeys(event) }

                        delegate: Item {
                            id: tableRow
                            required property int index
                            required property string name
                            required property string displayName
                            required property string companions
                            required property string companionLabel
                            required property string nickname
                            required property string kingdoms
                            required property string gender
                            required property string kingdomDisplay
                            required property string genderDisplay
                            required property int maxHp
                            required property int startHp
                            required property string packageName
                            required property bool hidden

                            width: generalTable.width
                            height: HomeTheme.generalTableRowHeight
                            objectName: "general-row-" + index

                            Rectangle {
                                anchors.fill: parent
                                color: root.selectedName === tableRow.name
                                       ? HomeTheme.tableRowSelected
                                       : (tableRow.hidden ? HomeTheme.tableRowHidden : "transparent")

                                Rectangle {
                                    visible: root.selectedName === tableRow.name
                                    width: 4
                                    height: parent.height
                                    color: HomeTheme.baSky
                                }

                                Row {
                                    anchors.fill: parent

                                    Text {
                                        width: parent.width * 0.20
                                        height: parent.height
                                        text: tableRow.nickname
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: root.selectedName === tableRow.name
                                               ? HomeTheme.tableRowSelectedText
                                               : (tableRow.hidden ? HomeTheme.tableRowHiddenText
                                                                  : HomeTheme.btnSecondaryText)
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: parent.width * 0.22
                                        height: parent.height
                                        text: root.generalNameWithCompanions(tableRow.displayName,
                                                                             tableRow.companions,
                                                                             tableRow.companionLabel)
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: root.selectedName === tableRow.name
                                               ? HomeTheme.tableRowSelectedText
                                               : (tableRow.hidden ? HomeTheme.tableRowHiddenText
                                                                  : HomeTheme.btnSecondaryText)
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                    Text {
                                        width: parent.width * 0.16
                                        height: parent.height
                                        text: tableRow.kingdomDisplay
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: root.selectedName === tableRow.name
                                               ? HomeTheme.tableRowSelectedText
                                               : (tableRow.hidden ? HomeTheme.tableRowHiddenText
                                                                  : HomeTheme.btnSecondaryText)
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: parent.width * 0.12
                                        height: parent.height
                                        text: tableRow.genderDisplay
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: root.selectedName === tableRow.name
                                               ? HomeTheme.tableRowSelectedText
                                               : (tableRow.hidden ? HomeTheme.tableRowHiddenText
                                                                  : HomeTheme.btnSecondaryText)
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: parent.width * 0.10
                                        height: parent.height
                                        text: root.hpText(tableRow.startHp, tableRow.maxHp)
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: root.selectedName === tableRow.name
                                               ? HomeTheme.tableRowSelectedText
                                               : (tableRow.hidden ? HomeTheme.tableRowHiddenText
                                                                  : HomeTheme.btnSecondaryText)
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        width: parent.width * 0.20
                                        height: parent.height
                                        text: tableRow.packageName
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: root.selectedName === tableRow.name
                                               ? HomeTheme.tableRowSelectedText
                                               : (tableRow.hidden ? HomeTheme.tableRowHiddenText
                                                                  : HomeTheme.btnSecondaryText)
                                        font.pixelSize: 12
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.selectGeneral(tableRow.name)
                                }
                            }
                        }
                    }
                }

                Item {
                    id: listSkeleton
                    anchors.fill: parent
                    anchors.margins: HomeTheme.generalGridMargin
                    visible: root.catalogPending

                    property int cellW: HomeTheme.generalCellWidth(width, root.gridColumns)
                    property int cellH: HomeTheme.generalCellHeight(width, root.gridColumns)
                    property int cols: HomeTheme.generalCellColumns(width, root.gridColumns)

                    Grid {
                        visible: !root.tableMode
                        anchors.fill: parent
                        columns: listSkeleton.cols

                        Repeater {
                            model: listSkeleton.cols * 3
                            Item {
                                width: listSkeleton.cellW
                                height: listSkeleton.cellH
                                SkeletonBlock {
                                    anchors.fill: parent
                                    anchors.margins: HomeTheme.generalCellInset
                                    radius: 8
                                }
                            }
                        }
                    }

                    Column {
                        visible: root.tableMode
                        anchors.fill: parent
                        spacing: 4
                        Repeater {
                            model: 12
                            SkeletonBlock {
                                width: listSkeleton.width
                                height: HomeTheme.generalTableRowHeight
                                radius: 4
                            }
                        }
                    }
                }
            }

            BASlantedPanel {
                id: detailPanel
                visible: !root.compact || root.compactPane === 1
                width: root.compact ? parent.width
                                    : parent.width - navPanel.width - listPanel.width - HomeTheme.generalPanelGap * 2
                height: parent.height
                slant: 0
                cornerRadius: 10
                shadowBlur: 0
                shadowOffset: 0
                topColor: HomeTheme.baDockTop
                bottomColor: HomeTheme.baDockBottom
                borderColor: HomeTheme.baDockBorder
                clip: true

                Flickable {
                    id: detailFlick
                    anchors.fill: parent
                    anchors.margins: 14
                    contentWidth: width
                    contentHeight: detailLayout.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    activeFocusOnTab: root.compact
                    ScrollBar.vertical: HomeScrollBar { }
                    KeyNavigation.tab: sameNameBtn
                    KeyNavigation.backtab: compactBar.detailButton
                    Keys.onPressed: function(event) {
                        if (!root.compact) return
                        var step = event.key === Qt.Key_Down ? 48 : event.key === Qt.Key_Up ? -48
                                 : event.key === Qt.Key_PageDown ? height : event.key === Qt.Key_PageUp ? -height : 0
                        if (!step) return
                        contentY = Math.max(0, Math.min(contentHeight - height, contentY + step))
                        event.accepted = true
                    }

                GridLayout {
                    id: detailLayout
                    columns: root.compact ? 1 : 2
                    width: detailFlick.width
                    // Keep the original skill/voice panels; the portrait detail scrolls vertically.
                    height: root.compact ? Math.max(detailFlick.height, HomeTheme.catalogDetailContentHeight, implicitHeight)
                                         : detailFlick.height
                    rowSpacing: 12
                    columnSpacing: 12

                    Item {
                        Layout.preferredWidth: root.compact ? detailLayout.width : Math.round(detailPanel.width * 0.33)
                        Layout.preferredHeight: root.compact ? HomeTheme.catalogPortraitHeight : -1
                        Layout.fillHeight: !root.compact
                        clip: true
                        transformOrigin: Item.Top
                        scale: root.uiScale

                        SkeletonBlock {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            width: parent.width
                            height: Math.min(parent.height, parent.width * 1.45)
                            radius: 10
                            visible: root.catalogPending
                                     || (selectedName.length > 0 && !cardImg.hasFrame)
                        }

                        Image {
                            id: cardImg
                            // 換圖時保留舊圖到新圖解碼完成；只有從無到有才顯示骨架。
                            property bool hasFrame: false
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            width: parent.width
                            height: Math.min(parent.height, parent.width * 1.45)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            retainWhileLoading: true
                            cache: true
                            smooth: true
                            mipmap: false
                            opacity: hasFrame ? 1 : 0
                            source: (root.detailsReady && homeController.artRevision >= 0)
                                    ? homeController.generalCardImage(root.shownName) : ""
                            onStatusChanged: {
                                if (status === Image.Ready)
                                    hasFrame = true
                                else if (status !== Image.Loading)
                                    hasFrame = false
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: !root.compact
                        transformOrigin: Item.TopLeft
                        scale: root.uiScale
                        spacing: 8

                        SkeletonBlock {
                            visible: root.catalogPending || root.detailsPending
                            Layout.preferredWidth: 140
                            Layout.preferredHeight: 16
                        }

                        SkeletonBlock {
                            visible: root.catalogPending || root.detailsPending
                            Layout.preferredWidth: 220
                            Layout.preferredHeight: 32
                        }

                        Row {
                            visible: root.catalogPending || root.detailsPending
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: 4
                                SkeletonBlock {
                                    width: 72
                                    height: 24
                                    radius: 4
                                }
                            }
                        }

                        CopyableText {
                            Layout.fillWidth: true
                            visible: root.detailsReady && String(details.nickname || "").length > 0
                            text: details.nickname || ""
                            color: HomeTheme.pillText
                            font.pixelSize: 15
                        }

                        Row {
                            Layout.fillWidth: true
                            visible: root.detailsReady || (!root.catalogPending && selectedName.length === 0)
                            spacing: 8

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 28
                                height: 28
                                visible: details.lord === true
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: true
                                source: homeController.lordIcon()
                            }

                            HiddenMark {
                                visible: details.hidden === true
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            CopyableText {
                                id: generalNameText
                                width: Math.max(0, parent.width
                                    - (details.lord === true ? 36 : 0)
                                    - (details.hidden === true ? 36 : 0)
                                    - (companionBadge.visible ? companionBadge.width + parent.spacing : 0))
                                text: details.displayName || root.ui("ChooseGeneralDialog", "Choose general")
                                color: HomeTheme.btnSecondaryText
                                font.pixelSize: 32
                                font.bold: true
                            }

                            MetaBadge {
                                id: companionBadge
                                visible: root.detailsReady && String(details.companions || "").length > 0
                                width: Math.min(implicitWidth, Math.max(80, parent.width * 0.42))
                                label: details.companionLabel + ": " + (details.companions || "")
                            }
                        }

                        CopyableText {
                            Layout.fillWidth: true
                            visible: root.detailsReady && String(details.mapping || "").length > 0
                            text: details.mapping || ""
                            color: HomeTheme.pillText
                            font.pixelSize: 13
                        }

                        Flow {
                            Layout.fillWidth: true
                            visible: root.detailsReady
                            spacing: 10

                            Repeater {
                                model: root.kingdomKeys
                                delegate: Image {
                                    required property var modelData
                                    width: 28
                                    height: 28
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: true
                                    source: homeController.kingdomIcon(String(modelData))
                                }
                            }

                            Repeater {
                                model: Number(details.maxHp || 0)
                                delegate: Image {
                                    required property int modelData
                                    width: 22
                                    height: 22
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: true
                                    source: homeController.magatamaImage(
                                                modelData < Number(details.startHp || 0) ? 5 : 0)
                                }
                            }

                            Repeater {
                                model: Number(details.startHujia || 0)
                                delegate: Image {
                                    width: 22
                                    height: 22
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: true
                                    source: homeController.hujiaImage()
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            visible: root.detailsReady
                            spacing: 8

                            MetaBadge {
                                visible: details.hidden === true
                                label: qsTranslate("GeneralOverview", "Hidden")
                                accent: HomeTheme.hiddenBadge
                            }

                            MetaBadge {
                                visible: String(details.package || "").length > 0
                                label: root.ui("GeneralOverview", "Package") + " " + (details.package || "")
                            }

                            MetaBadge {
                                visible: String(details.illustrator || "").length > 0
                                label: root.ui("GeneralOverview", "Illustrator") + " " + (details.illustrator || "")
                            }

                            MetaBadge {
                                visible: String(details.cv || "").length > 0
                                label: qsTranslate("GeneralOverview", "CV") + " " + (details.cv || "")
                            }

                            MetaBadge {
                                visible: String(details.designer || "").length > 0
                                label: root.ui("GeneralOverview", "Designer") + " " + (details.designer || "")
                            }

                            MetaBadge {
                                visible: String(details.companions || "").length > 0
                                label: details.companionLabel + ": " + (details.companions || "")
                            }
                        }

                        Row {
                            Layout.fillWidth: true
                            visible: root.detailsReady || root.detailsPending
                            spacing: 8

                            Repeater {
                                model: [
                                    { key: 0, label: qsTranslate("GeneralOverview", "Skill details") },
                                    { key: 1, label: qsTranslate("GeneralOverview", "Voice lines") }
                                ]
                                delegate: Rectangle {
                                    required property var modelData
                                    implicitWidth: tabLabel.implicitWidth + 24
                                    height: 36
                                    radius: 8
                                    color: root.detailTab === modelData.key
                                           ? (modelData.key === 0 ? HomeTheme.tabSkillsBg : HomeTheme.tabVoiceBg)
                                           : HomeTheme.btnSecondary
                                    border.width: 1
                                    border.color: root.detailTab === modelData.key
                                                  ? (modelData.key === 0 ? HomeTheme.tabSkillsBorder : HomeTheme.tabVoiceBorder)
                                                  : HomeTheme.btnSecondaryBorder

                                    Text {
                                        id: tabLabel
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: HomeTheme.btnSecondaryText
                                        font.pixelSize: 15
                                        font.bold: root.detailTab === modelData.key
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.detailTab = modelData.key
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: !root.compact
                            Layout.preferredHeight: root.compact ? skillColumn.height + 20 : -1
                            visible: root.detailTab === 0
                            radius: 8
                            color: HomeTheme.tabSkillsBg
                            border.width: 1
                            border.color: HomeTheme.tabSkillsBorder

                            Flickable {
                                interactive: !root.compact
                                anchors.fill: parent
                                anchors.margins: 10
                                contentWidth: width
                                contentHeight: skillColumn.height
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: HomeScrollBar { }

                                Column {
                                    id: skillColumn
                                    width: parent.width
                                    spacing: 14

                                    Column {
                                        width: parent.width
                                        visible: root.catalogPending || root.detailsPending
                                        height: visible ? implicitHeight : 0
                                        spacing: 16

                                        Repeater {
                                            model: 2
                                            Column {
                                                width: skillColumn.width
                                                spacing: 8
                                                SkeletonBlock { width: 168; height: 22 }
                                                SkeletonBlock { width: parent.width; height: 14 }
                                                SkeletonBlock { width: parent.width * 0.88; height: 14 }
                                                SkeletonBlock { width: parent.width * 0.62; height: 14 }
                                            }
                                        }
                                    }

                                    CopyableText {
                                        width: parent.width
                                        visible: root.detailsReady && String(details.information || "").length > 0
                                        text: details.information || ""
                                        // 簡介常以填充字元開頭再接 <i>/<br>，AutoText 認不成 HTML。
                                        textFormat: TextEdit.RichText
                                        color: HomeTheme.pillText
                                        font.pixelSize: 13
                                    }

                                    CopyableText {
                                        width: parent.width
                                        visible: root.detailsReady && String(details.oracleText || "").length > 0
                                        text: details.oracleText || ""
                                        color: HomeTheme.pillText
                                        font.pixelSize: 13
                                    }

                                    Repeater {
                                        model: root.detailsReady ? (details.skills || []) : []
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: skillColumn.width
                                            height: skillBody.height + 16
                                            radius: 8
                                            color: HomeTheme.nativeSkillBg
                                            border.width: 1
                                            border.color: HomeTheme.tabSkillsBorder

                                            Column {
                                                id: skillBody
                                                x: 12
                                                y: 8
                                                width: parent.width - 24
                                                spacing: 8

                                                Flow {
                                                    width: parent.width
                                                    spacing: 8

                                                    ParallelogramPlate {
                                                        label: modelData.displayName || ""
                                                        related: modelData.related === true
                                                    }

                                                    Repeater {
                                                        model: modelData.tags || []
                                                        delegate: MetaBadge {
                                                            required property var modelData
                                                            label: String(modelData)
                                                            accent: HomeTheme.baSky
                                                        }
                                                    }
                                                }

                                                CopyableText {
                                                    width: parent.width
                                                    text: modelData.description || ""
                                                    textFormat: TextEdit.RichText
                                                    color: HomeTheme.btnSecondaryText
                                                    font.pixelSize: 15
                                                }

                                                CopyableText {
                                                    width: parent.width
                                                    visible: String(modelData.oracleText || "").length > 0
                                                    text: modelData.oracleText || ""
                                                    color: HomeTheme.pillText
                                                    font.pixelSize: 13
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: !root.compact
                            Layout.preferredHeight: root.compact ? lineListColumn.height + 20 : -1
                            visible: root.detailTab === 1
                            radius: 8
                            color: HomeTheme.tabVoiceBg
                            border.width: 1
                            border.color: HomeTheme.tabVoiceBorder

                            Flickable {
                                interactive: !root.compact
                                anchors.fill: parent
                                anchors.margins: 10
                                contentWidth: width
                                contentHeight: lineListColumn.height
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: HomeScrollBar { }

                                Column {
                                    id: lineListColumn
                                    width: parent.width
                                    spacing: 10

                                    Column {
                                        width: parent.width
                                        visible: root.catalogPending || root.detailsPending
                                        height: visible ? implicitHeight : 0
                                        spacing: 10

                                        Repeater {
                                            model: 4
                                            SkeletonBlock {
                                                width: lineListColumn.width
                                                height: 64
                                                radius: 8
                                            }
                                        }
                                    }

                                    Repeater {
                                        model: root.detailsReady ? (details.lines || []) : []
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: lineListColumn.width
                                            height: Math.max(lineColumn.height + 16, 48)
                                            radius: 8
                                            color: HomeTheme.btnSecondary
                                            border.color: HomeTheme.tabVoiceBorder
                                            opacity: modelData.enabled ? 1 : 0.7

                                            Rectangle {
                                                id: playBtn
                                                anchors.left: parent.left
                                                anchors.leftMargin: 10
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 28
                                                height: 28
                                                radius: 14
                                                color: modelData.enabled ? HomeTheme.tabVoiceBorder : HomeTheme.btnSecondaryBorder

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "▶"
                                                    color: "#FFFFFF"
                                                    font.pixelSize: 12
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    enabled: modelData.enabled === true
                                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                    onClicked: homeController.playAudio(modelData.audio || "")
                                                }
                                            }

                                            Column {
                                                id: lineColumn
                                                anchors.left: playBtn.right
                                                anchors.leftMargin: 10
                                                anchors.right: parent.right
                                                anchors.rightMargin: 12
                                                anchors.verticalCenter: parent.verticalCenter
                                                spacing: 4

                                                CopyableText {
                                                    width: parent.width
                                                    text: modelData.title || ""
                                                    color: HomeTheme.btnSecondaryText
                                                    font.pixelSize: 15
                                                    font.bold: true
                                                }

                                                CopyableText {
                                                    width: parent.width
                                                    text: modelData.text || ""
                                                    color: HomeTheme.pillText
                                                    font.pixelSize: 13
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        BAToolButton {
                            id: sameNameBtn
                            Layout.fillWidth: true
                            implicitHeight: 48
                            enabled: root.selectedName.length > 0 || root.sameNameFilter.length > 0
                            text: root.sameNameFilter.length > 0
                                  ? qsTranslate("GeneralOverview", "Clear same-name filter")
                                  : qsTranslate("GeneralOverview", "Same-name generals")
                            onActiveFocusChanged: if (activeFocus) root.revealDetailControl(this)
                            onClicked: {
                                // Keep the clicked general as the anchor while browsing its variants.
                                root.sameNameFilter = root.sameNameFilter.length > 0 ? "" : root.selectedName
                                root.rebuild()
                                if (root.compact && root.catalog && root.catalog.count > 0)
                                    root.showCompactPane(0)
                            }
                            KeyNavigation.tab: skinBtn.visible ? skinBtn : avatarBtn
                            KeyNavigation.backtab: root.tableMode ? generalTable : generalGrid
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            spacing: 8
                            visible: root.detailsReady

                            BAToolButton {
                                id: skinBtn
                                onActiveFocusChanged: if (activeFocus) root.revealDetailControl(this)
                                visible: details.hasSkin === true
                                Layout.fillWidth: true
                                implicitWidth: 80
                                implicitHeight: 48
                                text: root.ui("GeneralOverview", "changeHeroSkin")
                                onClicked: skinPanel.open()
                                KeyNavigation.tab: avatarBtn
                                KeyNavigation.backtab: sameNameBtn
                            }

                            BAToolButton {
                                id: avatarBtn
                                onActiveFocusChanged: if (activeFocus) root.revealDetailControl(this)
                                Layout.fillWidth: true
                                implicitWidth: 80
                                implicitHeight: 48
                                enabled: details.isAvatar !== true
                                text: details.isAvatar === true
                                      ? qsTranslate("GeneralOverview", "Current avatar")
                                      : qsTranslate("GeneralOverview", "Set as avatar")
                                onClicked: {
                                    homeController.setUserAvatar(root.shownName)
                                    root.reloadDetails()
                                }
                                KeyNavigation.tab: banBtn
                                KeyNavigation.backtab: skinBtn.visible ? skinBtn : sameNameBtn
                            }

                            BAToolButton {
                                id: banBtn
                                onActiveFocusChanged: if (activeFocus) root.revealDetailControl(this)
                                Layout.fillWidth: true
                                implicitWidth: 80
                                implicitHeight: 48
                                text: details.banned === true
                                      ? root.ui("GeneralOverview", "untieGeneral")
                                      : root.ui("GeneralOverview", "banGeneral")
                                onClicked: {
                                    homeController.setGeneralBanned(
                                                root.shownName, details.banned !== true)
                                    root.reloadDetails()
                                }
                                KeyNavigation.tab: searchField
                                KeyNavigation.backtab: avatarBtn
                            }
                        }
                    }
                }
                }
            }
        }
    }

    Item {
        id: skinPanel
        visible: false
        anchors.fill: parent
        z: 90
        property var skins: []
        property int currentIndex: 0
        focus: visible

        function open() {
            skins = homeController.heroSkinList(root.shownName)
            currentIndex = 0
            for (var i = 0; i < skins.length; ++i) {
                if (skins[i].current === true)
                    currentIndex = i
            }
            visible = true
            forceActiveFocus()
        }

        Keys.onPressed: function(event) {
            if (!visible)
                return
            if (event.key === Qt.Key_Left) {
                currentIndex = Math.max(0, currentIndex - 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                currentIndex = Math.min(Math.max(0, skins.length - 1), currentIndex + 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (currentIndex >= 0 && currentIndex < skins.length) {
                    homeController.setHeroSkin(root.shownName, Number(skins[currentIndex].index))
                    root.reloadDetails()
                    visible = false
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Space) {
                event.accepted = true
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: skinPanel.visible = false
        }

        BASlantedPanel {
            anchors.centerIn: parent
            width: Math.min(parent.width - (root.compact ? 16 : 80), 760)
            height: Math.min(parent.height - (root.compact ? 16 : 80), 520)
            slant: -0.04
            cornerRadius: 12
            shadowBlur: 0
            shadowOffset: 0
            topColor: HomeTheme.baDockTop
            bottomColor: HomeTheme.baDockBottom
            borderColor: HomeTheme.baDockBorder

            MouseArea { anchors.fill: parent }

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                RowLayout {
                    width: parent.width
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: root.ui("GeneralOverview", "changeHeroSkin")
                        color: HomeTheme.btnSecondaryText
                        font.pixelSize: 22
                        font.bold: true
                    }

                    BAToolButton {
                        implicitWidth: 88
                        implicitHeight: 40
                        text: qsTranslate("GeneralOverview", "OK")
                        onClicked: skinPanel.visible = false
                    }
                }

                Flickable {
                    width: parent.width
                    height: parent.height - 56
                    contentWidth: width
                    contentHeight: skinFlow.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: HomeScrollBar { }

                    Flow {
                        id: skinFlow
                        width: parent.width
                        spacing: 10

                        Repeater {
                            model: skinPanel.skins
                            delegate: Item {
                                required property var modelData
                                required property int index
                                width: 108
                                height: 176

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 8
                                    clip: true
                                    color: HomeTheme.btnSecondary
                                    border.width: (modelData.current === true
                                                   || index === skinPanel.currentIndex) ? 2 : 1
                                    border.color: (modelData.current === true
                                                   || index === skinPanel.currentIndex)
                                                  ? HomeTheme.focusBorderHigh
                                                  : HomeTheme.btnSecondaryBorder

                                    Image {
                                        anchors.fill: parent
                                        anchors.bottomMargin: 28
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                        source: modelData.image || ""
                                    }

                                    Text {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 6
                                        text: modelData.label || ""
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        color: HomeTheme.btnSecondaryText
                                        font.pixelSize: 12
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            skinPanel.currentIndex = index
                                            homeController.setHeroSkin(
                                                        root.shownName, Number(modelData.index))
                                            root.reloadDetails()
                                            skinPanel.visible = false
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: filterPanel
        visible: false
        anchors.fill: parent
        z: 80
        onVisibleChanged: {
            if (visible) Qt.callLater(function() { nicknameField.forceActiveFocus() })
            else if (root.visible) (root.compact ? compactBar.filterButton : filterBtn).forceActiveFocus()
        }

        // Modal scrim: the catalog behind must not show through the panel.
        Rectangle {
            anchors.fill: parent
            color: HomeTheme.modalScrim

            MouseArea {
                anchors.fill: parent
                onClicked: filterPanel.visible = false
            }
        }

        BASlantedPanel {
            anchors.centerIn: parent
            width: Math.min(parent.width - 16, 920)
            // Compact hugs its form instead of leaving an empty tinted slab below it.
            height: Math.min(parent.height - 16, root.compact
                ? filterColumn.height + HomeTheme.compactTouch + HomeTheme.compactMargin * 4 : 640)
            slant: -0.04
            cornerRadius: 12
            shadowBlur: 0
            shadowOffset: 0
            topColor: HomeTheme.cardPanelTop
            bottomColor: HomeTheme.cardPanelBottom
            borderColor: HomeTheme.cardPanelBorder
            transformOrigin: Item.Center
            scale: root.uiScale

            MouseArea { anchors.fill: parent }

            Flickable {
                anchors.fill: parent
                anchors.margins: root.compact ? HomeTheme.compactMargin : 28
                anchors.topMargin: root.compact ? HomeTheme.compactTouch + HomeTheme.compactMargin * 2 : 28
                contentWidth: width
                contentHeight: filterColumn.height
                clip: true
                ScrollBar.vertical: HomeScrollBar { }

                Column {
                    id: filterColumn
                    width: parent.width
                    spacing: 14

                    Text {
                        visible: !root.compact
                        text: root.ui("GeneralOverview", "Search...")
                        color: HomeTheme.btnSecondaryText
                        font.pixelSize: 24
                        font.bold: true
                    }

                    ThemeCheckBox {
                        id: hiddenBox
                        text: root.ui("GeneralSearch", "Include hidden generals")
                        checked: root.includeHidden
                        onToggled: root.includeHidden = checked
                    }

                    Row {
                        spacing: 12
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 90
                            text: root.ui("GeneralSearch", "Nickname")
                            color: HomeTheme.btnSecondaryText
                        }
                        ThemeField {
                            id: nicknameField
                            width: Math.max(80, root.compact ? filterColumn.width - 102
                                                             : Math.min(280, filterColumn.width - 102))
                            placeholderText: "?, *"
                            text: root.nicknameFilter
                            onTextChanged: root.nicknameFilter = text
                        }
                    }

                    Text {
                        text: root.ui("GeneralSearch", "Gender")
                        color: HomeTheme.btnSecondaryText
                        font.bold: true
                    }

                    Flow {
                        width: parent.width
                        spacing: 16
                        Repeater {
                            model: [
                                { key: "male", label: root.ui("GeneralSearch", "Male") },
                                { key: "female", label: root.ui("GeneralSearch", "Female") },
                                { key: "neuter", label: root.ui("GeneralSearch", "Neuter") },
                                { key: "sexless", label: root.ui("GeneralSearch", "Sexless") },
                                { key: "nogender", label: root.ui("GeneralSearch", "NoGender") }
                            ]
                            ThemeCheckBox {
                                required property var modelData
                                text: modelData.label
                                checked: root.genderFilter.indexOf(modelData.key) >= 0
                                onToggled: root.genderFilter = root.toggleIn(root.genderFilter, modelData.key)
                            }
                        }
                    }

                    GridLayout {
                        width: parent.width
                        columns: root.compact ? 2 : 4
                        rowSpacing: 12
                        columnSpacing: 12
                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.fillWidth: root.compact
                            text: root.ui("GeneralSearch", "MaxHp Min")
                            color: HomeTheme.btnSecondaryText
                        }
                        SpinBox {
                            id: hpMinBox
                            from: 0
                            to: 10
                            value: root.hpMin
                            editable: true
                            palette.windowText: HomeTheme.btnSecondaryText
                            palette.text: HomeTheme.btnSecondaryText
                            palette.base: HomeTheme.btnSecondary
                            palette.button: HomeTheme.btnSecondary
                            palette.buttonText: HomeTheme.btnSecondaryText
                            palette.highlight: HomeTheme.baSky
                            onValueModified: root.hpMin = value
                        }
                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.fillWidth: root.compact
                            text: root.ui("GeneralSearch", "MaxHp Max")
                            color: HomeTheme.btnSecondaryText
                        }
                        SpinBox {
                            id: hpMaxBox
                            from: 0
                            to: 10
                            value: root.hpMax
                            editable: true
                            palette.windowText: HomeTheme.btnSecondaryText
                            palette.text: HomeTheme.btnSecondaryText
                            palette.base: HomeTheme.btnSecondary
                            palette.button: HomeTheme.btnSecondary
                            palette.buttonText: HomeTheme.btnSecondaryText
                            palette.highlight: HomeTheme.baSky
                            onValueModified: root.hpMax = value
                        }
                    }

                    Row {
                        spacing: 16
                        BAToolButton {
                            text: root.ui("GeneralSearch", "Clear")
                            onClicked: {
                                hiddenBox.checked = true
                                root.includeHidden = true
                                nicknameField.text = ""
                                root.nicknameFilter = ""
                                root.sameNameFilter = ""
                                root.genderFilter = []
                                hpMinBox.value = 0
                                hpMaxBox.value = 0
                                root.hpMin = 0
                                root.hpMax = 0
                            }
                        }
                        BAToolButton {
                            text: qsTranslate("GeneralOverview", "OK")
                            onClicked: {
                                filterPanel.visible = false
                                root.rebuild()
                            }
                        }
                    }
                }
            }
            // Compact header: the title names the pane-bar tab that opened it, beside its exit.
            Text {
                visible: root.compact
                anchors.left: parent.left
                anchors.leftMargin: HomeTheme.compactMargin + HomeTheme.compactGap
                anchors.verticalCenter: compactFilterBack.verticalCenter
                text: qsTr("篩選")
                color: HomeTheme.btnSecondaryText
                font.pixelSize: HomeTheme.cardSectionTitleFontSize
                font.bold: true
            }
            BAToolButton {
                id: compactFilterBack
                visible: root.compact
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: HomeTheme.compactMargin
                implicitHeight: HomeTheme.compactTouch
                text: qsTr("返回一覽")
                onClicked: {
                    filterPanel.visible = false
                    root.rebuild()
                    root.showCompactPane(0)
                }
            }
        }
    }
}

