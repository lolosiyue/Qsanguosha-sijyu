#ifndef SPINE_SKELETON_BINARY_H
#define SPINE_SKELETON_BINARY_H

#include <spine/Atlas.h>
#include <spine/SkeletonData.h>
#include <spine/SpineString.h>

namespace spine {

class CurveTimeline;

/// Reads a Spine binary .skel file.
class SkeletonBinary {
public:
    enum RuntimeVersion {
        RuntimeAuto = 0,
        Runtime3_5_35,
        Runtime3_7,
        Runtime3_8,
        Runtime4_0,
        Runtime4_1
    };

    explicit SkeletonBinary(Atlas *atlas);
    ~SkeletonBinary();

    /// Read .skel file from disk.
    SkeletonData *readSkeletonDataFile(const String &path);

    /// Read .skel from in-memory buffer.
    SkeletonData *readSkeletonData(const unsigned char *binary, int length);

    void setScale(float scale) { _scale = scale; }
    float getScale() const { return _scale; }

    void setRuntimeVersionHint(RuntimeVersion v) { _runtimeVersionHint = v; }
    RuntimeVersion getRuntimeVersionHint() const { return _runtimeVersionHint; }
    void setRuntimeVersionHint(const String &versionText);

    const String &getError() const { return _error; }

private:
    Atlas *_atlas;
    float _scale;
    String _error;
    bool _nonessential;   // set during internalRead, used by readSkin
    bool _is36;           // true for legacy 3.x layout (3.5/3.6/3.7)
    bool _is35;           // true for 3.5.x specifically (no darkColor, no transform local/relative)
    bool _is37;           // true for 3.7.x specifically (adds IK compress/stretch, event audio, header audio)
    Vector<String> _strings; // string table (3.8+ only)
    RuntimeVersion _runtimeVersionHint;

    // Binary reading helpers
    struct DataInput {
        const unsigned char *cursor;
        const unsigned char *end;
    };

    static int readVarint(DataInput &input, bool optimizePositive);
    static float readFloat(DataInput &input);
    static int readInt(DataInput &input);
    static short readShort(DataInput &input);
    static signed char readSByte(DataInput &input);
    static unsigned char readByte(DataInput &input);
    static bool readBoolean(DataInput &input);
    static String readString(DataInput &input);
    static String readStringRef(DataInput &input, Vector<String> &strings);
    static float readColor(DataInput &input);
    static void readColor(DataInput &input, float &r, float &g, float &b, float &a);

    SkeletonData *internalRead(DataInput &input);
    Skin *readSkin(DataInput &input, SkeletonData *skeletonData, bool defaultSkin);
    Animation *readAnimation(DataInput &input, const String &name, SkeletonData *skeletonData);
    void readCurve(DataInput &input, CurveTimeline *timeline, int frameIndex);
    void skipVertices(DataInput &input, int vertexCount, float scale);
};

} // namespace spine

#endif // SPINE_SKELETON_BINARY_H
