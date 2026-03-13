#ifndef SPINE_ANIMATION_STATE_DATA_H
#define SPINE_ANIMATION_STATE_DATA_H

#include <spine/SkeletonData.h>
#include <spine/Animation.h>
#include <map>

namespace spine {

class AnimationStateData {
public:
    AnimationStateData(SkeletonData *skeletonData);

    SkeletonData *getSkeletonData() { return _skeletonData; }

    float getDefaultMix() const { return _defaultMix; }
    void setDefaultMix(float mix) { _defaultMix = mix; }

    void setMix(const String &fromName, const String &toName, float duration);
    void setMix(Animation *from, Animation *to, float duration);
    float getMix(Animation *from, Animation *to);

private:
    struct AnimPairKey {
        Animation *from;
        Animation *to;
        bool operator<(const AnimPairKey &o) const {
            if (from != o.from) return from < o.from;
            return to < o.to;
        }
    };
    SkeletonData *_skeletonData;
    float _defaultMix;
    std::map<AnimPairKey, float> _mixMap;
};

} // namespace spine

#endif // SPINE_ANIMATION_STATE_DATA_H
