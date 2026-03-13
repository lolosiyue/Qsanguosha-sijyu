#ifndef SPINE_TRANSFORM_CONSTRAINT_DATA_H
#define SPINE_TRANSFORM_CONSTRAINT_DATA_H

#include <spine/SpineString.h>
#include <spine/Vector.h>

namespace spine {

class BoneData;

class TransformConstraintData {
public:
    TransformConstraintData(const String &name) : _name(name), _order(0), _target(nullptr),
        _local(false), _relative(false),
        _offsetRotation(0), _offsetX(0), _offsetY(0),
        _offsetScaleX(0), _offsetScaleY(0), _offsetShearY(0),
        _rotateMix(0), _translateMix(0), _scaleMix(0), _shearMix(0) {}

    const String &getName() const { return _name; }
    int getOrder() const { return _order; }

    String _name;
    int _order;
    Vector<BoneData *> _bones;
    BoneData *_target;
    bool _local;
    bool _relative;
    float _offsetRotation;
    float _offsetX, _offsetY;
    float _offsetScaleX, _offsetScaleY;
    float _offsetShearY;
    float _rotateMix;
    float _translateMix;
    float _scaleMix;
    float _shearMix;
};

} // namespace spine

#endif // SPINE_TRANSFORM_CONSTRAINT_DATA_H
