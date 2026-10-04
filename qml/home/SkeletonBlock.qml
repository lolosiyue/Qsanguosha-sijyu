import QtQuick
import "."

// Static on purpose: the home page is hosted by a QQuickWidget, which has no render thread.
// A looping pulse (even an Animator) re-renders and flushes the whole scene on the GUI thread
// every frame until the last tile loads, which is what made catalog loading stutter.
Rectangle {
    id: bone
    radius: 6
    color: HomeTheme.baIce
    opacity: 0.7
    clip: true
}
