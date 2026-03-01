/******************************************************************************
 * spine-cpp SkeletonData implementation
 *****************************************************************************/

#include <spine/SkeletonData.h>
#include <spine/Animation.h>
#include <spine/Skin.h>

namespace spine {

SkeletonData::SkeletonData()
    : _defaultSkin(nullptr), _x(0), _y(0), _width(0), _height(0), _fps(30)
{
}

SkeletonData::~SkeletonData() {
    for (size_t i = 0; i < _bones.size(); ++i) delete _bones[i];
    for (size_t i = 0; i < _slots.size(); ++i) delete _slots[i];
    for (size_t i = 0; i < _skins.size(); ++i) delete _skins[i];
    for (size_t i = 0; i < _animations.size(); ++i) delete _animations[i];
    for (size_t i = 0; i < _ikConstraints.size(); ++i) delete _ikConstraints[i];
    for (size_t i = 0; i < _transformConstraints.size(); ++i) delete _transformConstraints[i];
}

BoneData *SkeletonData::findBone(const String &name) {
    for (size_t i = 0; i < _bones.size(); ++i)
        if (_bones[i]->_name == name) return _bones[i];
    return nullptr;
}

SlotData *SkeletonData::findSlot(const String &name) {
    for (size_t i = 0; i < _slots.size(); ++i)
        if (_slots[i]->_name == name) return _slots[i];
    return nullptr;
}

Animation *SkeletonData::findAnimation(const String &name) {
    for (size_t i = 0; i < _animations.size(); ++i)
        if (_animations[i]->getName() == name) return _animations[i];
    return nullptr;
}

IkConstraintData *SkeletonData::findIkConstraint(const String &name) {
    for (size_t i = 0; i < _ikConstraints.size(); ++i)
        if (_ikConstraints[i]->_name == name) return _ikConstraints[i];
    return nullptr;
}

TransformConstraintData *SkeletonData::findTransformConstraint(const String &name) {
    for (size_t i = 0; i < _transformConstraints.size(); ++i)
        if (_transformConstraints[i]->_name == name) return _transformConstraints[i];
    return nullptr;
}

} // namespace spine
