#ifndef SPINE_SLOT_DATA_H
#define SPINE_SLOT_DATA_H

#include <spine/SpineString.h>
#include <spine/Color.h>
#include <spine/BoneData.h>

namespace spine {

enum BlendMode {
    BlendMode_Normal,
    BlendMode_Additive,
    BlendMode_Multiply,
    BlendMode_Screen
};

class SlotData {
public:
    int _index;
    String _name;
    BoneData &_boneData;
    Color _color;
    Color _darkColor;
    bool _hasDarkColor;
    String _attachmentName;
    BlendMode _blendMode;

    SlotData(int index, const String &name, BoneData &boneData)
        : _index(index), _name(name), _boneData(boneData),
          _hasDarkColor(false), _blendMode(BlendMode_Normal) {}

    int getIndex() const { return _index; }
    const String &getName() const { return _name; }
    BoneData &getBoneData() { return _boneData; }
    BlendMode getBlendMode() const { return _blendMode; }
};

} // namespace spine

#endif // SPINE_SLOT_DATA_H
