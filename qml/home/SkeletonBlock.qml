import QtQuick
import "."

Rectangle {
    id: bone
    radius: 6
    color: HomeTheme.baIce
    opacity: 0.55
    clip: true

    // Animator 在渲染執行緒跑，GUI 執行緒忙著建立格子時脈動仍然順暢。
    SequentialAnimation {
        running: bone.visible && bone.width > 0 && bone.height > 0
        loops: Animation.Infinite
        OpacityAnimator { target: bone; to: 0.95; duration: 750; easing.type: Easing.InOutQuad }
        OpacityAnimator { target: bone; to: 0.42; duration: 750; easing.type: Easing.InOutQuad }
    }
}
