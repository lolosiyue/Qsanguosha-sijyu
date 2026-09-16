package org.qsanguosha.game;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.graphics.Rect;

import androidx.core.util.Consumer;
import androidx.window.java.layout.WindowInfoTrackerCallbackAdapter;
import androidx.window.layout.DisplayFeature;
import androidx.window.layout.FoldingFeature;
import androidx.window.layout.WindowInfoTracker;
import androidx.window.layout.WindowLayoutInfo;

import java.util.concurrent.Executor;

/** Small lifecycle-aware adapter from Jetpack WindowManager to the Qt provider. */
public final class RoomWindowPostureBridge {
    private static final Object LOCK = new Object();

    private static Activity requestedActivity;
    private static Activity listeningActivity;
    private static WindowInfoTrackerCallbackAdapter tracker;
    private static Consumer<WindowLayoutInfo> listener;
    private static int generation;
    private static int nativeGeneration;
    private static boolean responsivePreviewEnabled;
    private static boolean originalOrientationSaved;
    private static int originalRequestedOrientation;
    private static int orientationRequestGeneration;

    private RoomWindowPostureBridge() {
    }

    public static void attach(Activity activity, int qtGeneration) {
        if (activity == null) {
            return;
        }
        final int request;
        synchronized (LOCK) {
            requestedActivity = activity;
            nativeGeneration = qtGeneration;
            request = ++generation;
        }
        activity.runOnUiThread(() -> {
            synchronized (LOCK) {
                if (request != generation || requestedActivity != activity) {
                    return;
                }
                stopOnUiThread();
                try {
                    WindowInfoTracker windowInfoTracker = WindowInfoTracker.getOrCreate(activity);
                    WindowInfoTrackerCallbackAdapter callbackAdapter =
                            new WindowInfoTrackerCallbackAdapter(windowInfoTracker);
                    Consumer<WindowLayoutInfo> callback =
                            info -> publishWindowLayoutInfo(request, qtGeneration, info);
                    Executor mainExecutor = activity.getMainExecutor();
                    listeningActivity = activity;
                    tracker = callbackAdapter;
                    listener = callback;
                    callbackAdapter.addWindowLayoutInfoListener(activity, mainExecutor, callback);
                    if (responsivePreviewEnabled) {
                        activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
                    }
                } catch (RuntimeException unavailable) {
                    stopOnUiThread();
                    nativeOnPostureChanged(qtGeneration, 0, 0, 0, 0, 0, false, false);
                }
            }
        });
    }

    public static void detach() {
        final Activity activity;
        final int request;
        synchronized (LOCK) {
            activity = requestedActivity != null ? requestedActivity : listeningActivity;
            requestedActivity = null;
            request = ++generation;
        }
        if (activity == null) {
            return;
        }
        activity.runOnUiThread(() -> {
            synchronized (LOCK) {
                if (request == generation) {
                    stopOnUiThread();
                }
            }
        });
    }

    /** Enables rotation only while the user explicitly previews responsive UI. */
    public static void setResponsivePreview(Activity activity, boolean enabled) {
        if (activity == null) {
            return;
        }
        final int request;
        synchronized (LOCK) {
            request = ++orientationRequestGeneration;
        }
        activity.runOnUiThread(() -> {
            synchronized (LOCK) {
                if (request != orientationRequestGeneration) {
                    return;
                }
                if (enabled) {
                    if (!responsivePreviewEnabled) {
                        originalRequestedOrientation = activity.getRequestedOrientation();
                        originalOrientationSaved = true;
                    }
                    responsivePreviewEnabled = true;
                    activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
                    return;
                }
                if (!responsivePreviewEnabled) {
                    return;
                }
                responsivePreviewEnabled = false;
                if (originalOrientationSaved) {
                    activity.setRequestedOrientation(originalRequestedOrientation);
                }
                originalOrientationSaved = false;
            }
        });
    }

    private static void stopOnUiThread() {
        if (tracker != null && listener != null) {
            tracker.removeWindowLayoutInfoListener(listener);
        }
        tracker = null;
        listener = null;
        listeningActivity = null;
    }

    private static void publishWindowLayoutInfo(int request, int qtGeneration,
                                                WindowLayoutInfo info) {
        synchronized (LOCK) {
            if (request != generation || qtGeneration != nativeGeneration
                    || listeningActivity == null) {
                return;
            }
        }
        int mode = 0;
        Rect bounds = new Rect();
        boolean separating = false;
        boolean occluding = false;

        for (DisplayFeature displayFeature : info.getDisplayFeatures()) {
            if (!(displayFeature instanceof FoldingFeature)) {
                continue;
            }
            FoldingFeature feature = (FoldingFeature) displayFeature;
            bounds = feature.getBounds();
            separating = feature.isSeparating();
            occluding = feature.getOcclusionType().equals(FoldingFeature.OcclusionType.FULL);
            // A separating/occluding feature must remain visible to layout
            // consumers even when a dual-screen device reports FLAT posture.
            if (feature.getState().equals(FoldingFeature.State.HALF_OPENED)
                    || separating || occluding) {
                mode = feature.getOrientation().equals(FoldingFeature.Orientation.VERTICAL) ? 1 : 2;
            }
            break;
        }

        // Bounds are Android window pixels; the Qt adapter converts them using
        // the active screen DPR before exposing them to scene-local consumers.
        nativeOnPostureChanged(qtGeneration, mode, bounds.left, bounds.top,
                bounds.right, bounds.bottom, separating, occluding);
    }

    private static native void nativeOnPostureChanged(int generation, int mode,
                                                       int left, int top, int right, int bottom,
                                                       boolean separating, boolean occluding);
}
