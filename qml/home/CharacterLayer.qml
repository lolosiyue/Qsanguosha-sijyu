import QtQuick

Item {
    id: root

    // 底緣向下延伸 0.5 倍視窗高：放大角色並讓下半身疊入底部導覽列被遮蓋
    property real baseBottomMargin: -parent.height * 0.5

    Image {
        id: character

        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.baseBottomMargin

        width: parent.width * 1.1
        height: parent.height * 1.42

        source: homeController.characterImage
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft
        verticalAlignment: Image.AlignBottom
        visible: source.toString() !== "" && status !== Image.Error

        mipmap: true
        asynchronous: true

        transform: Translate {
            x: mouseArea.normalizedX * 10
            y: mouseArea.normalizedY * 5
        }

        Behavior on x {
            NumberAnimation { duration: 250 }
        }

        Behavior on y {
            NumberAnimation { duration: 250 }
        }

        SequentialAnimation on anchors.bottomMargin {
            loops: Animation.Infinite

            NumberAnimation {
                to: root.baseBottomMargin + 4
                duration: 2400
                easing.type: Easing.InOutSine
            }

            NumberAnimation {
                to: root.baseBottomMargin - 4
                duration: 2400
                easing.type: Easing.InOutSine
            }
        }

        onStatusChanged: {
            if (status === Image.Error)
                console.error("Character load failed:", source)
        }
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        hoverEnabled: true

        property real normalizedX: (mouseX / width - 0.5) * 2
        property real normalizedY: (mouseY / height - 0.5) * 2
    }
}
