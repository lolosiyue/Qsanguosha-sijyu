import QtQuick
import QtMultimedia
import "."

// 首頁影片背景。用 MediaPlayer + VideoOutput 而唔用 Video：Video 冇 expose
// mediaStatus，分唔出「載入成功」同「格式唔支援」，而 M2B-A 要求呢兩者要
// 分得開。冇接 audioOutput，所以背景影片一定係無聲。
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
