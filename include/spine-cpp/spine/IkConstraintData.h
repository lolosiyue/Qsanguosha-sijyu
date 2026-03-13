#ifndef SPINE_IK_CONSTRAINT_DATA_H
#define SPINE_IK_CONSTRAINT_DATA_H

#include <spine/SpineString.h>
#include <spine/Vector.h>

namespace spine {

class BoneData;

class IkConstraintData {
public:
    IkConstraintData(const String &name) : _name(name), _order(0), _target(nullptr),
        _mix(1), _softness(0), _bendDirection(1),
        _compress(false), _stretch(false), _uniform(false) {}

    const String &getName() const { return _name; }
    int getOrder() const { return _order; }

    String _name;
    int _order;
    Vector<BoneData *> _bones;
    BoneData *_target;
    float _mix;
    float _softness;
    int _bendDirection;
    bool _compress;
    bool _stretch;
    bool _uniform;
};

} // namespace spine

#endif // SPINE_IK_CONSTRAINT_DATA_H
