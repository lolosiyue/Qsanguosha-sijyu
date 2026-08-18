import QtQuick
import "."

Rectangle {
    id: bone
    radius: 6
    color: HomeTheme.baIce
    opacity: 0.55
    clip: true

    SequentialAnimation on opacity {
        running: bone.visible && bone.width > 0 && bone.height > 0
        loops: Animation.Infinite
        NumberAnimation { to: 0.95; duration: 750; easing.type: Easing.InOutQuad }
        NumberAnimation { to: 0.42; duration: 750; easing.type: Easing.InOutQuad }
    }
}
