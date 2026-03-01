/******************************************************************************
 * spine-cpp AnimationState implementation
 *****************************************************************************/

#include <spine/AnimationState.h>
#include <spine/Skeleton.h>
#include <cmath>

namespace spine {

// ─── TrackEntry ─────────────────────────────────────────────────────────────

TrackEntry::TrackEntry()
    : _trackIndex(0), _animation(nullptr), _loop(false), _holdPrevious(false),
      _eventThreshold(0), _attachmentThreshold(0), _drawOrderThreshold(0),
      _animationStart(0), _animationEnd(0), _animationLast(-1), _nextAnimationLast(-1),
      _delay(0), _trackTime(0), _trackLast(-1), _nextTrackLast(-1), _trackEnd(FLT_MAX),
      _timeScale(1), _alpha(1), _mixTime(0), _mixDuration(0),
      _interruptAlpha(0), _totalAlpha(0), _mixBlend(MixBlend_Replace),
      _mixingFrom(nullptr), _mixingTo(nullptr), _next(nullptr)
{
}

bool TrackEntry::isComplete() const {
    return _trackTime >= _animationEnd - _animationStart;
}

// ─── AnimationState ─────────────────────────────────────────────────────────

AnimationState::AnimationState(AnimationStateData *data)
    : _data(data), _timeScale(1)
{
}

AnimationState::~AnimationState() {
    clearTracks();
}

void AnimationState::update(float delta) {
    delta *= _timeScale;
    for (size_t i = 0; i < _tracks.size(); ++i) {
        TrackEntry *current = _tracks[i];
        if (!current) continue;

        current->_animationLast = current->_nextAnimationLast;
        current->_trackLast = current->_nextTrackLast;

        float currentDelta = delta * current->_timeScale;
        current->_trackTime += currentDelta;
        current->_nextTrackLast = current->_trackTime;

        if (current->_mixingFrom) {
            current->_mixTime += currentDelta;
        }
    }
}

bool AnimationState::apply(Skeleton &skeleton) {
    bool applied = false;
    for (size_t i = 0; i < _tracks.size(); ++i) {
        TrackEntry *current = _tracks[i];
        if (!current) continue;

        float animationLast = current->_animationLast;
        float animationTime = current->_trackTime;
        float alpha = current->_alpha;

        if (current->_mixingFrom) {
            alpha *= std::min(1.0f, current->_mixTime / current->_mixDuration);
        }

        /* For the first apply (animationLast == -1), use MixBlend_Setup
           so that attachment timelines and draw-order timelines fire correctly.
           After that, use the track's configured mixBlend. */
        MixBlend blend = (animationLast < 0) ? MixBlend_Setup : current->_mixBlend;

        current->_animation->apply(skeleton, animationLast, animationTime,
                                    current->_loop, &_events, alpha,
                                    blend, MixDirection_In);

        current->_nextAnimationLast = animationTime;

        // Fire events
        for (size_t e = 0; e < _events.size(); ++e) {
            if (current->listener)
                current->listener(EventType_Event, *current, _events[e]);
            if (listener)
                listener(EventType_Event, *current, _events[e]);
        }
        _events.clear();

        // Check completion (require duration > 0 to avoid false completion on static poses)
        float duration = current->_animation->getDuration();
        if (current->_loop) {
            if (duration > 0 && animationLast > 0) {
                float prevLoops = std::floor(animationLast / duration);
                float curLoops = std::floor(animationTime / duration);
                if (curLoops > prevLoops) {
                    if (current->listener)
                        current->listener(EventType_Complete, *current, nullptr);
                    if (listener)
                        listener(EventType_Complete, *current, nullptr);
                }
            }
        } else if (duration > 0 && animationTime >= duration && animationLast < duration) {
            if (current->listener)
                current->listener(EventType_Complete, *current, nullptr);
            if (listener)
                listener(EventType_Complete, *current, nullptr);
        }

        applied = true;
    }
    return applied;
}

TrackEntry *AnimationState::setAnimation(int trackIndex, const String &animationName, bool loop) {
    Animation *animation = _data->getSkeletonData()->findAnimation(animationName);
    if (!animation) return nullptr;
    return setAnimation(trackIndex, animation, loop);
}

TrackEntry *AnimationState::setAnimation(int trackIndex, Animation *animation, bool loop) {
    TrackEntry *current = expandToIndex(trackIndex);
    if (current) {
        // Notify end
        if (current->listener)
            current->listener(EventType_End, *current, nullptr);
        if (listener)
            listener(EventType_End, *current, nullptr);
    }

    TrackEntry *entry = new TrackEntry();
    entry->_trackIndex = trackIndex;
    entry->_animation = animation;
    entry->_loop = loop;
    entry->_trackEnd = FLT_MAX;
    entry->_animationStart = 0;
    entry->_animationEnd = animation->getDuration();
    entry->_animationLast = -1;
    entry->_nextAnimationLast = -1;
    entry->_trackTime = 0;
    entry->_trackLast = -1;
    entry->_nextTrackLast = -1;
    entry->_alpha = 1;
    entry->_mixTime = 0;
    entry->_mixDuration = current ? _data->getDefaultMix() : 0;
    entry->_timeScale = 1;
    entry->_mixBlend = MixBlend_Replace;
    entry->_mixingFrom = current;

    if (current) {
        current->_mixingTo = entry;
    }

    _tracks[trackIndex] = entry;

    if (entry->listener)
        entry->listener(EventType_Start, *entry, nullptr);
    if (listener)
        listener(EventType_Start, *entry, nullptr);

    return entry;
}

TrackEntry *AnimationState::addAnimation(int trackIndex, const String &animationName, bool loop, float delay) {
    Animation *animation = _data->getSkeletonData()->findAnimation(animationName);
    if (!animation) return nullptr;
    return addAnimation(trackIndex, animation, loop, delay);
}

TrackEntry *AnimationState::addAnimation(int trackIndex, Animation *animation, bool loop, float delay) {
    TrackEntry *last = expandToIndex(trackIndex);
    if (last) {
        while (last->_next) last = last->_next;
    }

    TrackEntry *entry = new TrackEntry();
    entry->_trackIndex = trackIndex;
    entry->_animation = animation;
    entry->_loop = loop;
    entry->_animationStart = 0;
    entry->_animationEnd = animation->getDuration();
    entry->_timeScale = 1;
    entry->_alpha = 1;
    entry->_mixBlend = MixBlend_Replace;

    if (last) {
        last->_next = entry;
        if (delay <= 0) {
            float duration = last->_animation ? last->_animation->getDuration() : 0;
            entry->_delay = duration - _data->getMix(last->_animation, animation) + delay;
        } else {
            entry->_delay = delay;
        }
    } else {
        _tracks[trackIndex] = entry;
    }

    return entry;
}

TrackEntry *AnimationState::setEmptyAnimation(int trackIndex, float mixDuration) {
    // Simplified - just clear the track
    clearTrack(trackIndex);
    return nullptr;
}

void AnimationState::addEmptyAnimation(int trackIndex, float mixDuration, float delay) {
    // Simplified
    (void)trackIndex; (void)mixDuration; (void)delay;
}

void AnimationState::clearTracks() {
    for (size_t i = 0; i < _tracks.size(); ++i) {
        clearTrack(static_cast<int>(i));
    }
    _tracks.clear();
}

void AnimationState::clearTrack(int trackIndex) {
    if (trackIndex >= static_cast<int>(_tracks.size())) return;
    TrackEntry *current = _tracks[trackIndex];
    if (!current) return;

    if (current->listener)
        current->listener(EventType_End, *current, nullptr);
    if (listener)
        listener(EventType_End, *current, nullptr);

    // Delete chain
    TrackEntry *from = current->_mixingFrom;
    while (from) {
        TrackEntry *prev = from->_mixingFrom;
        delete from;
        from = prev;
    }

    TrackEntry *next = current->_next;
    while (next) {
        TrackEntry *n = next->_next;
        delete next;
        next = n;
    }

    delete current;
    _tracks[trackIndex] = nullptr;
}

TrackEntry *AnimationState::getCurrent(int trackIndex) {
    if (trackIndex >= static_cast<int>(_tracks.size())) return nullptr;
    return _tracks[trackIndex];
}

TrackEntry *AnimationState::expandToIndex(int index) {
    if (index < static_cast<int>(_tracks.size()))
        return _tracks[index];
    while (static_cast<int>(_tracks.size()) <= index)
        _tracks.add(nullptr);
    return nullptr;
}

} // namespace spine
