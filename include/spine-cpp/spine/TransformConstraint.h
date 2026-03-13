#ifndef SPINE_TRANSFORM_CONSTRAINT_H
#define SPINE_TRANSFORM_CONSTRAINT_H

#include <spine/TransformConstraintData.h>
#include <spine/Vector.h>

namespace spine {

class Bone;
class Skeleton;

class TransformConstraint {
public:
    TransformConstraint(TransformConstraintData &data, Skeleton &skeleton);

    void update();

    TransformConstraintData &getData() { return _data; }
    int getOrder() const { return _data._order; }

    TransformConstraintData &_data;
    Vector<Bone *> _bones;
    Bone *_target;
    float _rotateMix, _translateMix, _scaleMix, _shearMix;

    bool _active;

private:
    Skeleton &_skeleton;

    void applyAbsoluteWorld();
    void applyRelativeWorld();
    void applyAbsoluteLocal();
    void applyRelativeLocal();
};

} // namespace spine

#endif // SPINE_TRANSFORM_CONSTRAINT_H
