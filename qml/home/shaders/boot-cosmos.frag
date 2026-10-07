#version 440

// Drawn boot animation: a Big Bang that settles into a black hole with an accretion
// disk. Everything follows `time` (seconds), which BootCosmosItem advances on the
// render thread. Compiled into boot-cosmos.frag.qsb with
//   qsb --glsl "100 es,120,150" --hlsl 50 --msl 12 -o boot-cosmos.frag.qsb boot-cosmos.frag
// and boot-cosmos.vert the same way plus -b, the batchable variant Qt Quick requires.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    vec2 resolution;
    float cornerRadius;
};

float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i) {
        value += amplitude * noise(p);
        p = p * 2.03 + vec2(17.1, 9.2);
        amplitude *= 0.5;
    }
    return value;
}

vec3 starField(vec2 p)
{
    vec3 color = vec3(0.0);
    for (int layer = 0; layer < 2; ++layer) {
        float density = layer == 0 ? 22.0 : 48.0;
        vec2 grid = p * density + float(layer) * 31.7;
        vec2 cell = floor(grid);
        float h = hash(cell);
        if (h > 0.9) {
            vec2 centre = vec2(hash(cell + 7.1), hash(cell + 3.3)) * 0.7 + 0.15;
            float d = length(fract(grid) - centre);
            float twinkle = 0.65 + 0.35 * sin(time * (1.3 + h * 2.7) + h * 40.0);
            float size = layer == 0 ? 0.11 : 0.07;
            vec3 tint = mix(vec3(0.65, 0.78, 1.0), vec3(1.0, 0.86, 0.68), hash(cell + 9.9));
            color += tint * smoothstep(size, 0.0, d) * twinkle * (layer == 0 ? 1.0 : 0.55);
        }
    }
    return color;
}

void main()
{
    vec2 pixel = qt_TexCoord0 * resolution;
    // Height units around the centre of the scene; y grows downward.
    vec2 p = (pixel - resolution * vec2(0.5, 0.44)) / (resolution.y * 0.5);
    float r = length(p);
    vec2 dir = p / max(r, 0.001);
    // Seconds since the Big Bang, and how far the black hole has formed.
    float bang = time - 0.6;
    float form = smoothstep(1.4, 3.4, bang);
    // Event horizon radius.
    float R = 0.2;

    // Deep space, with a faint nebula that appears with the stars.
    float starsIn = smoothstep(0.6, 2.6, bang);
    vec3 color = mix(vec3(0.004, 0.006, 0.016), vec3(0.02, 0.03, 0.07), 1.0 - qt_TexCoord0.y);
    color += vec3(0.10, 0.05, 0.16) * fbm(p * 1.6 + 3.0) * starsIn * 0.6;

    // Stars bend around the hole as it forms.
    vec2 lensed = p - dir * (R * R * 1.4 * form) / max(r, R * 0.6);
    color += starField(lensed + vec2(time * 0.004, 0.0)) * starsIn;

    // Before the bang: a single point of light that swells.
    float before = 1.0 - smoothstep(0.0, 0.08, bang);
    color += vec3(1.0, 0.92, 0.8) * (0.0015 / (r * r + 0.0015)) * smoothstep(0.0, 0.6, time) * before;

    if (bang > 0.0) {
        // Flash, a fireball of cooling plasma, debris and the shock front.
        color += vec3(1.0, 0.9, 0.78) * exp(-bang * 3.2) * 1.6 / (1.0 + r * r * 5.0);

        float fireball = 0.12 + bang * 0.85;
        float plasma = fbm(p * 3.2 - dir * bang * 1.5 + 5.0);
        vec3 hot = mix(vec3(1.0, 0.55, 0.15), vec3(0.55, 0.25, 1.0), smoothstep(0.0, fireball, r));
        color += hot * plasma * smoothstep(fireball, fireball * 0.15, r) * exp(-bang * 1.15) * 2.2;

        float front = bang * 1.25;
        float debris = smoothstep(0.62, 0.95, noise(dir * 10.0 + vec2((r - front) * 4.0)));
        color += vec3(1.0, 0.8, 0.55) * debris * smoothstep(front + 0.05, front - 0.6, r)
                 * exp(-bang * 0.9) * 1.2;

        float shock = (r - bang * 1.7) / 0.035;
        color += vec3(0.6, 0.75, 1.0) * exp(-shock * shock) * exp(-bang * 1.1) * 1.4;
    }

    if (form > 0.0) {
        // The near side of the disk: a tilted band whose inner part turns faster. It
        // passes in front of the lower half of the horizon.
        vec2 d = vec2(p.x, p.y / 0.2);
        float rd = length(d);
        vec2 diskDir = d / max(rd, 0.001);
        float band = smoothstep(R * 1.25, R * 1.75, rd) * smoothstep(R * 4.4, R * 2.1, rd);
        float turn = time * 1.6 * R * 3.0 / max(rd, R);
        vec2 spun = vec2(cos(turn) * diskDir.x - sin(turn) * diskDir.y,
                         sin(turn) * diskDir.x + cos(turn) * diskDir.y);
        float streaks = fbm(spun * 3.0 + vec2(0.0, rd * 7.0));
        // The side turning towards the viewer is brighter.
        float doppler = 1.0 - 0.55 * diskDir.x;
        vec3 diskColor = mix(vec3(1.0, 0.94, 0.8), vec3(1.0, 0.42, 0.12), smoothstep(R * 1.6, R * 4.0, rd));
        float inFront = p.y > 0.0 || r > R ? 1.0 : 0.0;
        float near = band * inFront * form;

        // The far side of the disk, lensed into a thin arc over the horizon and a fainter
        // one under it, brightest next to the photon ring.
        float arcOuter = smoothstep(R * 1.6, R * 1.18, r);
        float arc = smoothstep(R * 1.09, R * 1.16, r) * arcOuter * arcOuter;
        float above = smoothstep(0.35, -0.95, dir.y);
        float below = smoothstep(0.55, 1.0, dir.y) * 0.4;
        float farTurn = time * 0.8;
        vec2 farDir = vec2(cos(farTurn) * dir.x - sin(farTurn) * dir.y, sin(farTurn) * dir.x + cos(farTurn) * dir.y);
        float farSwirl = fbm(farDir * 2.5 + vec2(0.0, r * 9.0));
        vec3 arcColor = mix(vec3(1.0, 0.55, 0.2), vec3(1.0, 0.92, 0.75), arcOuter);
        color += arcColor * arc * (above + below) * (0.5 + farSwirl) * 1.6 * form;

        // The horizon swallows everything behind it; the photon ring hugs its edge,
        // hidden where the near disk crosses it.
        color *= 1.0 - smoothstep(R * 1.03, R * 0.97, r) * form;
        float ring = (r - R * 1.07) / 0.006;
        color += vec3(1.0, 0.85, 0.62) * exp(-ring * ring) * 1.6 * form * (1.0 - near * 0.85);

        color += diskColor * (0.35 + streaks) * doppler * 1.25 * near;
    }

    // Filmic roll-off and a soft vignette.
    color = 1.0 - exp(-color * 1.35);
    color *= 1.0 - 0.35 * smoothstep(0.6, 1.6, length(p * vec2(0.8, 1.0)));

    // Rounded card corners.
    vec2 q = abs(pixel - resolution * 0.5) - (resolution * 0.5 - cornerRadius);
    float outside = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - cornerRadius;
    float alpha = clamp(0.5 - outside, 0.0, 1.0);

    fragColor = vec4(color * alpha, alpha) * qt_Opacity;
}
