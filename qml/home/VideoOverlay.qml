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
    }
}
