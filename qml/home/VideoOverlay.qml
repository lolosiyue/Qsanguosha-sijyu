import QtQuick
import QtMultimedia
import "."

// Home page video backdrop. Uses MediaPlayer + VideoOutput instead of Video: Video
// does not expose mediaStatus, so "loaded successfully" and "format unsupported"
// cannot be told apart, while M2B-A requires the two to be distinguished. No
// audioOutput is connected, so the backdrop video is always silent.
Item {
    id: root

    property url source: ""
    // 結果分類同 MultimediaSmokeReport 用同一套字串。
    signal videoReady()
    signal failed(string reason, string message)

    VideoOutput {
        id: output

        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
        opacity: HomeTheme.backdropOpacity
    }

    MediaPlayer {
        id: player

        source: root.source
        videoOutput: output
        loops: MediaPlayer.Infinite

        onErrorOccurred: function(error, errorString) {
            var reason = "playback_error";
            if (error === MediaPlayer.ResourceError)
                reason = homeController.localFileExists(root.source)
                    ? "playback_error" : "asset_missing";
            else if (error === MediaPlayer.FormatError)
                reason = "codec_unsupported";
            root.failed(reason, errorString ? String(errorString) : "");
        }

        onMediaStatusChanged: {
            if (player.mediaStatus === MediaPlayer.LoadedMedia
                    || player.mediaStatus === MediaPlayer.BufferedMedia)
                root.videoReady();
            else if (player.mediaStatus === MediaPlayer.InvalidMedia)
                root.failed("codec_unsupported", "InvalidMedia");
        }

        // source 就緒後需呼叫 play() 才會有畫面
        Component.onCompleted: player.play()
        onSourceChanged: player.play()
    }
}
