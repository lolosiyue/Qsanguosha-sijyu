#ifndef SPINE_MESH_ATTACHMENT_H
#define SPINE_MESH_ATTACHMENT_H

#include <spine/Attachment.h>
#include <spine/Atlas.h>

namespace spine {

class Slot;

class MeshAttachment : public Attachment {
public:
    MeshAttachment(const String &name);

    void computeWorldVertices(Slot &slot, int start, int count,
                              float *worldVertices, int offset, int stride);

    Vector<float> &getWorldVerticesArray() { return _worldVertices; }
    Vector<int> &getBones() { return _bones; }
    Vector<float> &getVertices() { return _vertices; }
    Vector<unsigned short> &getTriangles() { return _triangles; }
    Vector<float> &getRegionUVs() { return _regionUVs; }
    Vector<float> &getUVs() { return _uvs; }

    Color &getColor() { return _color; }

    AtlasRegion *getRegion() { return _region; }
    void setRegion(AtlasRegion *region) { _region = region; }

    int getWorldVerticesLength() const { return _worldVerticesLength; }
    void setWorldVerticesLength(int len) { _worldVerticesLength = len; }

    int getHullLength() const { return _hullLength; }
    void setHullLength(int len) { _hullLength = len; }

    bool isWeighted() const { return _weighted; }
    void setWeighted(bool v) { _weighted = v; }

    void updateUVs();

private:
    Vector<int> _bones;
    Vector<float> _vertices;
    Vector<unsigned short> _triangles;
    Vector<float> _regionUVs;
    Vector<float> _uvs;
    Vector<float> _worldVertices;

    Color _color;
    AtlasRegion *_region;
    int _worldVerticesLength;
    int _hullLength;
    bool _weighted;
};

} // namespace spine

#endif // SPINE_MESH_ATTACHMENT_H
