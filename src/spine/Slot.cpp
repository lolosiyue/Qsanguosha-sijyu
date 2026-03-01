/******************************************************************************
 * spine-cpp Slot implementation
 *****************************************************************************/

#include <spine/Slot.h>
#include <spine/Skeleton.h>

namespace spine {

Slot::Slot(SlotData &data, Bone &bone)
    : _data(data), _bone(bone), _attachment(nullptr)
{
    _color.set(data._color.r, data._color.g, data._color.b, data._color.a);
    if (data._hasDarkColor)
        _darkColor.set(data._darkColor.r, data._darkColor.g, data._darkColor.b, data._darkColor.a);
}

Skeleton &Slot::getSkeleton() {
    return _bone.getSkeleton();
}

} // namespace spine
