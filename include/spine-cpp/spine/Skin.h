#ifndef SPINE_SKIN_H
#define SPINE_SKIN_H

#include <spine/SpineString.h>
#include <spine/Attachment.h>
#include <map>

namespace spine {

class Skeleton;

class Skin {
public:
    struct AttachmentKey {
        int slotIndex;
        String name;
        bool operator<(const AttachmentKey &o) const {
            if (slotIndex != o.slotIndex) return slotIndex < o.slotIndex;
            return name < o.name;
        }
    };

    Skin(const String &name) : _name(name) {}
    ~Skin();

    const String &getName() const { return _name; }

    void setAttachment(int slotIndex, const String &name, Attachment *attachment);
    Attachment *getAttachment(int slotIndex, const String &name);

    void attachAll(Skeleton &skeleton, Skin &oldSkin);

private:
    String _name;
    std::map<AttachmentKey, Attachment *> _attachments;
};

} // namespace spine

#endif // SPINE_SKIN_H
