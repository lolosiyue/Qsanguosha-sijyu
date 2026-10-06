import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "."

// The start-server page shares C++ ServerSetupSession with the legacy ServerDialog.
// Edits stay on this page; a start button commits them and leaves for the game.
Item {
    id: root
    objectName: "serverScene"

    property bool compact: Config.responsiveUiEnabled && (width < 900 || height > width)
    property int section: 0
    property var values: ({})
    property var modeEntries: []
    property var modeChoices: ({})
    property string modeKey: ""
    property bool miniCustom: false
    property var packageSections: []
    property var packageChecks: ({})
    // Hundreds of package checkboxes are added in short slices after the page opens, so neither
    // entering the page nor its package tab stalls. Packages only change after a restart.
    property int packageCount: 0
    property int packagesBuilt: 0
    readonly property bool packagesLoading: packagesBuilt < packageCount
    readonly property bool highContrast: homeController.visualMode === "highcontrast"
    readonly property var navigationEntry: sectionTabs.count > 0 ? sectionTabs.itemAt(0) : null
    readonly property var lastControl: serverButton
    signal navigationEndpointChanged()

    // The dialog's objectName for the checked mode: the chosen id for grouped modes, else the entry key.
    readonly property string modeName: {
        var entry = entryFor(modeKey)
        if (!entry)
            return ""
        return entry.kind === "group" ? (modeChoices[entry.key] || "") : entry.key
    }
    readonly property bool hegemonyAllowed: !/scenario|mini|1v1|1v3/.test(modeName)
    readonly property bool miniEditable: modeName.indexOf("mini") >= 0
                                         || ["02_1v1", "06_3v3", "06_XMode", "04_1v3"].indexOf(modeName) >= 0
    readonly property bool secondAllowed: !miniEditable
    readonly property bool secondEnabled: values.Enable2ndGeneral === true && secondAllowed
    readonly property bool hegemonyEnabled: values.EnableHegemony === true && hegemonyAllowed

    focus: true
    Keys.onEscapePressed: root.cancel()
    onNavigationEntryChanged: navigationEndpointChanged()
    onSectionChanged: formView.contentY = 0

    function takeKeyboard() {
        var tab = sectionTabs.itemAt(root.section)
        if (tab) tab.forceActiveFocus()
    }

    // Read Config again on every entry so the page matches the dialog and the last start.
    function reload() {
        var loaded = serverSetup.load()
        var entries = serverSetup.modes()
        var current = loaded.GameMode
        var choices = {}
        var key = ""
        for (var i = 0; i < entries.length; ++i) {
            var entry = entries[i]
            if (entry.options.length > 0) {
                var found = false
                for (var j = 0; j < entry.options.length; ++j) {
                    if (entry.options[j].value === current)
                        found = true
                }
                choices[entry.key] = found ? current : entry.options[0].value
                if (found && key === "")
                    key = entry.key
            } else if (entry.key === current && key === "") {
                key = entry.key
            }
        }
        if (key === "" && current === "custom_scenario")
            key = "mini"

        var sections = serverSetup.packageSections()
        var checks = {}
        var count = 0
        for (var s = 0; s < sections.length; ++s) {
            for (var p = 0; p < sections[s].packages.length; ++p)
                checks[sections[s].packages[p].name] = sections[s].packages[p].checked
            count += sections[s].packages.length
        }

        values = loaded
        modeEntries = entries
        modeChoices = choices
        modeKey = key
        miniCustom = false
        packageSections = sections
        packageChecks = checks
        packageCount = count
        section = 0
        formView.contentY = 0
        packageFeed.start()
    }
    // Adds one section header or checkbox at a time until the slice's time runs out.
    function buildPackages(deadline) {
        while (Date.now() < deadline) {
            var last = sectionRepeater.count > 0 ? sectionRepeater.itemAt(sectionRepeater.count - 1) : null
            if (last && last.rows.count < last.modelData.packages.length) {
                last.rows.append({ "packageIndex": last.rows.count })
                ++packagesBuilt
            } else if (sectionRows.count < packageSections.length) {
                sectionRows.append({ "sectionIndex": sectionRows.count })
            } else {
                packageFeed.stop()
                return
            }
        }
    }
    function cancel() {
        homeController.openHome()
    }
    function start(acceptType) {
        var next = Object.assign({}, root.values)
        next.EnableHegemony = root.hegemonyEnabled
        next.Enable2ndGeneral = root.secondEnabled
        next.ServerPort = parseInt(root.values.ServerPort) || 0
        var mode = resolvedMode()
        if (mode !== "")
            next.GameMode = mode
        var enabled = []
        var banned = []
        for (var s = 0; s < root.packageSections.length; ++s) {
            var packages = root.packageSections[s].packages
            for (var p = 0; p < packages.length; ++p)
                (root.packageChecks[packages[p].name] ? enabled : banned).push(packages[p].name)
        }
        next.EnabledPackages = enabled
        next.BanPackages = banned
        serverSetup.start(next, acceptType)
        homeController.openHome()
    }
    function set(key, value) {
        var next = Object.assign({}, root.values)
        next[key] = value
        root.values = next
    }
    function entryFor(key) {
        for (var i = 0; i < root.modeEntries.length; ++i) {
            if (root.modeEntries[i].key === key)
                return root.modeEntries[i]
        }
        return null
    }
    function hasMode(key) {
        return entryFor(key) !== null
    }
    function choose(entry, value) {
        var next = Object.assign({}, root.modeChoices)
        next[entry.key] = value
        root.modeChoices = next
        root.modeKey = entry.key
    }
    function resolvedMode() {
        var entry = entryFor(root.modeKey)
        if (!entry)
            return ""
        if (entry.kind === "mini" && root.miniCustom)
            return "custom_scenario"
        if (entry.options.length > 0)
            return root.modeChoices[entry.key] || ""
        return entry.key
    }
    function setPackages(packages, mode) {
        var next = Object.assign({}, root.packageChecks)
        for (var i = 0; i < packages.length; ++i) {
            if (!packages[i].enabled)
                continue
            next[packages[i].name] = mode === "all" ? true
                                   : mode === "none" ? false : !next[packages[i].name]
        }
        root.packageChecks = next
    }
    function checkedCount(packages) {
        var count = 0
        for (var i = 0; i < packages.length; ++i) {
            if (root.packageChecks[packages[i].name])
                ++count
        }
        return count
    }
    function editCustomMiniScene() {
        if (serverSetup.editCustomMiniScene())
            root.miniCustom = true
    }
    function detectAddress() {
        var address = serverSetup.detectAddress()
        if (address !== "")
            root.set("Address", address)
    }
    // Keyboard focus inside the scrolling form keeps the focused control in view.
    function reveal(item) {
        var y = item.mapToItem(formView.contentItem, 0, 0).y
        if (y < formView.contentY)
            formView.contentY = Math.max(0, y - HomeTheme.settingsRowGap)
        else if (y + item.height > formView.contentY + formView.height)
            formView.contentY = Math.min(Math.max(0, formView.contentHeight - formView.height),
                                         y + item.height - formView.height + HomeTheme.settingsRowGap)
    }

    component GroupTitle: Text {
        Layout.fillWidth: true
        Layout.topMargin: HomeTheme.settingsRowGap
        Accessible.role: Accessible.Heading
        color: HomeTheme.cardAccent
        font.pixelSize: HomeTheme.settingsGroupFontSize
        font.bold: true
        wrapMode: Text.Wrap
    }

    // Label and control share a row; compact stacks them.
    component SettingRow: GridLayout {
        Layout.fillWidth: true
        columns: root.compact ? 1 : 2
        columnSpacing: HomeTheme.cardPanelGap
        rowSpacing: HomeTheme.compactGap
    }

    component SettingLabel: Text {
        Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsLabelWidth
        Layout.fillWidth: root.compact
        color: enabled ? HomeTheme.cardTextSecondary : HomeTheme.cardControlDisabledText
        font.pixelSize: HomeTheme.settingsFontSize
        wrapMode: Text.Wrap
    }

    component FormCheck: Basic.CheckBox {
        id: check
        property string hint: ""
        Layout.fillWidth: true
        implicitHeight: root.compact ? HomeTheme.compactTouch
                                     : Math.max(implicitContentHeight, implicitIndicatorHeight) + topPadding + bottomPadding
        padding: 4
        spacing: 10
        onActiveFocusChanged: if (activeFocus) root.reveal(check)
        Keys.onShortcutOverride: function(event) {
            if (event.key === Qt.Key_Space)
                event.accepted = true
        }
        ToolTip.visible: hint !== "" && hovered
        ToolTip.text: hint
        ToolTip.delay: 500
        indicator: Rectangle {
            implicitWidth: HomeTheme.settingsCheckSize
            implicitHeight: HomeTheme.settingsCheckSize
            x: check.leftPadding
            y: parent ? (parent.height - height) / 2 : 0
            radius: HomeTheme.cardBadgeRadius
            color: check.checked ? HomeTheme.cardInteractive : HomeTheme.cardInputFill
            border.width: check.activeFocus
                          ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                               : HomeTheme.cardSelectedBorderWidth)
                          : HomeTheme.cardBorderWidth
            border.color: check.activeFocus ? HomeTheme.focusBorderHigh
                        : check.checked ? HomeTheme.cardInteractive : HomeTheme.cardTileBorder
            opacity: check.enabled ? 1.0 : HomeTheme.cardControlDisabledOpacity
            Text {
                anchors.centerIn: parent
                visible: check.checked
                text: "✓"
                color: HomeTheme.cardTagMark
                font.pixelSize: HomeTheme.cardBodyFontSize
                font.bold: true
            }
        }
        contentItem: Text {
            leftPadding: check.indicator.width + check.spacing
            text: check.text
            color: check.enabled ? HomeTheme.cardTextPrimary : HomeTheme.cardControlDisabledText
            font.pixelSize: HomeTheme.settingsFontSize
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    component SettingCheck: FormCheck {
        required property string key
        checked: root.values[key] === true
        onToggled: root.set(key, checked)
    }

    component ModeRadio: Basic.RadioButton {
        id: radio
        autoExclusive: false
        padding: 4
        spacing: 10
        implicitHeight: root.compact ? HomeTheme.compactTouch
                                     : Math.max(implicitContentHeight, implicitIndicatorHeight) + topPadding + bottomPadding
        onActiveFocusChanged: if (activeFocus) root.reveal(radio)
        Keys.onShortcutOverride: function(event) {
            if (event.key === Qt.Key_Space)
                event.accepted = true
        }
        indicator: Rectangle {
            implicitWidth: HomeTheme.settingsCheckSize
            implicitHeight: HomeTheme.settingsCheckSize
            x: radio.leftPadding
            y: parent ? (parent.height - height) / 2 : 0
            radius: width / 2
            color: HomeTheme.cardInputFill
            border.width: radio.activeFocus
                          ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                               : HomeTheme.cardSelectedBorderWidth)
                          : HomeTheme.cardBorderWidth
            border.color: radio.activeFocus ? HomeTheme.focusBorderHigh
                        : radio.checked ? HomeTheme.cardInteractive : HomeTheme.cardTileBorder
            opacity: radio.enabled ? 1.0 : HomeTheme.cardControlDisabledOpacity
            Rectangle {
                anchors.centerIn: parent
                width: parent.width / 2
                height: width
                radius: width / 2
                visible: radio.checked
                color: HomeTheme.cardInteractive
            }
        }
        contentItem: Text {
            leftPadding: radio.indicator.width + radio.spacing
            text: radio.text
            color: radio.enabled ? HomeTheme.cardTextPrimary : HomeTheme.cardControlDisabledText
            font.pixelSize: HomeTheme.settingsFontSize
            font.bold: radio.checked
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    component SettingChoice: CardComboBox {
        id: choice
        required property string key
        required property var options
        Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsChoiceWidth
        Layout.fillWidth: root.compact
        implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight
        font.pixelSize: HomeTheme.settingsFontSize
        textRole: "label"
        valueRole: "value"
        model: options
        currentIndex: {
            var current = root.values[key]
            for (var i = 0; i < options.length; ++i) {
                if (options[i].value === current)
                    return i
            }
            return 0
        }
        onActivated: root.set(key, currentValue)
        onActiveFocusChanged: if (activeFocus) root.reveal(choice)
    }

    component SettingSpin: RowLayout {
        id: spinRow
        required property string key
        property int from: 0
        property int to: 100
        property int stepSize: 1
        property string suffix: ""
        property string accessibleName: ""
        spacing: HomeTheme.compactGap

        Basic.SpinBox {
            id: spin
            from: spinRow.from
            to: spinRow.to
            stepSize: spinRow.stepSize
            editable: true
            implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight
            value: Number(root.values[spinRow.key]) || 0
            textFromValue: function(value) { return String(value) }
            valueFromText: function(text) { return parseInt(text) || 0 }
            validator: IntValidator { bottom: spin.from; top: spin.to }
            onValueModified: root.set(spinRow.key, value)
            onActiveFocusChanged: if (activeFocus) root.reveal(spinRow)
            Accessible.name: spinRow.accessibleName
            font.pixelSize: HomeTheme.settingsFontSize
            palette.text: HomeTheme.cardTextPrimary
            palette.base: HomeTheme.cardInputFill
            palette.button: HomeTheme.cardTileFill
            palette.buttonText: HomeTheme.cardTextPrimary
            palette.mid: HomeTheme.cardPanelBorder
            palette.highlight: HomeTheme.cardInteractive
        }
        Text {
            visible: spinRow.suffix !== ""
            text: spinRow.suffix
            color: HomeTheme.cardTextSecondary
            font.pixelSize: HomeTheme.settingsFontSize
        }
    }

    component SettingField: Basic.TextField {
        id: field
        required property string key
        Layout.fillWidth: true
        implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight
        text: root.values[key] !== undefined ? String(root.values[key]) : ""
        onTextEdited: root.set(key, text)
        onActiveFocusChanged: if (activeFocus) root.reveal(field)
        color: HomeTheme.cardTextPrimary
        placeholderTextColor: HomeTheme.cardTextSecondary
        font.pixelSize: HomeTheme.settingsFontSize
        verticalAlignment: Text.AlignVCenter
        leftPadding: HomeTheme.cardControlHPadding
        rightPadding: HomeTheme.cardControlHPadding
        background: Rectangle {
            radius: HomeTheme.cardControlRadius
            color: HomeTheme.cardInputFill
            border.width: field.activeFocus ? HomeTheme.cardSelectedBorderWidth : HomeTheme.cardBorderWidth
            border.color: field.activeFocus ? HomeTheme.focusBorderHigh : HomeTheme.cardPanelBorder
        }
    }

    component FormButton: BAToolButton {
        id: formButton
        implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight + 4
        Layout.fillWidth: root.compact
        onActiveFocusChanged: if (activeFocus) root.reveal(formButton)
    }

    // One tooltip serves every package checkbox; package contents can be long, so they wrap.
    Basic.ToolTip {
        id: packageTip
        property Item check: null
        parent: check
        visible: check !== null && check.hovered && text !== ""
        delay: 500
        width: Math.min(implicitWidth, 560)
        text: check ? serverSetup.packageTooltip(check.modelData.name) : ""
    }

    ListModel { id: sectionRows }
    Timer {
        id: packageFeed
        interval: 16
        repeat: true
        onTriggered: root.buildPackages(Date.now() + 12)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 0 : HomeTheme.cardPageHMargin
        anchors.rightMargin: root.compact ? 0 : HomeTheme.cardPageHMargin
        anchors.topMargin: root.compact ? 0 : HomeTheme.cardPageTopMargin
        anchors.bottomMargin: HomeTheme.cardPageBottomMargin
        spacing: root.compact ? HomeTheme.compactGap : HomeTheme.cardPanelGap

        BASlantedPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? titleColumn.implicitHeight + HomeTheme.compactMargin * 2
                                                 : HomeTheme.cardHeaderHeight
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

            ColumnLayout {
                id: titleColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: root.compact ? HomeTheme.compactMargin : HomeTheme.cardHeaderPadding
                anchors.rightMargin: root.compact ? HomeTheme.compactMargin : HomeTheme.cardHeaderPadding
                spacing: HomeTheme.cardHeaderTitleGap
                Text {
                    Layout.fillWidth: true
                    text: qsTranslate("ServerDialog", "Start server")
                    color: HomeTheme.cardTextPrimary
                    font.pixelSize: root.compact ? HomeTheme.cardSectionTitleFontSize : HomeTheme.cardTitleFontSize
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: {
                        var entry = root.entryFor(root.modeKey)
                        if (!entry)
                            return ""
                        if (entry.kind === "mini" && root.miniCustom)
                            return entry.label + " · " + qsTranslate("ServerDialog", "Custom Mini Scene")
                        var value = root.modeChoices[entry.key]
                        for (var i = 0; i < entry.options.length; ++i) {
                            if (entry.options[i].value === value)
                                return entry.label + " · " + entry.options[i].label
                        }
                        return entry.label
                    }
                    color: HomeTheme.cardTextSecondary
                    font.pixelSize: HomeTheme.cardCaptionFontSize
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: root.compact ? 1 : 2
            columnSpacing: HomeTheme.cardPanelGap
            rowSpacing: HomeTheme.compactGap

            // Tabs are vertical when wide and move above the content when narrow.
            GridLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: root.compact
                Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsNavWidth
                columns: root.compact ? 2 : 1
                columnSpacing: HomeTheme.compactGap
                rowSpacing: HomeTheme.compactGap

                Repeater {
                    id: sectionTabs
                    model: [qsTranslate("ServerDialog", "Basic"),
                            qsTranslate("ServerDialog", "Game Pacakge Selection"),
                            qsTranslate("ServerDialog", "Advanced"),
                            qsTranslate("ServerDialog", "Miscellaneous")]
                    BAToolButton {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        implicitWidth: 0
                        implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardActionButtonExtent
                        text: modelData
                        opacity: root.section === index ? 1 : 0.65
                        Accessible.role: Accessible.PageTab
                        Accessible.checked: root.section === index
                        onClicked: root.section = index
                    }
                }
            }

            BASlantedPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                slant: 0
                cornerRadius: HomeTheme.cardPanelRadius
                shadowBlur: 0
                shadowOffset: 0
                topColor: HomeTheme.cardPanelTop
                bottomColor: HomeTheme.cardPanelBottom
                borderColor: HomeTheme.cardPanelBorder
                shadowColor: HomeTheme.baDockShadow

                Flickable {
                    id: formView
                    anchors.fill: parent
                    anchors.margins: root.compact ? HomeTheme.compactMargin : HomeTheme.cardPanelContentPadding
                    contentWidth: width
                    contentHeight: form.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: HomeScrollBar { }

                    ColumnLayout {
                        id: form
                        width: formView.width - HomeTheme.cardGridGap
                        spacing: HomeTheme.settingsRowGap

                        // Basic
                        ColumnLayout {
                            visible: root.section === 0
                            Layout.fillWidth: true
                            spacing: HomeTheme.settingsRowGap

                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Server name") }
                                SettingField {
                                    key: "ServerName"
                                    Accessible.name: qsTranslate("ServerDialog", "Server name")
                                }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Operation timeout") }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: root.compact ? 2 : 3
                                    columnSpacing: HomeTheme.cardPanelGap
                                    rowSpacing: HomeTheme.compactGap
                                    SettingSpin {
                                        key: "OperationTimeout"
                                        from: 5
                                        to: 60
                                        suffix: qsTranslate("ServerDialog", " seconds").trim()
                                        accessibleName: qsTranslate("ServerDialog", "Operation timeout")
                                        enabled: root.values.OperationNoLimit !== true
                                    }
                                    SettingCheck {
                                        key: "OperationNoLimit"
                                        text: qsTranslate("ServerDialog", "No limit")
                                        Layout.fillWidth: false
                                    }
                                    FormButton {
                                        Layout.columnSpan: root.compact ? 2 : 1
                                        text: qsTranslate("ServerDialog", "Banlist ...")
                                        onClicked: Qt.callLater(function() { serverSetup.editBanlist() })
                                    }
                                }
                            }

                            GroupTitle { text: qsTranslate("ServerDialog", "Game mode") }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: root.compact ? 1 : 2
                                columnSpacing: HomeTheme.cardPanelGap
                                rowSpacing: HomeTheme.compactGap

                                Repeater {
                                    model: root.modeEntries
                                    GridLayout {
                                        id: modeRow
                                        required property var modelData
                                        readonly property bool hasOptions: modelData.options.length > 0
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        Layout.alignment: Qt.AlignTop
                                        // Portrait keeps the radio beside its choice; a custom button wraps below.
                                        columns: root.compact ? 2 : 3
                                        columnSpacing: HomeTheme.compactGap
                                        rowSpacing: HomeTheme.compactGap

                                        ModeRadio {
                                            Layout.fillWidth: !modeRow.hasOptions
                                            Layout.preferredWidth: root.compact || !modeRow.hasOptions ? -1 : HomeTheme.settingsLabelWidth * 0.6
                                            text: modeRow.modelData.label
                                            checked: root.modeKey === modeRow.modelData.key
                                            onClicked: root.modeKey = modeRow.modelData.key
                                        }
                                        CardComboBox {
                                            id: modeChoice
                                            visible: modeRow.hasOptions
                                            Layout.fillWidth: true
                                            implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight
                                            font.pixelSize: HomeTheme.settingsFontSize
                                            accessibleLabel: modeRow.modelData.label
                                            enabled: !(modeRow.modelData.kind === "mini" && root.miniCustom)
                                            textRole: "label"
                                            valueRole: "value"
                                            model: modeRow.modelData.options
                                            currentIndex: {
                                                var current = root.modeChoices[modeRow.modelData.key]
                                                var options = modeRow.modelData.options
                                                for (var i = 0; i < options.length; ++i) {
                                                    if (options[i].value === current)
                                                        return i
                                                }
                                                return 0
                                            }
                                            onActivated: root.choose(modeRow.modelData, currentValue)
                                            onActiveFocusChanged: if (activeFocus) root.reveal(modeChoice)
                                        }
                                        FormButton {
                                            visible: modeRow.modelData.key === "04_boss"
                                            enabled: root.modeName === "04_boss"
                                            text: qsTr("Custom Boss Mode")
                                            onClicked: Qt.callLater(function() { serverSetup.editBossMode() })
                                        }
                                        FormButton {
                                            Layout.columnSpan: root.compact ? 2 : 1
                                            visible: modeRow.modelData.kind === "mini"
                                            enabled: root.miniEditable
                                            text: qsTranslate("ServerDialog", "Custom Mini Scene")
                                            onClicked: Qt.callLater(root.editCustomMiniScene)
                                        }
                                    }
                                }
                            }

                            GroupTitle {
                                visible: root.hasMode("02_1v1")
                                enabled: root.modeName === "02_1v1"
                                opacity: enabled ? 1 : HomeTheme.cardControlDisabledOpacity
                                text: qsTranslate("ServerDialog", "1v1 options")
                            }
                            ColumnLayout {
                                visible: root.hasMode("02_1v1")
                                enabled: root.modeName === "02_1v1"
                                Layout.fillWidth: true
                                spacing: HomeTheme.settingsRowGap
                                SettingRow {
                                    SettingLabel { text: qsTranslate("ServerDialog", "Rule option") }
                                    SettingChoice {
                                        key: "1v1/Rule"
                                        accessibleLabel: qsTranslate("ServerDialog", "Rule option")
                                        options: [
                                            { "value": "Classical", "label": qsTranslate("ServerDialog", "Classical") },
                                            { "value": "2013", "label": "2013" },
                                            { "value": "WZZZ", "label": qsTranslate("ServerDialog", "WZZZ") }
                                        ]
                                    }
                                }
                                SettingRow {
                                    SettingLabel { text: qsTranslate("ServerDialog", "Extension setting") }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: HomeTheme.cardPanelGap
                                        SettingCheck { key: "1v1/UsingExtension"; text: qsTranslate("ServerDialog", "General extensions") }
                                        SettingCheck { key: "1v1/UsingCardExtension"; text: qsTranslate("ServerDialog", "Card extensions") }
                                    }
                                }
                            }

                            GroupTitle {
                                visible: root.hasMode("06_3v3")
                                enabled: root.modeName === "06_3v3"
                                opacity: enabled ? 1 : HomeTheme.cardControlDisabledOpacity
                                text: qsTranslate("ServerDialog", "3v3 options")
                            }
                            ColumnLayout {
                                visible: root.hasMode("06_3v3")
                                enabled: root.modeName === "06_3v3"
                                Layout.fillWidth: true
                                spacing: HomeTheme.settingsRowGap
                                SettingRow {
                                    ModeRadio {
                                        Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsLabelWidth
                                        text: qsTranslate("ServerDialog", "Official mode")
                                        checked: root.values["3v3/UsingExtension"] !== true
                                        onClicked: root.set("3v3/UsingExtension", false)
                                    }
                                    SettingChoice {
                                        key: "3v3/OfficialRule"
                                        accessibleLabel: qsTranslate("ServerDialog", "Official mode")
                                        options: [
                                            { "value": "Classical", "label": qsTranslate("ServerDialog", "Classical") },
                                            { "value": "2012", "label": "2012" },
                                            { "value": "2013", "label": "2013" }
                                        ]
                                    }
                                }
                                SettingRow {
                                    ModeRadio {
                                        Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsLabelWidth
                                        text: qsTranslate("ServerDialog", "Extension mode")
                                        checked: root.values["3v3/UsingExtension"] === true
                                        onClicked: root.set("3v3/UsingExtension", true)
                                    }
                                    FormButton {
                                        Layout.alignment: Qt.AlignLeft
                                        enabled: root.values["3v3/UsingExtension"] === true
                                        text: qsTranslate("ServerDialog", "General selection ...")
                                        onClicked: Qt.callLater(function() { serverSetup.select3v3Generals() })
                                    }
                                }
                                SettingCheck { key: "3v3/ExcludeDisasters"; text: qsTranslate("ServerDialog", "Exclude disasters") }
                                SettingRow {
                                    SettingLabel { text: qsTranslate("ServerDialog", "Role choose") }
                                    SettingChoice {
                                        key: "3v3/RoleChoose"
                                        accessibleLabel: qsTranslate("ServerDialog", "Role choose")
                                        options: [
                                            { "value": "Normal", "label": qsTranslate("ServerDialog", "Normal") },
                                            { "value": "Random", "label": qsTranslate("ServerDialog", "Random") },
                                            { "value": "AllRoles", "label": qsTranslate("ServerDialog", "All roles") }
                                        ]
                                    }
                                }
                            }

                            GroupTitle {
                                visible: root.hasMode("06_XMode")
                                enabled: root.modeName === "06_XMode"
                                opacity: enabled ? 1 : HomeTheme.cardControlDisabledOpacity
                                text: qsTranslate("ServerDialog", "XMode options")
                            }
                            SettingRow {
                                visible: root.hasMode("06_XMode")
                                enabled: root.modeName === "06_XMode"
                                SettingLabel { text: qsTranslate("ServerDialog", "Role choose") }
                                SettingChoice {
                                    key: "XMode/RoleChooseX"
                                    accessibleLabel: qsTranslate("ServerDialog", "Role choose")
                                    options: [
                                        { "value": "Normal", "label": qsTranslate("ServerDialog", "Normal") },
                                        { "value": "Random", "label": qsTranslate("ServerDialog", "Random") },
                                        { "value": "AllRoles", "label": qsTranslate("ServerDialog", "All roles") }
                                    ]
                                }
                            }
                        }

                        // Packages
                        ColumnLayout {
                            visible: root.section === 1
                            Layout.fillWidth: true
                            spacing: HomeTheme.settingsRowGap

                            Flow {
                                Layout.fillWidth: true
                                spacing: HomeTheme.cardPanelGap
                                SettingCheck {
                                    key: "DisableLua"
                                    text: qsTranslate("ServerDialog", "Disable Lua")
                                    hint: qsTranslate("ServerDialog", "The setting takes effect after reboot")
                                }
                                SettingCheck {
                                    key: "AddGodGeneral"
                                    text: qsTr("Add god generals")
                                    hint: qsTr("When off, generals of the god kingdom never appear in the game")
                                }
                                SettingCheck {
                                    key: "GeneralVersionDedup"
                                    text: qsTr("Keep only the newest version of same-name generals")
                                    hint: qsTr("3rd edition > 2nd edition > Strategic Assault > Revamped·OL > 10th Anniversary > New > Mobile > OL > Wings > Nostalgia > Base")
                                }
                            }

                            // Progress, not a looping animation: see SkeletonBlock.qml.
                            ColumnLayout {
                                visible: root.packagesLoading
                                Layout.fillWidth: true
                                spacing: HomeTheme.compactGap
                                Text {
                                    text: qsTranslate("HomeCompactShell", "Now loading...") + "  "
                                          + root.packagesBuilt + "/" + root.packageCount
                                    color: HomeTheme.cardTextSecondary
                                    font.pixelSize: HomeTheme.settingsFontSize
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 4
                                    radius: 2
                                    color: HomeTheme.cardInputFill
                                    Rectangle {
                                        width: parent.width * root.packagesBuilt / Math.max(1, root.packageCount)
                                        height: parent.height
                                        radius: parent.radius
                                        color: HomeTheme.cardInteractive
                                    }
                                }
                            }

                            Repeater {
                                id: sectionRepeater
                                model: sectionRows
                                ColumnLayout {
                                    id: packageSection
                                    required property int sectionIndex
                                    readonly property var modelData: root.packageSections[sectionIndex]
                                    property alias rows: packageRows
                                    property bool expanded: true
                                    Layout.fillWidth: true
                                    spacing: HomeTheme.compactGap

                                    ListModel { id: packageRows }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        Layout.topMargin: HomeTheme.settingsRowGap
                                        columns: root.compact ? 3 : 4
                                        columnSpacing: HomeTheme.compactGap
                                        rowSpacing: HomeTheme.compactGap

                                        BAToolButton {
                                            Layout.columnSpan: root.compact ? 3 : 1
                                            Layout.fillWidth: true
                                            implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight + 4
                                            text: (packageSection.expanded ? "▾ " : "▸ ") + packageSection.modelData.title + "  "
                                                  + root.checkedCount(packageSection.modelData.packages) + "/"
                                                  + packageSection.modelData.packages.length
                                            Accessible.role: Accessible.Button
                                            Accessible.checked: packageSection.expanded
                                            onClicked: packageSection.expanded = !packageSection.expanded
                                        }
                                        FormButton {
                                            text: qsTranslate("CollapsibleSection", "Select All")
                                            onClicked: root.setPackages(packageSection.modelData.packages, "all")
                                        }
                                        FormButton {
                                            text: qsTranslate("CollapsibleSection", "Select None")
                                            onClicked: root.setPackages(packageSection.modelData.packages, "none")
                                        }
                                        FormButton {
                                            text: qsTranslate("CollapsibleSection", "Reverse Select")
                                            onClicked: root.setPackages(packageSection.modelData.packages, "reverse")
                                        }
                                    }

                                    GridLayout {
                                        visible: packageSection.expanded
                                        Layout.fillWidth: true
                                        columns: root.compact ? 2 : 4
                                        columnSpacing: HomeTheme.cardPanelGap
                                        rowSpacing: 0

                                        Repeater {
                                            model: packageSection.expanded ? packageRows : null
                                            FormCheck {
                                                id: packageCheck
                                                required property int packageIndex
                                                readonly property var modelData: packageSection.modelData.packages[packageIndex]
                                                Layout.preferredWidth: 1
                                                text: modelData.label
                                                enabled: modelData.enabled
                                                checked: root.packageChecks[modelData.name] === true
                                                onToggled: {
                                                    var next = Object.assign({}, root.packageChecks)
                                                    next[modelData.name] = checked
                                                    root.packageChecks = next
                                                }
                                                onHoveredChanged: if (hovered) packageTip.check = packageCheck
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Advanced
                        ColumnLayout {
                            visible: root.section === 2
                            Layout.fillWidth: true
                            spacing: HomeTheme.settingsRowGap

                            SettingCheck { key: "ForbidSIMC"; text: qsTranslate("ServerDialog", "Forbid same IP with multiple connection") }
                            SettingCheck { key: "DisableChat"; text: qsTranslate("ServerDialog", "Disable chat") }
                            SettingCheck { key: "RandomSeat"; text: qsTranslate("ServerDialog", "Arrange the seats randomly") }
                            SettingCheck { key: "WithoutLordskill"; text: qsTranslate("ServerDialog", "Without Lordskill") }
                            SettingCheck { key: "EnableSPConvert"; text: qsTranslate("ServerDialog", "Enable SP Convert") }
                            SettingRow {
                                SettingLabel {
                                    text: qsTranslate("ServerDialog", "Upperlimit for lord")
                                    ToolTip.visible: lordHint.hovered
                                    ToolTip.text: qsTranslate("ServerDialog", "-1 means that all lords are available")
                                    HoverHandler { id: lordHint }
                                }
                                SettingSpin { key: "LordMaxChoice"; from: -1; to: 15; accessibleName: qsTranslate("ServerDialog", "Upperlimit for lord") }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Upperlimit for non-lord") }
                                SettingSpin { key: "NonLordMaxChoice"; from: 0; to: 15; accessibleName: qsTranslate("ServerDialog", "Upperlimit for non-lord") }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Upperlimit for general") }
                                SettingSpin { key: "MaxChoice"; from: 3; to: 21; accessibleName: qsTranslate("ServerDialog", "Upperlimit for general") }
                            }
                            SettingRow {
                                SettingLabel {
                                    text: qsTranslate("ServerDialog", "Pile-swapping limitation")
                                    ToolTip.visible: pileHint.hovered
                                    ToolTip.text: qsTranslate("ServerDialog", "-1 means no limitations")
                                    HoverHandler { id: pileHint }
                                }
                                SettingSpin { key: "PileSwappingLimitation"; from: -1; to: 15; accessibleName: qsTranslate("ServerDialog", "Pile-swapping limitation") }
                            }

                            SettingCheck {
                                key: "EnableCheat"
                                text: qsTranslate("ServerDialog", "Enable cheat")
                                hint: qsTranslate("ServerDialog", "This option enables the cheat menu")
                            }
                            SettingCheck {
                                visible: root.values.EnableCheat === true
                                key: "FreeChoose"
                                text: qsTranslate("ServerDialog", "Choose generals and cards freely")
                            }
                            SettingCheck {
                                visible: root.values.EnableCheat === true
                                key: "FreeAssign"
                                text: qsTranslate("ServerDialog", "Assign role and seat freely")
                            }
                            SettingCheck {
                                visible: root.values.EnableCheat === true
                                enabled: root.values.FreeAssign === true
                                key: "FreeAssignSelf"
                                text: qsTranslate("ServerDialog", "Assign only your own role")
                            }

                            FormCheck {
                                text: qsTranslate("ServerDialog", "Enable second general")
                                enabled: root.secondAllowed
                                checked: root.secondEnabled
                                onToggled: root.set("Enable2ndGeneral", checked)
                            }
                            SettingRow {
                                visible: root.secondEnabled
                                SettingLabel { text: qsTranslate("ServerDialog", "Max HP scheme") }
                                SettingChoice {
                                    key: "MaxHpScheme"
                                    accessibleLabel: qsTranslate("ServerDialog", "Max HP scheme")
                                    options: [
                                        { "value": 0, "label": qsTranslate("ServerDialog", "Sum - X") },
                                        { "value": 1, "label": qsTranslate("ServerDialog", "Minimum") },
                                        { "value": 2, "label": qsTranslate("ServerDialog", "Maximum") },
                                        { "value": 3, "label": qsTranslate("ServerDialog", "Average") }
                                    ]
                                }
                            }
                            SettingRow {
                                visible: root.secondEnabled && Number(root.values.MaxHpScheme) === 0
                                SettingLabel { text: qsTranslate("ServerDialog", "Subtraction for scheme 0") }
                                SettingSpin { key: "Scheme0Subtraction"; from: -5; to: 12; accessibleName: qsTranslate("ServerDialog", "Subtraction for scheme 0") }
                            }
                            SettingCheck {
                                visible: root.secondEnabled && Number(root.values.MaxHpScheme) !== 0
                                key: "PreventAwakenBelow3"
                                text: qsTranslate("ServerDialog", "Prevent maxhp being less than 3 for awaken skills")
                            }

                            FormCheck {
                                text: qsTranslate("ServerDialog", "Enable Hegemony")
                                enabled: root.hegemonyAllowed
                                checked: root.hegemonyEnabled
                                onToggled: root.set("EnableHegemony", checked)
                            }
                            SettingCheck { key: "EnableMeleeMode"; text: qsTranslate("ServerDialog", "Enable Melee Mode (Peach as Slash/Jink in late game)") }
                            SettingRow {
                                visible: root.hegemonyEnabled
                                SettingLabel { text: qsTranslate("ServerDialog", "Upperlimit for hegemony") }
                                SettingSpin { key: "HegemonyMaxChoice"; from: 5; to: 21; accessibleName: qsTranslate("ServerDialog", "Upperlimit for hegemony") }
                            }
                            SettingCheck {
                                visible: root.hegemonyEnabled
                                key: "RewardTheFirstShowingPlayer"
                                text: qsTranslate("ServerDialog", "Reward the first showing player")
                            }

                            GroupTitle { text: qsTranslate("ServerDialog", "Address") }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Address") }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: root.compact ? 1 : 2
                                    columnSpacing: HomeTheme.compactGap
                                    rowSpacing: HomeTheme.compactGap
                                    SettingField {
                                        key: "Address"
                                        placeholderText: qsTranslate("ServerDialog", "Public IP or domain")
                                        Accessible.name: qsTranslate("ServerDialog", "Address")
                                    }
                                    FormButton {
                                        text: qsTranslate("ServerDialog", "Detect my WAN IP")
                                        onClicked: root.detectAddress()
                                    }
                                }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Port") }
                                SettingField {
                                    key: "ServerPort"
                                    validator: IntValidator { bottom: 1000; top: 65535 }
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    Accessible.name: qsTranslate("ServerDialog", "Port")
                                }
                            }
                            SettingCheck { key: "serverconfig/upnp"; text: qsTr("Enable UPnP port mapping") }
                            SettingCheck {
                                key: "serverconfig/addtolistserver"
                                text: qsTr("Add to the server list")
                                hint: qsTr("Lets others find this server with \"Find servers\". Only servers reachable from the internet are listed.")
                            }
                        }

                        // Miscellaneous
                        ColumnLayout {
                            visible: root.section === 3
                            Layout.fillWidth: true
                            spacing: HomeTheme.settingsRowGap

                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Game start count down") }
                                SettingSpin {
                                    key: "CountDownSeconds"
                                    from: 0
                                    to: 10
                                    suffix: qsTranslate("ServerDialog", " seconds").trim()
                                    accessibleName: qsTranslate("ServerDialog", "Game start count down")
                                }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Nullification count down") }
                                SettingSpin {
                                    key: "NullificationCountDown"
                                    from: 5
                                    to: 15
                                    suffix: qsTranslate("ServerDialog", " seconds").trim()
                                    accessibleName: qsTranslate("ServerDialog", "Nullification count down")
                                }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ServerDialog", "Enable the luck card") }
                                SettingSpin {
                                    key: "LuckCardTimes"
                                    from: -1
                                    to: 10
                                    suffix: qsTr("times")
                                    accessibleName: qsTranslate("ServerDialog", "Enable the luck card")
                                }
                            }
                            SettingCheck { key: "EnableMinimizeDialog"; text: qsTranslate("ServerDialog", "Minimize the dialog when server runs") }
                            SettingCheck { key: "SurrenderAtDeath"; text: qsTranslate("ServerDialog", "Surrender at the time of Death") }

                            GroupTitle { text: qsTranslate("ServerDialog", "Artificial intelligence") }
                            SettingCheck { key: "EnableAI"; text: qsTranslate("ServerDialog", "Enable AI") }
                            SettingCheck {
                                key: "AIHumanized"
                                enabled: root.values.EnableAI === true
                                text: qsTr("Humanized AI")
                                hint: qsTr("AI chats, occasionally makes small mistakes, and waits a moment before acting")
                            }
                            SettingCheck {
                                key: "JevHybrid50P"
                                enabled: root.values.EnableAI === true && root.modeName === "50p"
                                text: qsTranslate("ServerDialog", "JEV hybrid for 50-player AI seats (paid, max $0.10 per game)")
                                hint: qsTranslate("ServerDialog", "Uses JEV only for supported robot decisions. SmartAI handles all other decisions and any provider or budget failure.")
                            }
                            SettingRow {
                                enabled: root.values.EnableAI === true
                                SettingLabel { text: qsTranslate("ServerDialog", "AI delay") }
                                SettingSpin {
                                    key: "OriginAIDelay"
                                    from: 0
                                    to: 5000
                                    stepSize: 100
                                    suffix: qsTranslate("ServerDialog", " millisecond").trim()
                                    accessibleName: qsTranslate("ServerDialog", "AI delay")
                                }
                            }
                            SettingCheck {
                                key: "AlterAIDelayAD"
                                enabled: root.values.EnableAI === true
                                text: qsTranslate("ServerDialog", "Alter AI Delay After Death")
                            }
                            SettingRow {
                                enabled: root.values.AlterAIDelayAD === true
                                SettingLabel { text: qsTranslate("ServerDialog", "AI delay After Death") }
                                SettingSpin {
                                    key: "AIDelayAD"
                                    from: 0
                                    to: 5000
                                    stepSize: 100
                                    suffix: qsTranslate("ServerDialog", " millisecond").trim()
                                    accessibleName: qsTranslate("ServerDialog", "AI delay After Death")
                                }
                            }
                        }
                    }
                }
            }
        }

        // Portrait keeps all three actions on one row as icon-over-text tiles, like the home actions.
        RowLayout {
            Layout.fillWidth: true
            spacing: root.compact ? HomeTheme.compactGap : HomeTheme.cardPanelGap

            Item { Layout.fillWidth: true; visible: !root.compact }

            HomeMainButton {
                Layout.fillWidth: root.compact
                Layout.preferredWidth: root.compact ? 1 : HomeTheme.settingsFooterButtonWidth
                implicitHeight: root.compact ? HomeTheme.compactActionTileHeight : HomeTheme.settingsFooterButtonHeight
                compact: true
                tile: root.compact
                text: qsTranslate("ServerDialog", "Cancel")
                iconSource: "qrc:/QSanguosha/Home/icons/home.svg"
                onClicked: root.cancel()
            }

            HomeMainButton {
                Layout.fillWidth: root.compact
                Layout.preferredWidth: root.compact ? 1 : HomeTheme.settingsFooterButtonWidth
                implicitHeight: root.compact ? HomeTheme.compactActionTileHeight : HomeTheme.settingsFooterButtonHeight
                compact: true
                tile: root.compact
                text: qsTranslate("ServerDialog", "PC Console Start")
                iconSource: "qrc:/QSanguosha/Home/icons/quick-join.svg"
                onClicked: root.start(-1)
            }

            HomeMainButton {
                id: serverButton
                Layout.fillWidth: root.compact
                Layout.preferredWidth: root.compact ? 1 : HomeTheme.settingsFooterButtonWidth
                implicitHeight: root.compact ? HomeTheme.compactActionTileHeight : HomeTheme.settingsFooterButtonHeight
                compact: true
                tile: root.compact
                primary: true
                text: qsTranslate("ServerDialog", "Start Server")
                iconSource: "qrc:/QSanguosha/Home/icons/server.svg"
                onClicked: root.start(1)
            }
        }
    }
}
