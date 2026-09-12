import QtQuick
import "."

Item {
    id: root

    property url backdropSource: {
        var cfg = homeController.backgroundImage;
        return cfg.toString() !== "" ? cfg : homeController.randomBackdrop();
    }

    readonly property bool backdropIsVideoFile: /\.(mp4|webm|mkv)$/i.test(String(backdropSource))

    // Video playback requires three conditions to hold at once: the user has not
    // disabled it, the multimedia backend actually loads, and the backdrop itself is
    // a video file. If any one fails, a static image is used and no Video component
    // is created.
    property bool isVideo: homeController.videoBackgroundEnabled
                           && homeController.hasVideoSupport
                           && backdropIsVideoFile

    // If the video cannot play, fall back to a static backdrop. This is the static
    // fallback required by M2B-A: HomeScene still loads even when the video fails.
    function fallBackToStaticBackdrop(reason, message) {
        homeController.reportVideoStatus(reason, message);
        isVideo = false;
        var next = homeController.randomBackdrop();
        if (next.toString() !== "" && next !== backdropSource)
            backdropSource = next;
        // 保留原因，只額外標記「靜態背景已經頂上」。
        homeController.confirmVideoFallback();
    }

    Component.onCompleted: {
        if (!backdropIsVideoFile)
            homeController.reportVideoStatus("not_requested", "");
        else if (!homeController.videoBackgroundEnabled)
            fallBackToStaticBackdrop("disabled", "");
        else if (!homeController.hasVideoSupport)
            fallBackToStaticBackdrop("backend_unavailable", "");
        else if (!homeController.localFileExists(backdropSource))
            fallBackToStaticBackdrop("asset_missing", String(backdropSource));
    }

    Rectangle {
        anchors.fill: parent
        color: HomeTheme.windowBg

        Image {
            id: backdropImage
            anchors.fill: parent
            source: isVideo ? "" : backdropSource
            fillMode: Image.PreserveAspectCrop
            mipmap: false
            asynchronous: true
            cache: true
            opacity: HomeTheme.backdropOpacity
            visible: !isVideo && status === Image.Ready

            onStatusChanged: {
                if (status === Image.Error && backdropSource.toString() !== "") {
                    var next = homeController.randomBackdrop()
                    if (next.toString() !== "" && next !== backdropSource)
                        backdropSource = next
                }
            }
        }

        Loader {
            anchors.fill: parent
            active: isVideo
            sourceComponent: VideoOverlay {
                source: backdropSource

                onVideoReady: homeController.reportVideoStatus("ok", "")
                onFailed: function(reason, message) {
                    root.fallBackToStaticBackdrop(reason, message);
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent

        gradient: Gradient {
            GradientStop { position: 0.0; color: HomeTheme.gradientTop }
            GradientStop { position: 0.3; color: HomeTheme.gradientMidTop }
            GradientStop { position: 0.6; color: HomeTheme.gradientMidBot }
            GradientStop { position: 1.0; color: HomeTheme.gradientBottom }
        }
    }
}
