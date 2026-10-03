package org.qsanguosha.game;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.MediaMetadataRetriever;
import android.media.MediaPlayer;
import android.media.SoundPool;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Game audio that stays inside the Android framework.
 * Qt Multimedia's AAudio data callback crashes under ARM translation, so this
 * bridge never registers a native callback. API 28 file loading is a later
 * {@code SDK_INT} branch inside {@link #loadEffectSample(String)}.
 */
public final class AudioBridge {
    private static final int VOICE_SLOTS = 8;
    private static final int MAX_EFFECT_STREAMS = 4;
    private static final int TRACKED_EFFECT_STREAMS = 16;
    private static final long START_TIMEOUT_SECONDS = 3L;

    private static final Object LOCK = new Object();

    private static HandlerThread thread;
    private static Handler handler;
    private static Context appContext;
    private static SoundPool effects;
    private static final HashMap<String, Integer> effectSamples = new HashMap<>();
    private static final HashMap<Integer, String> loadingSamples = new HashMap<>();
    private static final HashSet<String> playWhenLoaded = new HashSet<>();
    private static final HashMap<String, Boolean> playSuperpose = new HashMap<>();
    private static final HashMap<String, Integer> activeEffects = new HashMap<>();
    private static final HashMap<String, Integer> effectDurations = new HashMap<>();
    private static final ArrayList<Integer> effectStreams = new ArrayList<>();
    private static final MediaPlayer[] voices = new MediaPlayer[VOICE_SLOTS];
    private static final String[] voicePaths = new String[VOICE_SLOTS];
    private static final boolean[] voiceActive = new boolean[VOICE_SLOTS];
    private static final boolean[] voicePaused = new boolean[VOICE_SLOTS];
    private static final long[] voiceStarted = new long[VOICE_SLOTS];
    private static final int[] voiceGeneration = new int[VOICE_SLOTS];
    private static MediaPlayer bgmPlayer;
    private static String bgmPath = "";
    private static boolean bgmActive;
    private static boolean bgmPaused;
    private static volatile boolean ready;
    private static volatile boolean hasOutput;
    private static volatile boolean suspended;
    private static volatile float effectGain = 1f;
    private static volatile float voiceGain = 1f;
    private static volatile float bgmGain = 1f;
    private static volatile String lastError = "";

    private AudioBridge() {
    }

    public static boolean start(Context context) {
        synchronized (LOCK) {
            if (ready) {
                return true;
            }
            if (context == null) {
                lastError = "no android context";
                return false;
            }
            appContext = context.getApplicationContext();
            thread = new HandlerThread("QSanAudio");
            thread.start();
            handler = new Handler(thread.getLooper());
        }
        final boolean[] started = {false};
        final CountDownLatch latch = new CountDownLatch(1);
        handler.post(() -> {
            started[0] = openOnAudioThread();
            latch.countDown();
        });
        try {
            if (!latch.await(START_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                lastError = "audio thread timeout";
                shutdown();
                return false;
            }
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            lastError = "audio thread interrupted";
            shutdown();
            return false;
        }
        ready = started[0];
        if (!ready) {
            shutdown();
        }
        return ready;
    }

    public static void shutdown() {
        final Handler audioHandler;
        final HandlerThread audioThread;
        synchronized (LOCK) {
            ready = false;
            audioHandler = handler;
            audioThread = thread;
            handler = null;
            thread = null;
        }
        if (audioHandler == null || audioThread == null) {
            return;
        }
        final CountDownLatch latch = new CountDownLatch(1);
        audioHandler.post(() -> {
            releaseOnAudioThread();
            latch.countDown();
        });
        try {
            latch.await(START_TIMEOUT_SECONDS, TimeUnit.SECONDS);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        }
        audioThread.quitSafely();
    }

    public static void play(String path, boolean superpose, boolean effectChannel) {
        post(() -> {
            if (!ready || suspended || path == null || path.isEmpty()) {
                return;
            }
            if (effectChannel) {
                loadEffect(path, true, superpose);
            } else {
                playVoice(path, superpose);
            }
        });
    }

    public static void stopAll() {
        post(AudioBridge::stopAllOnAudioThread);
    }

    public static void playBgm(String path) {
        post(() -> {
            if (!ready || suspended || path == null || path.isEmpty()) {
                return;
            }
            if (bgmActive && path.equals(bgmPath) && bgmPlayer != null && playing(bgmPlayer)) {
                return;
            }
            ensureBgmPlayer();
            try {
                armPlayer(bgmPlayer, AudioAttributes.CONTENT_TYPE_MUSIC, path);
                bgmPlayer.setLooping(true);
                bgmPlayer.setVolume(bgmGain, bgmGain);
                bgmPlayer.start();
                bgmPath = path;
                bgmActive = true;
                bgmPaused = false;
            } catch (IOException | IllegalStateException | IllegalArgumentException failure) {
                noteFailure(failure);
                bgmPlayer.reset();
                bgmActive = false;
                bgmPath = "";
            }
        });
    }

    public static void setBgmVolume(float volume) {
        setVolumes(effectGain, voiceGain, volume);
    }

    public static void stopBgm() {
        post(() -> {
            bgmActive = false;
            bgmPaused = false;
            bgmPath = "";
            if (bgmPlayer != null) {
                bgmPlayer.reset();
            }
        });
    }

    public static void setSuspended(boolean pause) {
        post(() -> {
            if (suspended == pause) {
                return;
            }
            suspended = pause;
            if (effects == null) {
                return;
            }
            if (pause) {
                effects.autoPause();
                pauseVoices();
                if (bgmPlayer != null && bgmActive && playing(bgmPlayer)) {
                    bgmPlayer.pause();
                    bgmPaused = true;
                }
                return;
            }
            effects.autoResume();
            resumeVoices();
            if (bgmPaused && bgmPlayer != null) {
                bgmPaused = false;
                try {
                    bgmPlayer.start();
                } catch (IllegalStateException failure) {
                    noteFailure(failure);
                    bgmActive = false;
                }
            }
        });
    }

    public static void setVolumes(float effect, float voice, float bgm) {
        post(() -> {
            effectGain = clamp(effect);
            voiceGain = clamp(voice);
            bgmGain = clamp(bgm);
            if (effects != null) {
                for (int stream : effectStreams) {
                    effects.setVolume(stream, effectGain, effectGain);
                }
            }
            for (MediaPlayer player : voices) {
                if (player != null) {
                    player.setVolume(voiceGain, voiceGain);
                }
            }
            if (bgmPlayer != null) {
                bgmPlayer.setVolume(bgmGain, bgmGain);
            }
        });
    }

    public static boolean hasOutputDevice() {
        return hasOutput;
    }

    public static int sdk() {
        return Build.VERSION.SDK_INT;
    }

    public static String lastError() {
        return lastError == null ? "" : lastError;
    }

    private static void post(Runnable action) {
        final Handler audioHandler;
        synchronized (LOCK) {
            audioHandler = handler;
        }
        if (audioHandler != null) {
            audioHandler.post(action);
        }
    }

    private static boolean openOnAudioThread() {
        AudioManager manager = appContext.getSystemService(AudioManager.class);
        AudioDeviceInfo[] outputs = manager == null
                ? new AudioDeviceInfo[0]
                : manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
        hasOutput = outputs.length > 0;
        AudioAttributes attributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build();
        effects = new SoundPool.Builder()
                .setMaxStreams(MAX_EFFECT_STREAMS)
                .setAudioAttributes(attributes)
                .build();
        effects.setOnLoadCompleteListener(AudioBridge::onEffectLoaded);
        return true;
    }

    private static void onEffectLoaded(SoundPool pool, int sampleId, int status) {
        String path = loadingSamples.remove(sampleId);
        if (path == null) {
            return;
        }
        if (status != 0) {
            lastError = "effect decode failed";
            playWhenLoaded.remove(path);
            playSuperpose.remove(path);
            return;
        }
        effectSamples.put(path, sampleId);
        if (playWhenLoaded.remove(path)) {
            boolean superpose = Boolean.TRUE.equals(playSuperpose.remove(path));
            playLoadedEffect(path, sampleId, superpose);
        }
    }

    private static void loadEffect(String path, boolean playAfter, boolean superpose) {
        Integer sampleId = effectSamples.get(path);
        if (sampleId != null) {
            if (playAfter) {
                playLoadedEffect(path, sampleId, superpose);
            }
            return;
        }
        if (loadingSamples.containsValue(path)) {
            if (playAfter) {
                playWhenLoaded.add(path);
                playSuperpose.put(path, superpose);
            }
            return;
        }
        int loaded = loadEffectSample(path);
        if (loaded == 0) {
            return;
        }
        loadingSamples.put(loaded, path);
        if (playAfter) {
            playWhenLoaded.add(path);
            playSuperpose.put(path, superpose);
        }
    }

    /** API 29+ path load. Older SoundPool overloads belong in the else branch. */
    private static int loadEffectSample(String path) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return effects.load(path, 1);
        }
        lastError = "effect file load requires API 29";
        return 0;
    }

    private static void playLoadedEffect(String path, int sampleId, boolean superpose) {
        if (suspended || effects == null) {
            return;
        }
        if (!superpose && activeEffects.containsKey(path)) {
            return;
        }
        int stream = effects.play(sampleId, effectGain, effectGain, 1, 0, 1f);
        if (stream == 0) {
            lastError = "effect play failed";
            return;
        }
        effectStreams.add(stream);
        if (effectStreams.size() > TRACKED_EFFECT_STREAMS) {
            effectStreams.remove(0);
        }
        if (superpose) {
            return;
        }
        activeEffects.put(path, stream);
        int duration = durationMs(path);
        if (duration <= 0) {
            activeEffects.remove(path);
            return;
        }
        handler.postDelayed(() -> {
            Integer current = activeEffects.get(path);
            if (current != null && current == stream) {
                activeEffects.remove(path);
            }
        }, duration);
    }

    private static int durationMs(String path) {
        Integer cached = effectDurations.get(path);
        if (cached != null) {
            return cached;
        }
        int duration = 0;
        MediaMetadataRetriever retriever = new MediaMetadataRetriever();
        try {
            retriever.setDataSource(path);
            String value = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);
            if (value != null) {
                duration = Integer.parseInt(value);
            }
        } catch (RuntimeException failure) {
            duration = 0;
        } finally {
            try {
                retriever.release();
            } catch (IOException ignored) {
                // Duration already parsed; releasing the retriever is separate.
            }
        }
        effectDurations.put(path, duration);
        return duration;
    }

    private static void playVoice(String path, boolean superpose) {
        int slot = selectVoiceSlot(path, superpose);
        if (slot < 0) {
            return;
        }
        MediaPlayer player = voices[slot];
        if (player == null) {
            player = new MediaPlayer();
            voices[slot] = player;
        }
        final int generation = ++voiceGeneration[slot];
        final int index = slot;
        try {
            armPlayer(player, AudioAttributes.CONTENT_TYPE_SPEECH, path);
            player.setVolume(voiceGain, voiceGain);
            player.setOnCompletionListener(finished -> {
                if (voiceGeneration[index] != generation) {
                    return;
                }
                voiceActive[index] = false;
                voicePaused[index] = false;
            });
            player.start();
            voicePaths[index] = path;
            voiceActive[index] = true;
            voicePaused[index] = false;
            voiceStarted[index] = android.os.SystemClock.uptimeMillis();
        } catch (IOException | IllegalStateException | IllegalArgumentException failure) {
            noteFailure(failure);
            player.reset();
            voiceActive[index] = false;
            voicePaths[index] = "";
        }
    }

    private static int selectVoiceSlot(String path, boolean superpose) {
        if (!superpose) {
            for (int i = 0; i < VOICE_SLOTS; ++i) {
                if (voiceActive[i] && path.equals(voicePaths[i])) {
                    return -1;
                }
            }
        }
        for (int i = 0; i < VOICE_SLOTS; ++i) {
            if (!voiceActive[i]) {
                return i;
            }
        }
        int oldest = 0;
        for (int i = 1; i < VOICE_SLOTS; ++i) {
            if (voiceStarted[i] < voiceStarted[oldest]) {
                oldest = i;
            }
        }
        return oldest;
    }

    private static void ensureBgmPlayer() {
        if (bgmPlayer == null) {
            bgmPlayer = new MediaPlayer();
        }
    }

    private static void armPlayer(MediaPlayer player, int contentType, String path) throws IOException {
        player.reset();
        player.setAudioAttributes(new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(contentType)
                .build());
        player.setDataSource(path);
        player.prepare();
    }

    private static boolean playing(MediaPlayer player) {
        try {
            return player.isPlaying();
        } catch (IllegalStateException failure) {
            noteFailure(failure);
            return false;
        }
    }

    private static void pauseVoices() {
        for (int i = 0; i < VOICE_SLOTS; ++i) {
            MediaPlayer player = voices[i];
            if (player == null || !voiceActive[i] || !playing(player)) {
                continue;
            }
            try {
                player.pause();
                voicePaused[i] = true;
            } catch (IllegalStateException failure) {
                noteFailure(failure);
                voiceActive[i] = false;
            }
        }
    }

    private static void resumeVoices() {
        for (int i = 0; i < VOICE_SLOTS; ++i) {
            if (!voicePaused[i] || voices[i] == null) {
                continue;
            }
            voicePaused[i] = false;
            try {
                voices[i].start();
            } catch (IllegalStateException failure) {
                noteFailure(failure);
                voiceActive[i] = false;
            }
        }
    }

    private static void stopAllOnAudioThread() {
        activeEffects.clear();
        playWhenLoaded.clear();
        playSuperpose.clear();
        if (effects != null) {
            for (int stream : effectStreams) {
                effects.stop(stream);
            }
        }
        effectStreams.clear();
        for (int i = 0; i < VOICE_SLOTS; ++i) {
            voiceActive[i] = false;
            voicePaused[i] = false;
            voicePaths[i] = "";
            if (voices[i] != null) {
                voices[i].reset();
            }
        }
        bgmActive = false;
        bgmPaused = false;
        bgmPath = "";
        if (bgmPlayer != null) {
            bgmPlayer.reset();
        }
    }

    private static void releaseOnAudioThread() {
        stopAllOnAudioThread();
        if (effects != null) {
            effects.release();
            effects = null;
        }
        effectSamples.clear();
        loadingSamples.clear();
        effectDurations.clear();
        for (int i = 0; i < VOICE_SLOTS; ++i) {
            if (voices[i] != null) {
                voices[i].release();
                voices[i] = null;
            }
        }
        if (bgmPlayer != null) {
            bgmPlayer.release();
            bgmPlayer = null;
        }
        hasOutput = false;
    }

    private static float clamp(float volume) {
        if (volume < 0f) {
            return 0f;
        }
        return Math.min(volume, 1f);
    }

    private static void noteFailure(Exception failure) {
        String message = failure.getMessage();
        lastError = message == null || message.isEmpty() ? failure.getClass().getSimpleName() : message;
    }
}
