#ifndef SPINE_ATTACHMENT_H
#define SPINE_ATTACHMENT_H

#include <spine/SpineString.h>
#include <spine/Color.h>
#include <spine/Vector.h>

namespace spine {

enum AttachmentType {
    AttachmentType_Region,
    AttachmentType_BoundingBox,
    AttachmentType_Mesh,
    AttachmentType_LinkedMesh,
    AttachmentType_Path,
    AttachmentType_Point,
    AttachmentType_Clipping
};

class Attachment {
public:
    Attachment(const String &name, AttachmentType type)
        : _name(name), _type(type) {}
    virtual ~Attachment() {}

    const String &getName() const { return _name; }
    AttachmentType getType() const { return _type; }

protected:
    String _name;
    AttachmentType _type;
};

} // namespace spine

#endif // SPINE_ATTACHMENT_H
