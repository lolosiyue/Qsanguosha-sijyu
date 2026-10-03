import QtQuick
import "."

Rectangle {
    id: bone
    radius: 6
    color: HomeTheme.baIce
    opacity: 0.55
    clip: true

    // The Animator runs on the render thread, keeping the pulse smooth while the GUI builds the grid.
    SequentialAnimation {
        running: bone.visible && bone.width > 0 && bone.height > 0
        loops: Animation.Infinite
        OpacityAnimator { target: bone; to: 0.95; duration: 750; easing.type: Easing.InOutQuad }
        OpacityAnimator { target: bone; to: 0.42; duration: 750; easing.type: Easing.InOutQuad }
    }
}
