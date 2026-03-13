#ifndef SPINE_IK_CONSTRAINT_H
#define SPINE_IK_CONSTRAINT_H

#include <spine/IkConstraintData.h>
#include <spine/Vector.h>

namespace spine {

class Bone;
class Skeleton;

class IkConstraint {
public:
    IkConstraint(IkConstraintData &data, Skeleton &skeleton);

    void update();

    /// 1-bone IK solver
    static void apply(Bone &bone, float targetX, float targetY,
                      bool compress, bool stretch, bool uniform, float alpha);

    /// 2-bone IK solver
    static void apply(Bone &parent, Bone &child, float targetX, float targetY,
                      int bendDir, bool stretch, float softness, float alpha);

    IkConstraintData &getData() { return _data; }
    int getOrder() const { return _data._order; }

    IkConstraintData &_data;
    Vector<Bone *> _bones;
    Bone *_target;
    float _mix;
    float _softness;
    int _bendDirection;
    bool _compress;
    bool _stretch;

    bool _active;
};

} // namespace spine

#endif // SPINE_IK_CONSTRAINT_H
