#ifndef SPINE_BONE_DATA_H
#define SPINE_BONE_DATA_H

#include <spine/SpineString.h>
#include <spine/Color.h>

namespace spine {

enum TransformMode {
    TransformMode_Normal = 0,
    TransformMode_OnlyTranslation = 1,
    TransformMode_NoRotationOrReflection = 2,
    TransformMode_NoScale = 3,
    TransformMode_NoScaleOrReflection = 4
};

class BoneData {
public:
    int _index;
    String _name;
    BoneData *_parent;
    float _length;
    float _x, _y;
    float _rotation;
    float _scaleX, _scaleY;
    float _shearX, _shearY;
    TransformMode _transformMode;
    bool _skinRequired;
    Color _color;

    BoneData(int index, const String &name, BoneData *parent)
        : _index(index), _name(name), _parent(parent),
          _length(0), _x(0), _y(0), _rotation(0),
          _scaleX(1), _scaleY(1), _shearX(0), _shearY(0),
          _transformMode(TransformMode_Normal),
          _skinRequired(false) {}

    int getIndex() const { return _index; }
    const String &getName() const { return _name; }
    BoneData *getParent() { return _parent; }
};

} // namespace spine

#endif // SPINE_BONE_DATA_H
