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
    // Loaded but paused while set, so a held backdrop starts at once when released.
    property bool held: false
    // Use the same result labels as MultimediaSmokeReport.
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

        // Call play() when the source is ready; Qt 6 Video does not autoplay.
        Component.onCompleted: if (!root.held) player.play()
        onSourceChanged: if (!root.held) player.play()
    }

    onHeldChanged: if (!held) player.play()
}
