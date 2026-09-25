import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "."

// 首頁設定頁:與舊版 ConfigDialog 共用 settingsSession(C++ SettingsSession),
// 這裡只負責外觀。預覽鍵變動即時套用,「儲存」才寫入,「取消」或離開此頁即復原。
Item {
    id: root
    objectName: "settingsScene"

    property real uiScale: 1.0
    property bool compact: Config.responsiveUiEnabled && (width < 900 || height > width)
    property int section: 0
    readonly property var values: settingsSession.values
    readonly property bool highContrast: homeController.visualMode === "highcontrast"
    readonly property var navigationEntry: sectionTabs.count > 0 ? sectionTabs.itemAt(0) : null
    readonly property var lastControl: saveButton
    signal navigationEndpointChanged()

    focus: true
    Keys.onEscapePressed: root.cancel()
    onNavigationEntryChanged: navigationEndpointChanged()
    onSectionChanged: formView.contentY = 0

    function takeKeyboard() {
        var tab = sectionTabs.itemAt(root.section)
        if (tab) tab.forceActiveFocus()
    }
    function save() {
        settingsSession.commit()
        homeController.openHome()
    }
    function cancel() {
        settingsSession.revert()
        homeController.openHome()
    }
    function set(key, value) {
        settingsSession.setValue(key, value)
    }
    function fontLabel(font) {
        return font ? font.family + " " + font.pointSize : ""
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
        color: HomeTheme.cardTextSecondary
        font.pixelSize: HomeTheme.settingsFontSize
        wrapMode: Text.Wrap
    }

    component SettingCheck: Basic.CheckBox {
        id: check
        required property string key
        property string hint: ""
        Layout.fillWidth: true
        implicitHeight: root.compact ? HomeTheme.compactTouch
                                     : Math.max(implicitContentHeight, implicitIndicatorHeight) + topPadding + bottomPadding
        padding: 4
        spacing: 10
        checked: root.values[key] === true
        onToggled: root.set(key, checked)
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

    component SettingSlider: RowLayout {
        id: sliderRow
        required property string key
        property real from: 0
        property real to: 1
        property real stepSize: 0.01
        property real displayScale: 100
        property int decimals: 0
        property string suffix: "%"
        property string accessibleName: ""
        Layout.fillWidth: true
        spacing: HomeTheme.cardPanelGap

        Basic.Slider {
            id: slider
            Layout.fillWidth: true
            from: sliderRow.from
            to: sliderRow.to
            stepSize: sliderRow.stepSize
            snapMode: Slider.SnapAlways
            value: Number(root.values[sliderRow.key])
            onMoved: root.set(sliderRow.key, value)
            onActiveFocusChanged: if (activeFocus) root.reveal(sliderRow)
            Accessible.name: sliderRow.accessibleName
            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: HomeTheme.settingsSliderTrack
                radius: height / 2
                color: HomeTheme.cardInputFill
                border.width: HomeTheme.cardBorderWidth
                border.color: HomeTheme.cardPanelBorder
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: parent.radius
                    color: HomeTheme.cardInteractive
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                implicitWidth: HomeTheme.settingsSliderHandle
                implicitHeight: HomeTheme.settingsSliderHandle
                radius: width / 2
                color: slider.pressed ? HomeTheme.cardControlPressedFill : HomeTheme.baWhite
                border.width: slider.activeFocus
                              ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                                   : HomeTheme.cardFocusBorderWidth)
                              : HomeTheme.cardSelectedBorderWidth
                border.color: slider.activeFocus ? HomeTheme.focusBorderHigh : HomeTheme.cardInteractive
            }
        }

        Text {
            Layout.preferredWidth: HomeTheme.settingsValueWidth
            horizontalAlignment: Text.AlignRight
            text: (slider.value * sliderRow.displayScale).toFixed(sliderRow.decimals) + sliderRow.suffix
            color: HomeTheme.cardTextPrimary
            font.pixelSize: HomeTheme.settingsFontSize
            font.bold: true
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

    component PathField: Rectangle {
        property alias text: pathText.text
        Layout.fillWidth: true
        Layout.columnSpan: root.compact ? 2 : 1
        implicitHeight: HomeTheme.cardControlHeight
        radius: HomeTheme.cardControlRadius
        color: HomeTheme.cardInputFill
        border.width: HomeTheme.cardBorderWidth
        border.color: HomeTheme.cardPanelBorder
        Accessible.role: Accessible.StaticText
        Accessible.name: pathText.text
        Text {
            id: pathText
            anchors.fill: parent
            anchors.leftMargin: HomeTheme.cardControlHPadding
            anchors.rightMargin: HomeTheme.cardControlHPadding
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
            color: HomeTheme.cardTextPrimary
            font.pixelSize: HomeTheme.cardBodyFontSize
        }
    }

    // Path field then its buttons; compact moves the buttons onto their own line.
    component PathControls: GridLayout {
        Layout.fillWidth: true
        columns: root.compact ? 2 : 3
        columnSpacing: HomeTheme.compactGap
        rowSpacing: HomeTheme.compactGap
    }

    component FormButton: BAToolButton {
        id: formButton
        implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight + 4
        Layout.fillWidth: root.compact
        onActiveFocusChanged: if (activeFocus) root.reveal(formButton)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 0 : HomeTheme.cardPageHMargin
        anchors.rightMargin: root.compact ? 0 : HomeTheme.cardPageHMargin
        anchors.topMargin: root.compact ? 0 : HomeTheme.cardPageTopMargin
        anchors.bottomMargin: HomeTheme.cardPageBottomMargin
        spacing: root.compact ? HomeTheme.compactGap : HomeTheme.cardPanelGap

        BASlantedPanel {
            id: headerPanel
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
                    text: qsTranslate("HomeScene", "Settings")
                    color: HomeTheme.cardTextPrimary
                    font.pixelSize: root.compact ? HomeTheme.cardSectionTitleFontSize : HomeTheme.cardTitleFontSize
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("更改会即时预览；点击“保存”后生效，取消或离开此页则还原。")
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

            // 分頁:寬版在左側直排,窄版改成內容上方的橫條。
            GridLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: root.compact
                Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsNavWidth
                columns: root.compact ? 3 : 1
                columnSpacing: HomeTheme.compactGap
                rowSpacing: HomeTheme.compactGap

                Repeater {
                    id: sectionTabs
                    model: [qsTranslate("ConfigDialog", "显示"), qsTranslate("ConfigDialog", "Audio"),
                            qsTranslate("ConfigDialog", "Game")]
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

                        // —— 顯示 ——
                        ColumnLayout {
                            visible: root.section === 0
                            Layout.fillWidth: true
                            spacing: HomeTheme.settingsRowGap

                            GroupTitle { text: qsTranslate("ConfigDialog", "界面") }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "主题") }
                                SettingChoice {
                                    key: "ColorScheme"
                                    accessibleLabel: qsTranslate("ConfigDialog", "主题")
                                    options: [
                                        { "value": 0, "label": qsTranslate("ConfigDialog", "跟随系统") },
                                        { "value": 1, "label": qsTranslate("ConfigDialog", "亮色") },
                                        { "value": 2, "label": qsTranslate("ConfigDialog", "暗色") }
                                    ]
                                }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "界面缩放") }
                                SettingSlider {
                                    key: "UIScale"
                                    from: 1
                                    to: 2
                                    stepSize: 0.05
                                    displayScale: 1
                                    decimals: 2
                                    suffix: "x"
                                    accessibleName: qsTranslate("ConfigDialog", "界面缩放")
                                }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "Visual effects") }
                                SettingChoice {
                                    key: "EffectsProfile"
                                    accessibleLabel: qsTranslate("ConfigDialog", "Visual effects")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 500
                                    ToolTip.text: qsTranslate("ConfigDialog", "Full: every animation, Spine, GIF and video. Reduced: shorter animations, no Spine/video. None: no decorative animation at all.")
                                    options: [
                                        { "value": "full", "label": qsTranslate("ConfigDialog", "Full effects") },
                                        { "value": "reduced", "label": qsTranslate("ConfigDialog", "Reduced effects") },
                                        { "value": "none", "label": qsTranslate("ConfigDialog", "No decorative effects") }
                                    ]
                                }
                            }
                            SettingCheck { key: "NoIndicator"; text: qsTranslate("ConfigDialog", "No indicator") }
                            SettingCheck { key: "NoEquipAnim"; text: qsTranslate("ConfigDialog", "No equip anim") }
                            SettingCheck { key: "NoCardMoveAnim"; text: qsTranslate("ConfigDialog", "No card move anim") }
                            SettingCheck { key: "EnableAnimatedGenerals"; text: qsTranslate("ConfigDialog", "Enable animated generals (GIF)") }
                            SettingCheck { key: "EnablePointerEffect"; text: qsTranslate("ConfigDialog", "Pointer effect") }

                            GroupTitle { text: qsTranslate("ConfigDialog", "直向與單手操作") }
                            SettingCheck { key: "UI/ResponsiveLayout"; text: qsTranslate("ConfigDialog", "自適應版面（首頁、對話框與牌桌）") }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "單手操作") }
                                SettingChoice {
                                    key: "UI/RoomHandedness"
                                    accessibleLabel: qsTranslate("ConfigDialog", "單手操作")
                                    options: [
                                        { "value": 0, "label": qsTranslate("ConfigDialog", "雙手／無偏好") },
                                        { "value": 1, "label": qsTranslate("ConfigDialog", "左手操作") },
                                        { "value": 2, "label": qsTranslate("ConfigDialog", "右手操作") }
                                    ]
                                }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "直向背景（獨立保存）") }
                                PathControls {
                                    PathField { text: root.values["UI/PortraitBackgroundImage"] || "" }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "選擇直向背景")
                                        onClicked: Qt.callLater(function() { settingsSession.choosePortraitBackground() })
                                    }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "恢復直向預設")
                                        onClicked: settingsSession.resetPortraitBackground()
                                    }
                                }
                            }

                            GroupTitle { text: qsTranslate("ConfigDialog", "背景") }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "Setup background") }
                                PathControls {
                                    PathField {
                                        readonly property string path: root.values.BackgroundImage || ""
                                        text: path.startsWith(":") ? "" : path
                                    }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "Browse ...")
                                        onClicked: Qt.callLater(function() { settingsSession.chooseBackgroundImage() })
                                    }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "Reset")
                                        onClicked: settingsSession.resetBackgroundImage()
                                    }
                                }
                            }
                            SettingCheck { key: "EnableAutoBackgroundChange"; text: qsTranslate("ConfigDialog", "Enable auto background change according to Lord") }
                            SettingCheck { key: "EnableBackgroundVideo"; text: qsTranslate("ConfigDialog", "Enable video background on the home page") }

                            GroupTitle { text: qsTranslate("ConfigDialog", "Visual mode") }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "Color blindness") }
                                SettingChoice {
                                    key: "VisualMode"
                                    accessibleLabel: qsTranslate("ConfigDialog", "Visual mode")
                                    options: [
                                        { "value": "normal", "label": qsTranslate("ConfigDialog", "Normal") },
                                        { "value": "grayscale", "label": qsTranslate("ConfigDialog", "Grayscale") },
                                        { "value": "highcontrast", "label": qsTranslate("ConfigDialog", "High contrast") }
                                    ]
                                }
                            }

                            GroupTitle { text: qsTranslate("ConfigDialog", "Font setup") }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "Application font") }
                                PathControls {
                                    PathField { text: root.fontLabel(root.values.AppFont) }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "Set application font")
                                        onClicked: Qt.callLater(function() { settingsSession.chooseAppFont() })
                                    }
                                }
                            }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "Text edit font") }
                                PathControls {
                                    PathField {
                                        text: root.fontLabel(root.values.UIFont)
                                        Rectangle {
                                            anchors.right: parent.right
                                            anchors.rightMargin: HomeTheme.cardControlHPadding
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: HomeTheme.settingsCheckSize
                                            height: HomeTheme.settingsCheckSize
                                            radius: HomeTheme.cardBadgeRadius
                                            color: root.values.TextEditColor || "transparent"
                                            border.width: HomeTheme.cardBorderWidth
                                            border.color: HomeTheme.cardTileBorder
                                        }
                                    }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "Font ...")
                                        onClicked: Qt.callLater(function() { settingsSession.chooseTextEditFont() })
                                    }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "Color ...")
                                        onClicked: Qt.callLater(function() { settingsSession.chooseTextEditColor() })
                                    }
                                }
                            }
                        }

                        // —— 音訊 ——
                        ColumnLayout {
                            visible: root.section === 1
                            Layout.fillWidth: true
                            spacing: HomeTheme.settingsRowGap

                            GroupTitle { text: qsTranslate("ConfigDialog", "Audio") }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "Setup background music") }
                                PathControls {
                                    PathField { text: root.values.BackgroundMusic || "" }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "Browse ...")
                                        onClicked: Qt.callLater(function() { settingsSession.chooseBackgroundMusic() })
                                    }
                                    FormButton {
                                        text: qsTranslate("ConfigDialog", "Reset")
                                        onClicked: settingsSession.resetBackgroundMusic()
                                    }
                                }
                            }
                            SettingCheck { key: "EnableEffects"; text: qsTranslate("ConfigDialog", "Enable effects") }
                            SettingCheck {
                                key: "EnableLastWord"
                                text: qsTranslate("ConfigDialog", "Enable last word")
                                enabled: root.values.EnableEffects === true
                            }
                            SettingCheck { key: "AudioMuted"; text: qsTranslate("ConfigDialog", "Mute all audio") }

                            Repeater {
                                model: [
                                    { "key": "MasterVolume", "label": qsTranslate("ConfigDialog", "Master Volume") },
                                    { "key": "BGMVolume", "label": qsTranslate("ConfigDialog", "BGM    Volume") },
                                    { "key": "EffectVolume", "label": qsTranslate("ConfigDialog", "Effect Volume") },
                                    { "key": "VoiceVolume", "label": qsTranslate("ConfigDialog", "Voice  Volume") },
                                    { "key": "FrontBGMVolume", "label": qsTranslate("ConfigDialog", "Front  Volume") }
                                ]
                                SettingRow {
                                    id: volumeRow
                                    required property var modelData
                                    SettingLabel { text: volumeRow.modelData.label }
                                    SettingSlider {
                                        key: volumeRow.modelData.key
                                        accessibleName: volumeRow.modelData.label
                                    }
                                }
                            }
                        }

                        // —— 遊戲 ——
                        ColumnLayout {
                            visible: root.section === 2
                            Layout.fillWidth: true
                            spacing: HomeTheme.settingsRowGap

                            GroupTitle { text: qsTranslate("ConfigDialog", "Game") }
                            SettingCheck { key: "NeverNullifyMyTrick"; text: qsTranslate("ConfigDialog", "Never nullify my single target trick") }
                            SettingCheck { key: "EnableAutoTarget"; text: qsTranslate("ConfigDialog", "Enable auto target") }
                            SettingCheck { key: "EnableIntellectualSelection"; text: qsTranslate("ConfigDialog", "Enable intellectual selection") }
                            SettingCheck { key: "EnableDoubleClick"; text: qsTranslate("ConfigDialog", "Enable double-click") }
                            SettingCheck { key: "EnableSuperDrag"; text: qsTranslate("ConfigDialog", "Enable super-drag") }
                            SettingCheck { key: "EnableCardDescription"; text: qsTranslate("ConfigDialog", "The card description shows the English name") }
                            SettingCheck { key: "EnableOracleConcepts"; text: qsTranslate("ConfigDialog", "Enable oracle concepts display") }
                            SettingRow {
                                SettingLabel { text: qsTranslate("ConfigDialog", "Bubble Chat Box Keep Time") }
                                RowLayout {
                                    spacing: HomeTheme.compactGap
                                    Basic.SpinBox {
                                        id: bubbleSpin
                                        from: 0
                                        to: 5000
                                        stepSize: 100
                                        editable: true
                                        implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight
                                        value: root.values.BubbleChatBoxKeepTime || 0
                                        onValueModified: root.set("BubbleChatBoxKeepTime", value)
                                        onActiveFocusChanged: if (activeFocus) root.reveal(bubbleSpin)
                                        Accessible.name: qsTranslate("ConfigDialog", "Bubble Chat Box Keep Time")
                                        font.pixelSize: HomeTheme.settingsFontSize
                                        palette.text: HomeTheme.cardTextPrimary
                                        palette.base: HomeTheme.cardInputFill
                                        palette.button: HomeTheme.cardTileFill
                                        palette.buttonText: HomeTheme.cardTextPrimary
                                        palette.mid: HomeTheme.cardPanelBorder
                                        palette.highlight: HomeTheme.cardInteractive
                                    }
                                    Text {
                                        text: qsTranslate("ConfigDialog", " millisecond").trim()
                                        color: HomeTheme.cardTextSecondary
                                        font.pixelSize: HomeTheme.settingsFontSize
                                    }
                                }
                            }

                            GroupTitle { text: qsTranslate("ConfigDialog", "录像") }
                            SettingCheck {
                                key: "recorder/autosave"
                                text: qsTranslate("ConfigDialog", "自动保存")
                                hint: qsTranslate("ConfigDialog", "录像将会自动保存到recorder目录下")
                            }
                            SettingCheck {
                                key: "recorder/networkonly"
                                text: qsTranslate("ConfigDialog", "仅保存联机录像")
                                enabled: root.values["recorder/autosave"] === true
                            }
                            SettingCheck {
                                key: "recorder/eventsave"
                                text: qsTranslate("ConfigDialog", "即时保存")
                                hint: qsTranslate("ConfigDialog", "用于记录闪退的录像，会在相同文件夹下生成debug.txt；因为是即时记录，会占用部分性能，可能导致游戏卡顿或缓慢。")
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: HomeTheme.cardPanelGap

            Item { Layout.fillWidth: true }

            HomeMainButton {
                id: cancelButton
                Layout.fillWidth: root.compact
                Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsFooterButtonWidth
                implicitHeight: HomeTheme.settingsFooterButtonHeight
                compact: true
                text: qsTr("取消")
                iconSource: "qrc:/QSanguosha/Home/icons/home.svg"
                onClicked: root.cancel()
            }

            HomeMainButton {
                id: saveButton
                Layout.fillWidth: root.compact
                Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsFooterButtonWidth
                implicitHeight: HomeTheme.settingsFooterButtonHeight
                compact: true
                primary: true
                text: qsTr("保存")
                iconSource: "qrc:/QSanguosha/Home/icons/settings.svg"
                onClicked: root.save()
            }
        }
    }
}
