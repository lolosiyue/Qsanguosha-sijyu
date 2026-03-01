/******************************************************************************
 * spine-cpp IkConstraint implementation
 * Ported from spine-ts/spine.js IkConstraint
 *****************************************************************************/

#include <spine/IkConstraint.h>
#include <spine/Bone.h>
#include <spine/Skeleton.h>
#include <cmath>

namespace spine {

static const float PI = 3.14159265358979323846f;
static const float DEG_RAD = PI / 180.0f;
static const float RAD_DEG = 180.0f / PI;

IkConstraint::IkConstraint(IkConstraintData &data, Skeleton &skeleton)
    : _data(data), _target(nullptr),
      _mix(data._mix), _softness(data._softness),
      _bendDirection(data._bendDirection),
      _compress(data._compress), _stretch(data._stretch),
      _active(true)
{
    // Resolve bone references
    for (size_t i = 0; i < data._bones.size(); ++i) {
        Bone *b = skeleton.findBone(data._bones[i]->getName());
        if (b) _bones.add(b);
    }
    if (data._target)
        _target = skeleton.findBone(data._target->getName());
}

void IkConstraint::update() {
    if (!_target || _bones.size() == 0) return;

    float targetX = _target->getWorldX();
    float targetY = _target->getWorldY();

    if (_bones.size() == 1) {
        apply(*_bones[0], targetX, targetY, _compress, _stretch, _data._uniform, _mix);
    } else if (_bones.size() >= 2) {
        apply(*_bones[0], *_bones[1], targetX, targetY, _bendDirection, _stretch, _softness, _mix);
    }
}

// 1-bone IK
void IkConstraint::apply(Bone &bone, float targetX, float targetY,
                          bool compress, bool stretch, bool uniform, float alpha)
{
    Bone *p = bone.getParent();
    if (!p) return;

    float id = 1.0f / (p->getA() * p->getD() - p->getB() * p->getC());
    float x = targetX - p->getWorldX();
    float y = targetY - p->getWorldY();
    float tx = (x * p->getD() - y * p->getB()) * id - bone.getX();
    float ty = (y * p->getA() - x * p->getC()) * id - bone.getY();

    float rotationIK = std::atan2(ty, tx) * RAD_DEG - bone.getData()._shearX - bone.getData()._rotation;
    if (bone.getScaleX() < 0) rotationIK += 180;

    if (rotationIK > 180) rotationIK -= 360;
    else if (rotationIK < -180) rotationIK += 360;

    float sx = bone.getScaleX();
    float sy = bone.getScaleY();
    if (compress || stretch) {
        float b = bone.getData()._length * sx;
        float dd = std::sqrt(tx * tx + ty * ty);
        if ((compress && dd < b) || (stretch && dd > b && b > 0.0001f)) {
            float s = (dd / b - 1) * alpha + 1;
            sx *= s;
            if (uniform) sy *= s;
        }
    }

    bone.updateWorldTransformWith(
        bone.getX(), bone.getY(),
        bone.getData()._rotation + rotationIK * alpha,
        sx, sy,
        bone.getData()._shearX, bone.getData()._shearY
    );
}

// 2-bone IK
void IkConstraint::apply(Bone &parent, Bone &child, float targetX, float targetY,
                          int bendDir, bool stretch, float softness, float alpha)
{
    if (alpha == 0) {
        child.updateWorldTransform();
        return;
    }

    float px = parent.getX(), py = parent.getY();
    float psx = parent.getScaleX(), psy = parent.getScaleY();
    float csx = child.getScaleX();
    int os1, os2, s2;

    if (psx < 0) {
        psx = -psx;
        os1 = 180;
        s2 = -1;
    } else {
        os1 = 0;
        s2 = 1;
    }
    if (psy < 0) {
        psy = -psy;
        s2 = -s2;
    }
    if (csx < 0) {
        csx = -csx;
        os2 = 180;
    } else {
        os2 = 0;
    }

    float cx = child.getX(), cy = 0, cwx = 0, cwy = 0;
    float a = parent.getA(), b = parent.getB(), c = parent.getC(), d = parent.getD();

    bool u = std::abs(psx - psy) <= 0.0001f;
    if (!u) {
        cy = 0;
        cwx = a * cx + parent.getWorldX();
        cwy = c * cx + parent.getWorldY();
    } else {
        cy = child.getY();
        cwx = a * cx + b * cy + parent.getWorldX();
        cwy = c * cx + d * cy + parent.getWorldY();
    }

    Bone *pp = parent.getParent();
    if (!pp) return;

    a = pp->getA(); b = pp->getB(); c = pp->getC(); d = pp->getD();
    float id = 1.0f / (a * d - b * c);
    float x = cwx - pp->getWorldX();
    float y = cwy - pp->getWorldY();
    float dx = (x * d - y * b) * id - px;
    float dy = (y * a - x * c) * id - py;

    float l1 = std::sqrt(dx * dx + dy * dy);
    float l2 = child.getData()._length * csx;
    if (l1 < 0.0001f) {
        apply(parent, targetX, targetY, false, stretch, false, alpha);
        child.updateWorldTransformWith(cx, cy, 0, child.getScaleX(), child.getScaleY(), child.getData()._shearX, child.getData()._shearY);
        return;
    }

    x = targetX - pp->getWorldX();
    y = targetY - pp->getWorldY();
    float tx = (x * d - y * b) * id - px;
    float ty = (y * a - x * c) * id - py;

    float dd = tx * tx + ty * ty;

    if (softness != 0) {
        softness *= psx * (csx + 1) / 2;
        float td = std::sqrt(dd), sd = td - l1 - l2 * psx + softness;
        if (sd > 0) {
            float p2 = std::min(1.0f, sd / (softness * 2)) - 1;
            p2 = (sd - softness * (1 - p2 * p2)) / td;
            tx -= p2 * tx;
            ty -= p2 * ty;
            dd = tx * tx + ty * ty;
        }
    }

    float a1, a2;
    if (u) {
        l2 *= psx;
        float cos2 = (dd - l1 * l1 - l2 * l2) / (2 * l1 * l2);
        if (cos2 < -1) cos2 = -1;
        else if (cos2 > 1) {
            cos2 = 1;
            if (stretch) {
                a = (std::sqrt(dd) / (l1 + l2) - 1) * alpha + 1;
                psx *= a; // stretch parent
            }
        }
        a2 = std::acos(cos2) * bendDir;
        a = l1 + l2 * cos2;
        b = l2 * std::sin(a2);
        a1 = std::atan2(ty * a - tx * b, tx * a + ty * b);
    } else {
        float la1 = l1 * l1, la2 = l2 * l2;
        float ldd = dd;
        a = psx * l2; b = psy * l2;
        float aa = a * a, bb = b * b;
        float ta = std::atan2(ty, tx);
        c = bb * la1 + aa * ldd - aa * bb;
        float c1 = -2 * bb * l1;
        float c2 = bb - aa;
        d = c1 * c1 - 4 * c2 * c;
        if (d >= 0) {
            float q = std::sqrt(d);
            if (c1 < 0) q = -q;
            q = -(c1 + q) / 2;
            float r0 = q / c2, r1 = c / q;
            float r = std::abs(r0) < std::abs(r1) ? r0 : r1;
            if (r * r <= ldd) {
                y = std::sqrt(ldd - r * r) * bendDir;
                a1 = ta - std::atan2(y, r);
                a2 = std::atan2(y / psy, (r - l1) / psx);
                goto outer;
            }
        }
        {
            float minAngle = PI, minX = l1 - a, minDist = minX * minX, minY = 0;
            float maxAngle = 0, maxX = l1 + a, maxDist = maxX * maxX, maxY = 0;

            c = -a * l1 / (aa - bb);
            if (c >= -1 && c <= 1) {
                c = std::acos(c);
                x = a * std::cos(c) + l1;
                y = b * std::sin(c);
                d = x * x + y * y;
                if (d < minDist) { minAngle = c; minDist = d; minX = x; minY = y; }
                if (d > maxDist) { maxAngle = c; maxDist = d; maxX = x; maxY = y; }
            }
            if (ldd <= (minDist + maxDist) / 2) {
                a1 = ta - std::atan2(minY * bendDir, minX);
                a2 = minAngle * bendDir;
            } else {
                a1 = ta - std::atan2(maxY * bendDir, maxX);
                a2 = maxAngle * bendDir;
            }
        }
    }
outer:
    float os = std::atan2(cy, cx) * s2;
    float rotation = parent.getData()._rotation;
    a1 = (a1 - os) * RAD_DEG + os1 - rotation;
    if (a1 > 180) a1 -= 360;
    else if (a1 < -180) a1 += 360;

    parent.updateWorldTransformWith(px, py, rotation + a1 * alpha,
                                     parent.getScaleX(), parent.getScaleY(), 0, 0);

    rotation = child.getData()._rotation;
    a2 = ((a2 + os) * RAD_DEG - child.getData()._shearX) * s2 + os2 - rotation;
    if (a2 > 180) a2 -= 360;
    else if (a2 < -180) a2 += 360;

    child.updateWorldTransformWith(cx, cy, rotation + a2 * alpha,
                                    child.getScaleX(), child.getScaleY(),
                                    child.getData()._shearX, child.getData()._shearY);
}

} // namespace spine
