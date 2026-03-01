/******************************************************************************
 * spine-cpp SkeletonBinary — dual-version parser
 *
 * Supports both Spine 3.6.x and 3.8+ / 4.x binary .skel formats.
 * Version is auto-detected from the header.
 *
 * Spine 3.6 vs 3.8+/4.x key differences:
 *   - 3.5/3.6/3.7 have NO string table; 3.8+ has one.
 *   - 3.5/3.6/3.7 root bone (i==0) has no parent; 3.8+ all bones have parent+1.
 *   - 3.5/3.6/3.7 bones have no skinRequired; 3.8+ do.
 *   - 3.6/3.7 slots have darkColor (readInt, -1=none); 3.5 does NOT.
 *   - All legacy slot attachment-name is inline string; 3.8+ uses string-ref.
 *   - 3.7 IK adds compress/stretch/uniform; 3.5/3.6 do not; 3.8+ adds softness.
 *   - All legacy constraints have no skinRequired; 3.8+ do.
 *   - All legacy non-default skins have no constraint arrays; 3.8+ do.
 *   - 3.7 events have audioPath/volume/balance; 3.5/3.6 do not.
 *   - 3.7 nonessential header has audioPath; 3.5/3.6 do not.
 *   - All legacy mesh triangles = shorts; 3.8+ = varints.
 *   - ALL versions use readBoolean() for vertex weighted flag.
 *   - 3.6/3.7 transform constraints have local/relative; 3.5 does NOT.
 *****************************************************************************/

#include <spine/SkeletonBinary.h>
#include <spine/Skin.h>
#include <spine/Animation.h>
#include <spine/RegionAttachment.h>
#include <spine/MeshAttachment.h>
#include <spine/EventData.h>
#include <spine/IkConstraintData.h>
#include <spine/TransformConstraintData.h>
#include <QDebug>
#include <fstream>
#include <cstring>
#include <cmath>

namespace spine {

static bool startsWithVersion(const String &v, const char *prefix) {
    return strncmp(v.buffer(), prefix, strlen(prefix)) == 0;
}

static bool isLegacy3xVersionText(const String &v) {
    return startsWithVersion(v, "3.5") || startsWithVersion(v, "3.6") || startsWithVersion(v, "3.7");
}

static const char *runtimeVersionName(SkeletonBinary::RuntimeVersion v) {
    switch (v) {
    case SkeletonBinary::Runtime3_5_35: return "3.5.35";
    case SkeletonBinary::Runtime3_7:    return "3.7";
    case SkeletonBinary::Runtime3_8:    return "3.8";
    case SkeletonBinary::Runtime4_0:    return "4.0";
    case SkeletonBinary::Runtime4_1:    return "4.1";
    default:                            return "auto";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   Construction
   ═══════════════════════════════════════════════════════════════════════════*/

SkeletonBinary::SkeletonBinary(Atlas *atlas)
    : _atlas(atlas), _scale(1.0f), _nonessential(false), _is36(false), _is35(false), _is37(false),
      _runtimeVersionHint(RuntimeAuto) {}

SkeletonBinary::~SkeletonBinary() {}

void SkeletonBinary::setRuntimeVersionHint(const String &versionText) {
    if (startsWithVersion(versionText, "3.5")) _runtimeVersionHint = Runtime3_5_35;
    else if (startsWithVersion(versionText, "3.7")) _runtimeVersionHint = Runtime3_7;
    else if (startsWithVersion(versionText, "3.8")) _runtimeVersionHint = Runtime3_8;
    else if (startsWithVersion(versionText, "4.0")) _runtimeVersionHint = Runtime4_0;
    else if (startsWithVersion(versionText, "4.1")) _runtimeVersionHint = Runtime4_1;
    else _runtimeVersionHint = RuntimeAuto;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Low-level binary readers
   ═══════════════════════════════════════════════════════════════════════════*/

unsigned char SkeletonBinary::readByte(DataInput &input) {
    if (input.cursor >= input.end) return 0;
    return *input.cursor++;
}

signed char SkeletonBinary::readSByte(DataInput &input) {
    return static_cast<signed char>(readByte(input));
}

bool SkeletonBinary::readBoolean(DataInput &input) {
    return readByte(input) != 0;
}

short SkeletonBinary::readShort(DataInput &input) {
    int hi = readByte(input);
    int lo = readByte(input);
    return static_cast<short>((hi << 8) | lo);
}

int SkeletonBinary::readInt(DataInput &input) {
    int b0 = readByte(input);
    int b1 = readByte(input);
    int b2 = readByte(input);
    int b3 = readByte(input);
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

int SkeletonBinary::readVarint(DataInput &input, bool optimizePositive) {
    unsigned char b = readByte(input);
    int value = b & 0x7F;
    if (b & 0x80) {
        b = readByte(input);
        value |= (b & 0x7F) << 7;
        if (b & 0x80) {
            b = readByte(input);
            value |= (b & 0x7F) << 14;
            if (b & 0x80) {
                b = readByte(input);
                value |= (b & 0x7F) << 21;
                if (b & 0x80) {
                    b = readByte(input);
                    value |= (b & 0x7F) << 28;
                }
            }
        }
    }
    if (!optimizePositive)
        value = (static_cast<unsigned int>(value) >> 1) ^ -(value & 1);
    return value;
}

float SkeletonBinary::readFloat(DataInput &input) {
    union { int i; float f; } u;
    u.i = readInt(input);
    return u.f;
}

String SkeletonBinary::readString(DataInput &input) {
    int byteCount = readVarint(input, true);
    if (byteCount == 0) return String("");
    if (byteCount < 0) {
        input.cursor = input.end;
        return String("");
    }
    ptrdiff_t remaining = input.end - input.cursor;
    if (byteCount > (1 << 20) || byteCount - 1 > remaining) {
        // Corrupted/shifted stream: stop further parsing safely.
        input.cursor = input.end;
        return String("");
    }
    byteCount--;
    if (byteCount == 0) return String("");
    char *buf = new char[byteCount + 1];
    for (int i = 0; i < byteCount && input.cursor < input.end; ++i)
        buf[i] = static_cast<char>(*input.cursor++);
    buf[byteCount] = '\0';
    String result(buf);
    delete[] buf;
    return result;
}

String SkeletonBinary::readStringRef(DataInput &input, Vector<String> &strings) {
    int index = readVarint(input, true);
    if (index == 0) return String("");
    index--;
    if (index >= 0 && index < static_cast<int>(strings.size()))
        return strings[index];
    return String("");
}

void SkeletonBinary::readColor(DataInput &input, float &r, float &g, float &b, float &a) {
    int rgba = readInt(input);
    r = ((rgba >> 24) & 0xFF) / 255.0f;
    g = ((rgba >> 16) & 0xFF) / 255.0f;
    b = ((rgba >>  8) & 0xFF) / 255.0f;
    a = ( rgba        & 0xFF) / 255.0f;
}

float SkeletonBinary::readColor(DataInput &input) {
    int rgba = readInt(input);
    return (rgba & 0xFF) / 255.0f;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Public entry points
   ═══════════════════════════════════════════════════════════════════════════*/

SkeletonData *SkeletonBinary::readSkeletonDataFile(const String &path) {
    std::ifstream file(path.buffer(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        _error = String("Could not open file: ") + path;
        return nullptr;
    }
    auto sz = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> buffer(static_cast<size_t>(sz));
    file.read(reinterpret_cast<char *>(buffer.data()), sz);
    file.close();
    return readSkeletonData(buffer.data(), static_cast<int>(buffer.size()));
}

SkeletonData *SkeletonBinary::readSkeletonData(const unsigned char *binary, int length) {
    DataInput input;
    input.cursor = binary;
    input.end    = binary + length;
    return internalRead(input);
}

/* ═══════════════════════════════════════════════════════════════════════════
   skipVertices  —  skip a vertex block (ALL versions use readBoolean)
   ═══════════════════════════════════════════════════════════════════════════*/

void SkeletonBinary::skipVertices(DataInput &input, int vertexCount, float /*scale*/) {
    bool weighted = readBoolean(input);
    if (!weighted) {
        /* non-weighted — vertexCount*2 floats */
        for (int i = 0; i < vertexCount * 2; ++i) readFloat(input);
    } else {
        /* weighted */
        for (int i = 0; i < vertexCount; ++i) {
            int bc = readVarint(input, true);
            for (int b = 0; b < bc; ++b) {
                readVarint(input, true); // boneIndex
                readFloat(input);        // x
                readFloat(input);        // y
                readFloat(input);        // weight
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   internalRead — version-detecting entry point
   ═══════════════════════════════════════════════════════════════════════════*/

SkeletonData *SkeletonBinary::internalRead(DataInput &input) {
    SkeletonData *sd = new SkeletonData();

    /* ── Header (same for all versions) ─────────────────────────────────── */
    sd->_hash    = readString(input);
    sd->_version = readString(input);
    sd->setWidth(readFloat(input));
    sd->setHeight(readFloat(input));
    sd->setX(0); sd->setY(0);

    /* Version routing (5-version support + auto detect)
       Runtime hint takes precedence; otherwise auto-detect by skeleton header version. */
    if (_runtimeVersionHint == Runtime3_5_35 || _runtimeVersionHint == Runtime3_7) {
        _is36 = true;
    } else if (_runtimeVersionHint == Runtime3_8 || _runtimeVersionHint == Runtime4_0 || _runtimeVersionHint == Runtime4_1) {
        _is36 = false;
    } else {
        _is36 = isLegacy3xVersionText(sd->_version);
    }

    /* Sub-version detection for 3.5 vs 3.6 vs 3.7 differences */
    _is35 = (_runtimeVersionHint == Runtime3_5_35) || startsWithVersion(sd->_version, "3.5");
    _is37 = (_runtimeVersionHint == Runtime3_7) || startsWithVersion(sd->_version, "3.7");

    _nonessential = readBoolean(input);
    if (_nonessential) {
        sd->_fps        = readFloat(input);
        sd->_imagesPath = readString(input);
        if (!_is36 || _is37)
            readString(input); /* audioPath — 3.7+ / 3.8+ */
    }

    qWarning("[SkelBin] header: hash='%s' ver='%s' size=%.0fx%.0f noness=%d is36=%d is35=%d is37=%d hint=%s",
             sd->_hash.buffer(), sd->_version.buffer(),
             sd->getWidth(), sd->getHeight(), (int)_nonessential, (int)_is36, (int)_is35, (int)_is37,
             runtimeVersionName(_runtimeVersionHint));

    /* ── String table (3.8+ only) ───────────────────────────────────────── */
    _strings.clear();
    if (!_is36) {
        int stringCount = readVarint(input, true);
        for (int i = 0; i < stringCount; ++i)
            _strings.add(readString(input));
        qWarning("[SkelBin] stringTable: %d entries", stringCount);
    }

    /* ── Bones ──────────────────────────────────────────────────────────── */
    int boneCount = readVarint(input, true);
    qWarning("[SkelBin] boneCount=%d", boneCount);
    for (int i = 0; i < boneCount; ++i) {
        String bname = readString(input);

        BoneData *parent = nullptr;
        int parentIdx = -1;
        if (_is36) {
            /* Spine 3.6: root bone (i==0) has NO parent field.
               Non-root bones store a direct 0-based parent index. */
            if (i > 0) {
                parentIdx = readVarint(input, true);
                if (parentIdx >= 0 && parentIdx < (int)sd->getBones().size())
                    parent = sd->getBones()[parentIdx];
            }
        } else {
            /* Spine 3.8+: all bones store parent+1 (0 = no parent). */
            parentIdx = readVarint(input, true) - 1;
            if (parentIdx >= 0 && parentIdx < (int)sd->getBones().size())
                parent = sd->getBones()[parentIdx];
        }

        BoneData *bd = new BoneData(i, bname, parent);
        bd->_rotation = readFloat(input);
        bd->_x        = readFloat(input) * _scale;
        bd->_y        = readFloat(input) * _scale;
        bd->_scaleX   = readFloat(input);
        bd->_scaleY   = readFloat(input);
        bd->_shearX   = readFloat(input);
        bd->_shearY   = readFloat(input);
        bd->_length   = readFloat(input) * _scale;

        int tmIdx = readVarint(input, true);
        bd->_transformMode = static_cast<TransformMode>(tmIdx);

        if (!_is36)
            bd->_skinRequired = readBoolean(input); /* 3.8+ only */

        if (_nonessential) readInt(input); /* bone colour RGBA */

        sd->getBones().add(bd);
        qWarning("[SkelBin]   bone[%d] '%s' parent=%d", i, bname.buffer(), parentIdx);
    }

    /* ── Slots ──────────────────────────────────────────────────────────── */
    int slotCount = readVarint(input, true);
    qWarning("[SkelBin] slotCount=%d", slotCount);
    for (int i = 0; i < slotCount; ++i) {
        String sname = readString(input);
        int boneIdx  = readVarint(input, true);
        BoneData *bd = (boneIdx >= 0 && boneIdx < (int)sd->getBones().size())
                           ? sd->getBones()[boneIdx] : nullptr;

        SlotData *sl = new SlotData(i, sname, *bd);
        readColor(input, sl->_color.r, sl->_color.g, sl->_color.b, sl->_color.a);

        /* Dark colour: 3.6+/3.7/3.8+ read readInt, -1 (0xFFFFFFFF) = none.
           3.5 does NOT have darkColor at all. */
        if (!_is35) {
            int darkColor = readInt(input);
            if (darkColor != -1) {
                sl->_hasDarkColor = true;
                sl->_darkColor.r = ((darkColor >> 16) & 0xFF) / 255.0f;
                sl->_darkColor.g = ((darkColor >>  8) & 0xFF) / 255.0f;
                sl->_darkColor.b = ( darkColor        & 0xFF) / 255.0f;
            }
        }

        /* attachment name */
        if (_is36)
            sl->_attachmentName = readString(input);
        else
            sl->_attachmentName = readStringRef(input, _strings);

        sl->_blendMode = static_cast<BlendMode>(readVarint(input, true));

        sd->getSlots().add(sl);
        qWarning("[SkelBin]   slot[%d] '%s' bone=%d attach='%s' blend=%d",
                 i, sname.buffer(), boneIdx,
                 sl->_attachmentName.buffer(), (int)sl->_blendMode);
    }

    /* ── IK constraints ─────────────────────────────────────────────────── */
    int ikCount = readVarint(input, true);
    qWarning("[SkelBin] ikCount=%d", ikCount);
    for (int i = 0; i < ikCount; ++i) {
        String ikName = readString(input);               // name
        int ikOrder = readVarint(input, true);            // order
        if (!_is36) readBoolean(input);                   // skinRequired (3.8+)
        IkConstraintData *ik = new IkConstraintData(ikName);
        ik->_order = ikOrder;
        int n = readVarint(input, true);                  // bone count
        for (int j = 0; j < n; ++j) {
            int boneIdx = readVarint(input, true);
            if (boneIdx < (int)sd->getBones().size())
                ik->_bones.add(sd->getBones()[boneIdx]);
        }
        int targetIdx = readVarint(input, true);          // target
        if (targetIdx < (int)sd->getBones().size())
            ik->_target = sd->getBones()[targetIdx];
        ik->_mix = readFloat(input);                      // mix
        if (!_is36) ik->_softness = readFloat(input);     // softness (3.8+)
        ik->_bendDirection = readSByte(input);            // bendDirection
        if (!_is36 || _is37) {
            ik->_compress = readBoolean(input);           // compress (3.7+ / 3.8+)
            ik->_stretch = readBoolean(input);            // stretch  (3.7+ / 3.8+)
            ik->_uniform = readBoolean(input);            // uniform  (3.7+ / 3.8+)
        }
        sd->getIkConstraints().add(ik);
    }

    /* ── Transform constraints ──────────────────────────────────────────── */
    int xfCount = readVarint(input, true);
    qWarning("[SkelBin] transformCount=%d", xfCount);
    for (int i = 0; i < xfCount; ++i) {
        String xfName = readString(input);                // name
        int xfOrder = readVarint(input, true);            // order
        if (!_is36) readBoolean(input);                   // skinRequired (3.8+)
        TransformConstraintData *xf = new TransformConstraintData(xfName);
        xf->_order = xfOrder;
        int n = readVarint(input, true);                  // bone count
        for (int j = 0; j < n; ++j) {
            int boneIdx = readVarint(input, true);
            if (boneIdx < (int)sd->getBones().size())
                xf->_bones.add(sd->getBones()[boneIdx]);
        }
        int targetIdx = readVarint(input, true);          // target
        if (targetIdx < (int)sd->getBones().size())
            xf->_target = sd->getBones()[targetIdx];
        /* local/relative: present in 3.6+/3.7+/3.8+, NOT in 3.5 */
        if (!_is35) {
            xf->_local = readBoolean(input);              // local
            xf->_relative = readBoolean(input);           // relative
        }
        xf->_offsetRotation = readFloat(input);           // offset rotation
        xf->_offsetX = readFloat(input);                  // offset x
        xf->_offsetY = readFloat(input);                  // offset y
        xf->_offsetScaleX = readFloat(input);             // offset scaleX
        xf->_offsetScaleY = readFloat(input);             // offset scaleY
        xf->_offsetShearY = readFloat(input);             // offset shearY
        xf->_rotateMix = readFloat(input);                // rotateMix
        xf->_translateMix = readFloat(input);             // translateMix
        xf->_scaleMix = readFloat(input);                 // scaleMix
        xf->_shearMix = readFloat(input);                 // shearMix
        sd->getTransformConstraints().add(xf);
    }

    /* ── Path constraints ───────────────────────────────────────────────── */
    int pcCount = readVarint(input, true);
    qWarning("[SkelBin] pathCount=%d", pcCount);
    for (int i = 0; i < pcCount; ++i) {
        readString(input);               // name
        readVarint(input, true);         // order
        if (!_is36) readBoolean(input);  // skinRequired (3.8+)
        int n = readVarint(input, true); // bone count
        for (int j = 0; j < n; ++j) readVarint(input, true);
        readVarint(input, true);         // target slot
        readVarint(input, true);         // positionMode
        readVarint(input, true);         // spacingMode
        readVarint(input, true);         // rotateMode
        readFloat(input);                // offset rotation
        readFloat(input);                // position
        readFloat(input);                // spacing
        readFloat(input);                // rotateMix
        readFloat(input);                // translateMix
    }

    /* ── Default skin ───────────────────────────────────────────────────── */
    Skin *defaultSkin = readSkin(input, sd, true);
    if (defaultSkin) {
        sd->setDefaultSkin(defaultSkin);
        qWarning("[SkelBin] defaultSkin loaded");
    } else {
        qWarning("[SkelBin] no defaultSkin");
    }

    /* ── Named skins ────────────────────────────────────────────────────── */
    int skinN = readVarint(input, true);
    qWarning("[SkelBin] namedSkinCount=%d", skinN);
    for (int i = 0; i < skinN; ++i)
        readSkin(input, sd, false);

    /* ── Events ─────────────────────────────────────────────────────────── */
    int eventN = readVarint(input, true);
    qWarning("[SkelBin] eventCount=%d", eventN);
    for (int i = 0; i < eventN; ++i) {
        String evName = readString(input);  // name
        readVarint(input, false);    // intValue
        readFloat(input);            // floatValue
        readString(input);           // stringValue
        if (!_is36 || _is37) {
            /* 3.7+ / 3.8+: audio fields */
            String audioPath = readString(input);   // audioPath
            if (!audioPath.isEmpty()) {
                readFloat(input);        // volume
                readFloat(input);        // balance
            }
        }
        qWarning("[SkelBin]   event[%d] '%s'", i, evName.buffer());
    }

    /* ── Animations ─────────────────────────────────────────────────────── */
    int animN = readVarint(input, true);
    qWarning("[SkelBin] animationCount=%d", animN);
    if (animN == 0) {
        qWarning("[SkelBin] WARNING: no animations parsed (ver='%s', is36=%d). File may use an unsupported binary layout.",
                 sd->_version.buffer(), (int)_is36);
    }
    for (int i = 0; i < animN; ++i) {
        String aname = _is36 ? readString(input)
                             : readStringRef(input, _strings);
        qWarning("[SkelBin]   reading anim '%s'", aname.buffer());
        Animation *anim = readAnimation(input, aname, sd);
        if (anim) sd->getAnimations().add(anim);
    }

    qWarning("[SkelBin] DONE: bones=%d slots=%d anims=%d",
             (int)sd->getBones().size(),
             (int)sd->getSlots().size(),
             (int)sd->getAnimations().size());

    return sd;
}

/* ═══════════════════════════════════════════════════════════════════════════
   readSkin — version-aware
   ═══════════════════════════════════════════════════════════════════════════*/

Skin *SkeletonBinary::readSkin(DataInput &input, SkeletonData *sd, bool defaultSkin) {
    String skinName;
    int slotN;

    if (defaultSkin) {
        slotN = readVarint(input, true);
        if (slotN == 0) return nullptr;
        skinName = String("default");
    } else {
        if (_is36) {
            skinName = readString(input);
        } else {
            skinName = readStringRef(input, _strings);
            /* 3.8+ non-default skins have constraint arrays */
            int nBones = readVarint(input, true);
            for (int j = 0; j < nBones; ++j) readVarint(input, true);
            int nIk = readVarint(input, true);
            for (int j = 0; j < nIk; ++j) readVarint(input, true);
            int nXf = readVarint(input, true);
            for (int j = 0; j < nXf; ++j) readVarint(input, true);
            int nPc = readVarint(input, true);
            for (int j = 0; j < nPc; ++j) readVarint(input, true);
        }
        slotN = readVarint(input, true);
    }

    qWarning("[SkelBin]   skin '%s' slotEntries=%d", skinName.buffer(), slotN);
    Skin *skin = new Skin(skinName);

    for (int si = 0; si < slotN; ++si) {
        int slotIndex = readVarint(input, true);
        int attachN   = readVarint(input, true);

        for (int ai = 0; ai < attachN; ++ai) {
            /* placeholder name */
            String placeholder = _is36 ? readString(input)
                                       : readStringRef(input, _strings);

            /* actual attachment name */
            String aname = _is36 ? readString(input)
                                 : readStringRef(input, _strings);
            if (aname.isEmpty()) aname = placeholder;

            int type = readByte(input);
            Attachment *att = nullptr;

            switch (type) {

            /* ── 0 : Region ──────────────────────────────────────────── */
            case 0: {
                String path = _is36 ? readString(input)
                                    : readStringRef(input, _strings);
                if (path.isEmpty()) path = aname;

                float rot = readFloat(input);
                float ax  = readFloat(input) * _scale;
                float ay  = readFloat(input) * _scale;
                float sx  = readFloat(input);
                float sy  = readFloat(input);
                float w   = readFloat(input) * _scale;
                float h   = readFloat(input) * _scale;
                float cr, cg, cb, ca;
                readColor(input, cr, cg, cb, ca);

                RegionAttachment *ra = new RegionAttachment(aname);
                ra->setX(ax);  ra->setY(ay);
                ra->setRotation(rot);
                ra->setScaleX(sx);  ra->setScaleY(sy);
                ra->setWidth(w);    ra->setHeight(h);
                ra->getColor().set(cr, cg, cb, ca);

                AtlasRegion *ar = _atlas ? _atlas->findRegion(path) : nullptr;
                if (!ar) ar = _atlas ? _atlas->findRegion(aname) : nullptr;
                ra->setRegion(ar);
                if (ar) ra->updateOffset();

                qWarning("[SkelBin]     region '%s' path='%s' atlas=%p %.0fx%.0f",
                         aname.buffer(), path.buffer(), ar, w/_scale, h/_scale);
                att = ra;
                break;
            }

            /* ── 1 : BoundingBox ─────────────────────────────────────── */
            case 1: {
                int vc = readVarint(input, true);
                skipVertices(input, vc, _scale);
                if (_nonessential) readColor(input);
                break;
            }

            /* ── 2 : Mesh ────────────────────────────────────────────── */
            case 2: {
                String path = _is36 ? readString(input)
                                    : readStringRef(input, _strings);
                if (path.isEmpty()) path = aname;

                float cr, cg, cb, ca;
                readColor(input, cr, cg, cb, ca);

                int vertexCount = readVarint(input, true);

                /* UVs: vertexCount*2 floats */
                Vector<float> uvs;
                for (int u = 0; u < vertexCount * 2; ++u)
                    uvs.add(readFloat(input));

                /* Triangles */
                int triN = readVarint(input, true);
                Vector<unsigned short> tris;
                if (_is36) {
                    for (int t = 0; t < triN; ++t)
                        tris.add(static_cast<unsigned short>(readShort(input)));
                } else {
                    for (int t = 0; t < triN; ++t)
                        tris.add(static_cast<unsigned short>(readVarint(input, true)));
                }

                /* Vertices — ALL versions use readBoolean() for weighted flag */
                Vector<float> verts;
                Vector<int> bones;
                bool weighted = false;
                {
                    weighted = readBoolean(input);
                    if (!weighted) {
                        for (int v = 0; v < vertexCount * 2; ++v)
                            verts.add(readFloat(input) * _scale);
                    } else {
                        for (int v = 0; v < vertexCount; ++v) {
                            int bc = readVarint(input, true);
                            bones.add(bc);
                            for (int b = 0; b < bc; ++b) {
                                bones.add(readVarint(input, true));
                                verts.add(readFloat(input) * _scale);
                                verts.add(readFloat(input) * _scale);
                                verts.add(readFloat(input));
                            }
                        }
                    }
                }

                int hullLen = readVarint(input, true);

                if (_nonessential) {
                    int edgeN = readVarint(input, true);
                    for (int e = 0; e < edgeN; ++e) {
                        if (_is36) readShort(input); else readVarint(input, true);
                    }
                    readFloat(input); /* width */
                    readFloat(input); /* height */
                }

                MeshAttachment *ma = new MeshAttachment(aname);
                ma->getRegionUVs()  = uvs;
                ma->getTriangles()  = tris;
                ma->setWeighted(weighted);
                ma->getVertices() = verts;
                if (weighted) ma->getBones() = bones;
                ma->setWorldVerticesLength(vertexCount * 2);
                ma->setHullLength(hullLen * 2);
                ma->getColor().set(cr, cg, cb, ca);

                AtlasRegion *ar = _atlas ? _atlas->findRegion(path) : nullptr;
                if (!ar) ar = _atlas ? _atlas->findRegion(aname) : nullptr;
                ma->setRegion(ar);
                if (ar) ma->updateUVs();

                qWarning("[SkelBin]     mesh '%s' verts=%d tris=%d wt=%d atlas=%p",
                         aname.buffer(), vertexCount, triN, (int)weighted, ar);
                att = ma;
                break;
            }

            /* ── 3 : LinkedMesh ──────────────────────────────────────── */
            case 3: {
                String lmPath;
                if (_is36) {
                    lmPath = readString(input);  // path
                } else {
                    lmPath = readStringRef(input, _strings); // path
                }
                int lmColor = readInt(input); // colour RGBA
                String lmSkin;
                String lmParent;
                if (_is36) {
                    lmSkin = readString(input);      // skin
                    lmParent = readString(input);    // parent
                } else {
                    lmSkin = readStringRef(input, _strings); // skin
                    lmParent = readStringRef(input, _strings); // parent
                }
                bool lmDeform = readBoolean(input);  // deform
                if (_nonessential) {
                    readFloat(input);   // width
                    readFloat(input);   // height
                }

                // Create a concrete mesh attachment by cloning the parent mesh data.
                // In this simplified runtime we resolve from current skin/default skin only.
                MeshAttachment *parentMesh = nullptr;
                if (!lmParent.isEmpty()) {
                    Attachment *fromCurrent = skin->getAttachment(slotIndex, lmParent);
                    if (fromCurrent && fromCurrent->getType() == spine::AttachmentType_Mesh)
                        parentMesh = static_cast<MeshAttachment *>(fromCurrent);
                    if (!parentMesh && sd && sd->getDefaultSkin()) {
                        Attachment *fromDefault = sd->getDefaultSkin()->getAttachment(slotIndex, lmParent);
                        if (fromDefault && fromDefault->getType() == spine::AttachmentType_Mesh)
                            parentMesh = static_cast<MeshAttachment *>(fromDefault);
                    }
                }

                if (parentMesh) {
                    MeshAttachment *ma = new MeshAttachment(aname);
                    ma->setWeighted(parentMesh->isWeighted());
                    ma->getBones() = parentMesh->getBones();
                    ma->getVertices() = parentMesh->getVertices();
                    ma->getTriangles() = parentMesh->getTriangles();
                    ma->getRegionUVs() = parentMesh->getRegionUVs();
                    ma->setWorldVerticesLength(parentMesh->getWorldVerticesLength());
                    ma->setHullLength(parentMesh->getHullLength());

                    AtlasRegion *ar = nullptr;
                    if (!lmPath.isEmpty()) ar = _atlas ? _atlas->findRegion(lmPath) : nullptr;
                    if (!ar) ar = parentMesh->getRegion();
                    ma->setRegion(ar);
                    if (ar) ma->updateUVs();

                    float r = ((lmColor >> 24) & 0xFF) / 255.0f;
                    float g = ((lmColor >> 16) & 0xFF) / 255.0f;
                    float b = ((lmColor >>  8) & 0xFF) / 255.0f;
                    float a = ( lmColor        & 0xFF) / 255.0f;
                    ma->getColor().set(r, g, b, a);

                    att = ma;
                    qWarning("[SkelBin]     linkedmesh '%s' path='%s' skin='%s' parent='%s' deform=%d RESOLVED=1",
                             aname.buffer(), lmPath.buffer(), lmSkin.buffer(), lmParent.buffer(), (int)lmDeform);
                } else {
                    qWarning("[SkelBin]     linkedmesh '%s' path='%s' skin='%s' parent='%s' deform=%d RESOLVED=0",
                             aname.buffer(), lmPath.buffer(), lmSkin.buffer(), lmParent.buffer(), (int)lmDeform);
                }
                break;
            }

            /* ── 4 : Path ────────────────────────────────────────────── */
            case 4: {
                readBoolean(input); // closed
                readBoolean(input); // constantSpeed
                int vc = readVarint(input, true);
                skipVertices(input, vc, _scale);
                int lenN = vc / 3;
                for (int l = 0; l < lenN; ++l) readFloat(input);
                if (_nonessential) readColor(input);
                break;
            }

            /* ── 5 : Point ───────────────────────────────────────────── */
            case 5: {
                readFloat(input);  // rotation
                readFloat(input);  // x
                readFloat(input);  // y
                if (_nonessential) readColor(input);
                break;
            }

            /* ── 6 : Clipping ────────────────────────────────────────── */
            case 6: {
                readVarint(input, true); // end slot
                int vc = readVarint(input, true);
                skipVertices(input, vc, _scale);
                if (_nonessential) readColor(input);
                break;
            }

            default:
                qWarning("[SkelBin]     UNKNOWN attachment type %d '%s'", type, aname.buffer());
                break;
            }

            if (att) {
                skin->setAttachment(slotIndex, placeholder, att);
                if (!aname.isEmpty() && !(aname == placeholder)) {
                    skin->setAttachment(slotIndex, aname, att);
                }
            }
        }
    }

    return skin;
}

/* ═══════════════════════════════════════════════════════════════════════════
   readAnimation — version-aware
   ═══════════════════════════════════════════════════════════════════════════*/

Animation *SkeletonBinary::readAnimation(DataInput &input, const String &name,
                                          SkeletonData *sd) {
    Vector<Timeline *> timelines;
    float duration = 0;
    auto updateDur = [&](float t) { if (t > duration) duration = t; };

    auto saneCount = [](int value, int maxValue) {
        return value >= 0 && value <= maxValue;
    };

    /* ── Slot timelines ─────────────────────────────────────────────────── */
    int slotTlCount = readVarint(input, true);
    if (!saneCount(slotTlCount, 20000)) return new Animation(name, timelines, duration);
    for (int i = 0; i < slotTlCount; ++i) {
        int slotIndex = readVarint(input, true);
        int tlN       = readVarint(input, true);
        if (!saneCount(tlN, 20000)) return new Animation(name, timelines, duration);
        for (int j = 0; j < tlN; ++j) {
            int tlType = readByte(input);
            int frames = readVarint(input, true);
            if (!saneCount(frames, 200000)) return new Animation(name, timelines, duration);

            switch (tlType) {
            case 0: { /* Attachment */
                AttachmentTimeline *tl = new AttachmentTimeline(frames);
                tl->_slotIndex = slotIndex;
                for (int f = 0; f < frames; ++f) {
                    float t = readFloat(input);
                    String an = _is36 ? readString(input)
                                      : readStringRef(input, _strings);
                    tl->setFrame(f, t, an);
                    updateDur(t);
                }
                timelines.add(tl);
                break;
            }
            case 1: { /* Color */
                ColorTimeline *tl = new ColorTimeline(frames);
                tl->_slotIndex = slotIndex;
                for (int f = 0; f < frames; ++f) {
                    float t = readFloat(input);
                    float r, g, b, a;
                    readColor(input, r, g, b, a);
                    tl->setFrame(f, t, r, g, b, a);
                    if (f < frames - 1) readCurve(input, tl, f);
                    updateDur(t);
                }
                timelines.add(tl);
                break;
            }
            case 2: { /* TwoColor — skip */
                for (int f = 0; f < frames; ++f) {
                    readFloat(input);
                    readInt(input);    // light RGBA
                    readInt(input);    // dark  RGBA
                    if (f < frames - 1) readCurve(input, nullptr, f);
                }
                break;
            }
            default:
                qWarning("[SkelBin] unknown slot-tl type %d", tlType);
                break;
            }
        }
    }

    /* ── Bone timelines ─────────────────────────────────────────────────── */
    int boneTlCount = readVarint(input, true);
    if (!saneCount(boneTlCount, 20000)) return new Animation(name, timelines, duration);
    for (int i = 0; i < boneTlCount; ++i) {
        int boneIndex = readVarint(input, true);
        int tlN       = readVarint(input, true);
        if (!saneCount(tlN, 20000)) return new Animation(name, timelines, duration);
        for (int j = 0; j < tlN; ++j) {
            int tlType = readByte(input);
            int frames = readVarint(input, true);
            if (!saneCount(frames, 200000)) return new Animation(name, timelines, duration);

            switch (tlType) {
            case 0: { /* Rotate */
                RotateTimeline *tl = new RotateTimeline(frames);
                tl->_boneIndex = boneIndex;
                for (int f = 0; f < frames; ++f) {
                    float t   = readFloat(input);
                    float deg = readFloat(input);
                    tl->setFrame(f, t, deg);
                    if (f < frames - 1) readCurve(input, tl, f);
                    updateDur(t);
                }
                timelines.add(tl);
                break;
            }
            case 1: /* Translate */
            case 2: /* Scale */
            case 3: { /* Shear */
                TranslateTimeline *tl;
                if (tlType == 2)
                    tl = new ScaleTimeline(frames);
                else if (tlType == 3)
                    tl = new ShearTimeline(frames);
                else
                    tl = new TranslateTimeline(frames);
                tl->_boneIndex = boneIndex;
                for (int f = 0; f < frames; ++f) {
                    float t = readFloat(input);
                    float x = readFloat(input);
                    float y = readFloat(input);
                    if (tlType == 1) { x *= _scale; y *= _scale; }
                    tl->setFrame(f, t, x, y);
                    if (f < frames - 1) readCurve(input, tl, f);
                    updateDur(t);
                }
                timelines.add(tl);
                break;
            }
            default:
                qWarning("[SkelBin] unknown bone-tl type %d", tlType);
                break;
            }
        }
    }

    /* ── IK constraint timelines ────────────────────────────────────────── */
    int ikTlN = readVarint(input, true);
    if (!saneCount(ikTlN, 20000)) return new Animation(name, timelines, duration);
    for (int i = 0; i < ikTlN; ++i) {
        int constraintIdx = readVarint(input, true); // constraint index
        int frames = readVarint(input, true);
        if (!saneCount(frames, 200000)) return new Animation(name, timelines, duration);
        IkConstraintTimeline *tl = new IkConstraintTimeline(frames);
        tl->_ikConstraintIndex = constraintIdx;
        for (int f = 0; f < frames; ++f) {
            float t = readFloat(input);    // time
            float mix = readFloat(input);  // mix
            float softness = 0;
            if (!_is36) softness = readFloat(input); // softness (3.8+)
            int bendDir = readSByte(input);          // bendDirection
            bool compress = false, stretch = false;
            if (!_is36 || _is37) {
                compress = readBoolean(input); // compress (3.7+ / 3.8+)
                stretch  = readBoolean(input); // stretch  (3.7+ / 3.8+)
            }
            tl->setFrame(f, t, mix, softness, bendDir, compress, stretch);
            if (f < frames - 1) readCurve(input, tl, f);
            updateDur(t);
        }
        timelines.add(tl);
    }

    /* ── Transform constraint timelines ─────────────────────────────────── */
    int xfTlN = readVarint(input, true);
    if (!saneCount(xfTlN, 20000)) return new Animation(name, timelines, duration);
    for (int i = 0; i < xfTlN; ++i) {
        int constraintIdx = readVarint(input, true); // constraint index
        int frames = readVarint(input, true);
        if (!saneCount(frames, 200000)) return new Animation(name, timelines, duration);
        TransformConstraintTimeline *tl = new TransformConstraintTimeline(frames);
        tl->_transformConstraintIndex = constraintIdx;
        for (int f = 0; f < frames; ++f) {
            float t = readFloat(input);           // time
            float rotateMix = readFloat(input);   // rotateMix
            float translateMix = readFloat(input);// translateMix
            float scaleMix = readFloat(input);    // scaleMix
            float shearMix = readFloat(input);    // shearMix
            tl->setFrame(f, t, rotateMix, translateMix, scaleMix, shearMix);
            if (f < frames - 1) readCurve(input, tl, f);
            updateDur(t);
        }
        timelines.add(tl);
    }

    /* ── Path constraint timelines — skip ───────────────────────────────── */
    int pathTlN = readVarint(input, true);
    if (!saneCount(pathTlN, 20000)) return new Animation(name, timelines, duration);
    for (int i = 0; i < pathTlN; ++i) {
        readVarint(input, true); // constraint index
        int nTl = readVarint(input, true);
        if (!saneCount(nTl, 20000)) return new Animation(name, timelines, duration);
        for (int j = 0; j < nTl; ++j) {
            int ptype  = readByte(input);
            int frames = readVarint(input, true);
            if (!saneCount(frames, 200000)) return new Animation(name, timelines, duration);
            if (ptype <= 1) {
                for (int f = 0; f < frames; ++f) {
                    readFloat(input); readFloat(input);
                    if (f < frames - 1) readCurve(input, nullptr, f);
                }
            } else {
                for (int f = 0; f < frames; ++f) {
                    readFloat(input); readFloat(input); readFloat(input);
                    if (f < frames - 1) readCurve(input, nullptr, f);
                }
            }
        }
    }

    /* ── Deform timelines ───────────────────────────────────────────────── */
    int defSkinN = readVarint(input, true);
    if (!saneCount(defSkinN, 20000)) return new Animation(name, timelines, duration);
    for (int i = 0; i < defSkinN; ++i) {
        readVarint(input, true); // skin index
        int defSlotN = readVarint(input, true);
        if (!saneCount(defSlotN, 20000)) return new Animation(name, timelines, duration);
        for (int j = 0; j < defSlotN; ++j) {
            readVarint(input, true); // slot index
            int defTlN = readVarint(input, true);
            if (!saneCount(defTlN, 20000)) return new Animation(name, timelines, duration);
            for (int k = 0; k < defTlN; ++k) {
                if (_is36)
                    readString(input); // attachment name (inline)
                else
                    readStringRef(input, _strings); // attachment name (ref)
                int frames = readVarint(input, true);
                if (!saneCount(frames, 200000)) return new Animation(name, timelines, duration);
                for (int f = 0; f < frames; ++f) {
                    readFloat(input); // time
                    int deformLen = readVarint(input, true);
                    if (deformLen > 0) {
                        readVarint(input, true); // start offset
                        for (int d = 0; d < deformLen; ++d) readFloat(input);
                    }
                    if (f < frames - 1) readCurve(input, nullptr, f);
                }
            }
        }
    }

    /* ── Draw-order timeline ────────────────────────────────────────────── */
    int doFrames = readVarint(input, true);
    if (!saneCount(doFrames, 200000)) return new Animation(name, timelines, duration);
    if (doFrames > 0) {
        int totalSlots = (int)sd->getSlots().size();
        DrawOrderTimeline *tl = new DrawOrderTimeline(doFrames);
        for (int f = 0; f < doFrames; ++f) {
            float t     = readFloat(input);
            int offsetN = readVarint(input, true);

            if (offsetN < 0 || offsetN > totalSlots) {
                qWarning("[SkelBin] draw order offsetN=%d out of range (totalSlots=%d)", offsetN, totalSlots);
                delete tl;
                return new Animation(name, timelines, duration);
            }

            Vector<int> drawOrder;
            drawOrder.setSize(totalSlots, -1);
            Vector<int> unchanged;
            unchanged.setSize(totalSlots - offsetN, 0);

            int origIdx = 0, uncIdx = 0;
            for (int o = 0; o < offsetN; ++o) {
                int si2 = readVarint(input, true);
                while (origIdx != si2) unchanged[uncIdx++] = origIdx++;
                drawOrder[origIdx + readVarint(input, true)] = origIdx++;
            }
            while (origIdx < totalSlots)
                unchanged[uncIdx++] = origIdx++;
            for (int o = totalSlots - 1; o >= 0; --o)
                if (drawOrder[o] == -1) drawOrder[o] = unchanged[--uncIdx];

            tl->setFrame(f, t, drawOrder);
            updateDur(t);
        }
        timelines.add(tl);
    }

    /* ── Event timeline — skip ──────────────────────────────────────────── */
    int evtFrames = readVarint(input, true);
    if (!saneCount(evtFrames, 200000)) return new Animation(name, timelines, duration);
    for (int f = 0; f < evtFrames; ++f) {
        readFloat(input);            // time
        readVarint(input, true);     // event index
        readVarint(input, false);    // intValue
        readFloat(input);            // floatValue
        bool hasStr = readBoolean(input);
        if (hasStr) readString(input);
        if (!_is36) {
            /* 3.8+: audio */
            bool hasAudio = readBoolean(input);
            if (hasAudio) {
                readString(input);  // audioPath
                readFloat(input);   // volume
                readFloat(input);   // balance
            }
        }
    }

    qWarning("[SkelBin]   anim '%s': %d timelines  dur=%.3f",
             name.buffer(), (int)timelines.size(), duration);

    return new Animation(name, timelines, duration);
}

/* ═══════════════════════════════════════════════════════════════════════════
   readCurve
   ═══════════════════════════════════════════════════════════════════════════*/

void SkeletonBinary::readCurve(DataInput &input, CurveTimeline *tl, int frameIndex) {
    int type = readByte(input);
    switch (type) {
    case 0: if (tl) tl->setLinear(frameIndex);  break;
    case 1: if (tl) tl->setStepped(frameIndex); break;
    case 2: {
        float cx1 = readFloat(input);
        float cy1 = readFloat(input);
        float cx2 = readFloat(input);
        float cy2 = readFloat(input);
        if (tl) tl->setCurve(frameIndex, cx1, cy1, cx2, cy2);
        break;
    }
    default: break;
    }
}

} // namespace spine
