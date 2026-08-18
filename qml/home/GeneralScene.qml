import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    focus: true

    property var allGenerals: []
    property var details: ({})
    property string selectedName: ""
    property string searchText: ""
    property string kingdomFilter: "all"
    property int pageSize: 20

    ListModel { id: filteredModel }

    function kingdomLabel(kingdom) {
        switch (kingdom) {
        case "wei": return "魏"
        case "shu": return "蜀"
        case "wu": return "吳"
        case "qun": return "群"
        case "god": return "神"
        default: return kingdom || "—"
        }
    }

    function kingdomAccent(kingdom) {
        switch (kingdom) {
        case "wei": return "#4D78C8"
        case "shu": return "#4C9A68"
        case "wu": return "#C35D5D"
        case "qun": return "#7C67B2"
        case "god": return "#B59145"
        default: return HomeTheme.btnPrimary
        }
    }

    function rebuild() {
        filteredModel.clear()
        var needle = searchText.trim().toLowerCase()
        for (var i = 0; i < allGenerals.length; ++i) {
            var g = allGenerals[i]
            if (kingdomFilter !== "all" && String(g.kingdoms).split("+").indexOf(kingdomFilter) < 0)
                continue
            if (needle.length > 0) {
                var haystack = (String(g.displayName) + " " + String(g.nickname) + " " + String(g.name) + " " + String(g.packageName)).toLowerCase()
                if (haystack.indexOf(needle) < 0)
                    continue
            }
            filteredModel.append(g)
        }
        if (filteredModel.count > 0 && (selectedName === "" || !containsName(selectedName)))
            selectGeneral(filteredModel.get(0).name)
    }

    function containsName(name) {
        for (var i = 0; i < filteredModel.count; ++i)
            if (filteredModel.get(i).name === name)
                return true
        return false
    }

    function selectGeneral(name) {
        selectedName = name
        details = homeController.generalDetails(name)
    }

    Component.onCompleted: {
        allGenerals = homeController.generals()
        rebuild()
    }

    Keys.onEscapePressed: homeController.openHome()

    HomeBackground {
        anchors.fill: parent
    }

    Rectangle {
        anchors.fill: parent
        color: homeController.isDarkTheme ? "#78070B18" : "#66EDF5FF"
    }

    Item {
        id: canvas
        anchors.centerIn: parent
        width: 1920
        height: 1080
        scale: Math.min(root.width / width, root.height / height)

        // Header inspired by image-gen-1.png while retaining the home glass/blue language.
        Rectangle {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 112
            color: HomeTheme.panelTop
            border.color: HomeTheme.panelBorder
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 38
                anchors.rightMargin: 38
                spacing: 24

                Button {
                    text: "‹"
                    font.pixelSize: 36
                    Layout.preferredWidth: 60
                    Layout.preferredHeight: 56
                    onClicked: homeController.openHome()
                }

                Column {
                    Layout.preferredWidth: 340
                    spacing: 1
                    Text {
                        text: "武將一覽"
                        color: HomeTheme.btnSecondaryText
                        font.pixelSize: 30
                        font.bold: true
                    }
                    Text {
                        text: "GENERAL OVERVIEW"
                        color: HomeTheme.pillText
                        font.pixelSize: 12
                        font.letterSpacing: 3
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 640
                    Layout.preferredHeight: 54
                    radius: 27
                    color: HomeTheme.btnSecondary
                    border.color: searchField.activeFocus ? HomeTheme.focusBorderHigh : HomeTheme.btnSecondaryBorder

                    TextField {
                        id: searchField
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        placeholderText: "搜尋武將名、稱號、代號或擴展包…"
                        color: HomeTheme.btnSecondaryText
                        placeholderTextColor: HomeTheme.pillText
                        font.pixelSize: 18
                        background: Item {}
                        onTextChanged: {
                            root.searchText = text
                            root.rebuild()
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 190
                    Layout.preferredHeight: 46
                    radius: 23
                    color: HomeTheme.pillBg
                    Text {
                        anchors.centerIn: parent
                        text: "全部武將  " + allGenerals.length
                        color: HomeTheme.btnSecondaryText
                        font.pixelSize: 16
                    }
                }

                Button {
                    text: homeController.isDarkTheme ? "☀" : "☾"
                    font.pixelSize: 24
                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 54
                    onClicked: homeController.toggleTheme()
                }
            }
        }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            anchors.topMargin: 22
            anchors.bottomMargin: 24
            spacing: 18

            // Left filter rail
            Rectangle {
                width: 300
                height: parent.height
                radius: 16
                color: HomeTheme.panelTop
                border.color: HomeTheme.panelBorder
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "陣營篩選"
                        color: HomeTheme.btnSecondaryText
                        font.pixelSize: 19
                        font.bold: true
                    }

                    Repeater {
                        model: [
                            { key: "all", text: "全部", mark: "全" },
                            { key: "wei", text: "魏", mark: "魏" },
                            { key: "shu", text: "蜀", mark: "蜀" },
                            { key: "wu", text: "吳", mark: "吳" },
                            { key: "qun", text: "群", mark: "群" },
                            { key: "god", text: "神", mark: "神" }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: 264
                            height: 54
                            radius: 10
                            color: root.kingdomFilter === modelData.key ? HomeTheme.navBgActive : (filterMouse.containsMouse ? HomeTheme.navBgHover : "transparent")
                            border.color: root.kingdomFilter === modelData.key ? HomeTheme.navBorderActive : "transparent"

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 34
                                    height: 34
                                    radius: 17
                                    color: modelData.key === "all" ? HomeTheme.btnPrimary : root.kingdomAccent(modelData.key)
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.mark
                                        color: "white"
                                        font.bold: true
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.text
                                    color: root.kingdomFilter === modelData.key || filterMouse.containsMouse ? HomeTheme.navTextHover : HomeTheme.navTextIdle
                                    font.pixelSize: 18
                                }
                            }

                            MouseArea {
                                id: filterMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    root.kingdomFilter = modelData.key
                                    root.rebuild()
                                }
                            }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: HomeTheme.panelBorder; opacity: 0.55 }

                    Text {
                        text: "目前顯示"
                        color: HomeTheme.pillText
                        font.pixelSize: 15
                    }
                    Text {
                        text: filteredModel.count + " 位武將"
                        color: HomeTheme.btnSecondaryText
                        font.pixelSize: 24
                        font.bold: true
                    }

                    Item { width: 1; height: 12 }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "點選武將卡片後，右側顯示完整立繪、體力與技能資料。Esc 可直接返回首頁。"
                        color: HomeTheme.pillText
                        font.pixelSize: 14
                        lineHeight: 1.35
                    }

                    Item { width: 1; height: 1; Layout.fillHeight: true }
                }
            }

            // Centre card browser
            Rectangle {
                width: 760
                height: parent.height
                radius: 16
                color: HomeTheme.panelTop
                border.color: HomeTheme.panelBorder
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Row {
                        width: parent.width
                        height: 42
                        spacing: 12
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.kingdomFilter === "all" ? "全部武將" : root.kingdomLabel(root.kingdomFilter) + "勢力"
                            color: HomeTheme.btnSecondaryText
                            font.pixelSize: 21
                            font.bold: true
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "· " + filteredModel.count
                            color: HomeTheme.pillText
                            font.pixelSize: 16
                        }
                    }

                    GridView {
                        id: generalGrid
                        width: parent.width
                        height: parent.height - 54
                        clip: true
                        cellWidth: 178
                        cellHeight: 236
                        model: filteredModel
                        boundsBehavior: Flickable.StopAtBounds

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Item {
                            required property string name
                            required property string displayName
                            required property string nickname
                            required property string kingdom
                            required property int maxHp
                            required property url portrait

                            width: generalGrid.cellWidth
                            height: generalGrid.cellHeight

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 6
                                radius: 12
                                color: root.selectedName === name ? HomeTheme.navBgActive : HomeTheme.btnSecondary
                                border.width: root.selectedName === name ? 2 : 1
                                border.color: root.selectedName === name ? HomeTheme.focusBorderHigh : HomeTheme.btnSecondaryBorder

                                Rectangle {
                                    id: artClip
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 7
                                    height: 166
                                    radius: 9
                                    clip: true
                                    color: HomeTheme.windowBg

                                    Image {
                                        anchors.fill: parent
                                        source: portrait
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.margins: 7
                                        width: 32
                                        height: 32
                                        radius: 16
                                        color: root.kingdomAccent(kingdom)
                                        Text {
                                            anchors.centerIn: parent
                                            text: root.kingdomLabel(kingdom)
                                            color: "white"
                                            font.pixelSize: 15
                                            font.bold: true
                                        }
                                    }
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: hpText.left
                                    anchors.bottom: parent.bottom
                                    anchors.leftMargin: 12
                                    anchors.bottomMargin: 12
                                    text: displayName
                                    elide: Text.ElideRight
                                    color: root.selectedName === name ? HomeTheme.navTextActive : HomeTheme.btnSecondaryText
                                    font.pixelSize: 18
                                    font.bold: true
                                }
                                Text {
                                    id: hpText
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: 12
                                    anchors.bottomMargin: 12
                                    text: "♥ " + maxHp
                                    color: root.selectedName === name ? HomeTheme.navTextActive : HomeTheme.pillText
                                    font.pixelSize: 14
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.selectGeneral(name)
                                }
                            }
                        }
                    }
                }
            }

            // Right detail panel: large illustration + data + skills, matching image-gen-1 layout.
            Rectangle {
                width: parent.width - 300 - 760 - 36
                height: parent.height
                radius: 16
                color: HomeTheme.panelTop
                border.color: HomeTheme.panelBorder
                border.width: 1
                clip: true

                Flickable {
                    anchors.fill: parent
                    contentWidth: width
                    contentHeight: detailColumn.height + 32
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    Column {
                        id: detailColumn
                        width: parent.width
                        spacing: 0

                        Item {
                            width: parent.width
                            height: 440

                            Image {
                                anchors.fill: parent
                                source: details.portrait || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                opacity: 0.88
                            }

                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "#18000000" }
                                    GradientStop { position: 0.62; color: "#26000000" }
                                    GradientStop { position: 1.0; color: homeController.isDarkTheme ? "#F00A0E27" : "#F0F2F7FD" }
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 22
                                width: 54
                                height: 54
                                radius: 27
                                color: root.kingdomAccent(details.kingdom || "")
                                Text {
                                    anchors.centerIn: parent
                                    text: root.kingdomLabel(details.kingdom || "")
                                    color: "white"
                                    font.pixelSize: 23
                                    font.bold: true
                                }
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 24
                                anchors.bottomMargin: 22
                                spacing: 4
                                Text {
                                    text: details.displayName || "選擇武將"
                                    color: HomeTheme.btnSecondaryText
                                    font.pixelSize: 34
                                    font.bold: true
                                }
                                Text {
                                    text: details.nickname || ""
                                    color: HomeTheme.pillText
                                    font.pixelSize: 16
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 78
                            color: "transparent"
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 24
                                anchors.rightMargin: 24
                                spacing: 32
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "勢力  " + root.kingdomLabel(details.kingdom || "")
                                    color: HomeTheme.btnSecondaryText
                                    font.pixelSize: 17
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "體力值  " + (details.maxHp || "—")
                                    color: HomeTheme.btnSecondaryText
                                    font.pixelSize: 17
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "擴展包  " + (details.package || "—")
                                    color: HomeTheme.btnSecondaryText
                                    font.pixelSize: 17
                                }
                            }
                        }

                        Rectangle { width: parent.width - 48; x: 24; height: 1; color: HomeTheme.panelBorder; opacity: 0.5 }

                        Text {
                            x: 24
                            width: parent.width - 48
                            topPadding: 18
                            bottomPadding: 12
                            text: "技能"
                            color: HomeTheme.btnPrimary
                            font.pixelSize: 21
                            font.bold: true
                        }

                        Repeater {
                            model: details.skills || []
                            delegate: Rectangle {
                                required property var modelData
                                x: 24
                                width: detailColumn.width - 48
                                height: skillContent.height + 28
                                radius: 12
                                color: HomeTheme.btnSecondary
                                border.color: HomeTheme.btnSecondaryBorder

                                Column {
                                    id: skillContent
                                    x: 16
                                    y: 14
                                    width: parent.width - 32
                                    spacing: 7
                                    Text {
                                        text: modelData.displayName
                                        color: HomeTheme.btnSecondaryText
                                        font.pixelSize: 19
                                        font.bold: true
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.description || ""
                                        textFormat: Text.RichText
                                        wrapMode: Text.WordWrap
                                        color: HomeTheme.btnSecondaryText
                                        font.pixelSize: 15
                                        lineHeight: 1.3
                                    }
                                    Text {
                                        visible: String(modelData.oracleText || "").length > 0
                                        width: parent.width
                                        text: modelData.oracleText || ""
                                        wrapMode: Text.WordWrap
                                        color: HomeTheme.pillText
                                        font.pixelSize: 13
                                        font.italic: true
                                    }
                                }
                            }
                        }

                        Item { width: 1; height: 22 }
                    }
                }
            }
        }
    }
}
