#ifndef SPINE_ANIMATION_STATE_H
#define SPINE_ANIMATION_STATE_H

#include <spine/AnimationStateData.h>
#include <spine/Animation.h>
#include <spine/EventData.h>
#include <spine/Vector.h>
#include <functional>

namespace spine {

class Skeleton;

enum EventType {
    EventType_Start,
    EventType_Interrupt,
    EventType_End,
    EventType_Complete,
    EventType_Dispose,
    EventType_Event
};

class TrackEntry {
    friend class AnimationState;
public:
    TrackEntry();

    int getTrackIndex() const { return _trackIndex; }
    Animation *getAnimation() { return _animation; }
    bool getLoop() const { return _loop; }
    void setLoop(bool loop) { _loop = loop; }

    float getAnimationStart() const { return _animationStart; }
    void setAnimationStart(float t) { _animationStart = t; }
    float getAnimationEnd() const { return _animationEnd; }
    void setAnimationEnd(float t) { _animationEnd = t; }
    float getAnimationLast() const { return _animationLast; }

    float getTrackTime() const { return _trackTime; }
    void setTrackTime(float t) { _trackTime = t; }
    float getTrackEnd() const { return _trackEnd; }
    void setTrackEnd(float t) { _trackEnd = t; }

    float getAlpha() const { return _alpha; }
    void setAlpha(float a) { _alpha = a; }
    float getMixTime() const { return _mixTime; }
    float getMixDuration() const { return _mixDuration; }
    void setMixDuration(float d) { _mixDuration = d; }

    float getTimeScale() const { return _timeScale; }
    void setTimeScale(float s) { _timeScale = s; }

    TrackEntry *getMixingFrom() { return _mixingFrom; }
    TrackEntry *getNext() { return _next; }

    bool isComplete() const;

    /// Listener callback type
    typedef std::function<void(EventType type, TrackEntry &entry, Event *event)> Listener;
    Listener listener;

private:
    int _trackIndex;
    Animation *_animation;
    bool _loop;
    bool _holdPrevious;
    float _eventThreshold;
    float _attachmentThreshold;
    float _drawOrderThreshold;

    float _animationStart;
    float _animationEnd;
    float _animationLast;
    float _nextAnimationLast;

    float _delay;
    float _trackTime;
    float _trackLast;
    float _nextTrackLast;
    float _trackEnd;

    float _timeScale;
    float _alpha;
    float _mixTime;
    float _mixDuration;

    float _interruptAlpha;
    float _totalAlpha;
    MixBlend _mixBlend;

    TrackEntry *_mixingFrom;
    TrackEntry *_mixingTo;
    TrackEntry *_next;

    Vector<int> _timelineMode;
    Vector<TrackEntry *> _timelineHoldMix;
};

class AnimationState {
public:
    AnimationState(AnimationStateData *data);
    ~AnimationState();

    /// Advance time and apply.
    void update(float delta);
    bool apply(Skeleton &skeleton);

    /// Set animation on a track.
    TrackEntry *setAnimation(int trackIndex, const String &animationName, bool loop);
    TrackEntry *setAnimation(int trackIndex, Animation *animation, bool loop);

    /// Add animation on a track (queued).
    TrackEntry *addAnimation(int trackIndex, const String &animationName, bool loop, float delay);
    TrackEntry *addAnimation(int trackIndex, Animation *animation, bool loop, float delay);

    /// Set empty animation on track.
    TrackEntry *setEmptyAnimation(int trackIndex, float mixDuration);
    void addEmptyAnimation(int trackIndex, float mixDuration, float delay);

    /// Clear all tracks.
    void clearTracks();
    void clearTrack(int trackIndex);

    TrackEntry *getCurrent(int trackIndex);

    AnimationStateData *getData() { return _data; }

    float getTimeScale() const { return _timeScale; }
    void setTimeScale(float s) { _timeScale = s; }

    /// Global listener
    typedef std::function<void(EventType type, TrackEntry &entry, Event *event)> Listener;
    Listener listener;

private:
    AnimationStateData *_data;
    Vector<TrackEntry *> _tracks;
    Vector<Event *> _events;
    float _timeScale;

    TrackEntry *expandToIndex(int index);
    TrackEntry *newTrackEntry(int trackIndex, Animation *animation, bool loop, TrackEntry *last);
    void animationsChanged();
    void setCurrent(int index, TrackEntry *current, bool interrupt);
    void disposeNext(TrackEntry *entry);
};

} // namespace spine

#endif // SPINE_ANIMATION_STATE_H
