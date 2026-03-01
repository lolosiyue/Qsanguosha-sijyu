/******************************************************************************
 * spine-cpp AnimationStateData implementation
 *****************************************************************************/

#include <spine/AnimationStateData.h>

namespace spine {

AnimationStateData::AnimationStateData(SkeletonData *skeletonData)
    : _skeletonData(skeletonData), _defaultMix(0)
{
}

void AnimationStateData::setMix(const String &fromName, const String &toName, float duration) {
    Animation *from = _skeletonData->findAnimation(fromName);
    Animation *to = _skeletonData->findAnimation(toName);
    if (from && to)
        setMix(from, to, duration);
}

void AnimationStateData::setMix(Animation *from, Animation *to, float duration) {
    AnimPairKey key;
    key.from = from;
    key.to = to;
    _mixMap[key] = duration;
}

float AnimationStateData::getMix(Animation *from, Animation *to) {
    if (!from || !to) return _defaultMix;
    AnimPairKey key;
    key.from = from;
    key.to = to;
    auto it = _mixMap.find(key);
    return it != _mixMap.end() ? it->second : _defaultMix;
}

} // namespace spine
