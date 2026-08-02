import QtQuick
import QtMultimedia
import "."

Item {
    id: root

    property url source: ""
    signal errorOccurred()

    Video {
        id: video

        anchors.fill: parent
        source: root.source
        fillMode: VideoOutput.PreserveAspectCrop
        muted: true
        loops: MediaPlayer.Infinite
        opacity: HomeTheme.backdropOpacity

        onErrorOccurred: root.errorOccurred()

        // Qt 6 的 Video 不會自動播放，source 就緒後需呼叫 play() 才會有畫面
        Component.onCompleted: video.play()
        onSourceChanged: video.play()
    }
}
