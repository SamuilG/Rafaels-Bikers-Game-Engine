#version 450

layout(set = 0, binding = 1) uniform sampler2D waveCells;
layout(std430, set = 0, binding = 2) readonly buffer GroundHeights { float y[]; } ground;

const int maxCorners = 12;
const int verticesPerCorner = 12;
const int slabVertexCount = maxCorners * verticesPerCorner;
const int fissureVerticesPerCorner = 3;
const int resolution = 128;
const float missing = -1000.0;

struct Slab {
    vec4 centerAndHinge;
    vec2 corners[maxCorners];
};
layout(std430, set = 0, binding = 4) readonly buffer GroundSlabs { Slab pieces[]; } slabs;
struct MaterialMapping {
    vec4 uAndSlab;
    vec4 v;
};
layout(std430, set = 0, binding = 5) readonly buffer GroundMaterials { MaterialMapping mappings[]; } surfaces;

layout(push_constant) uniform GroundWave {
    mat4 viewProjection;
    vec4 originRadius;
    vec4 shape;       // width, strength, texture repeats per metre, ground lift
    vec4 grid;        // spacing, output color mode (0 sRGB, 1 linear, 2 HDR), slab thickness, exposure
    vec4 baseColorFactor;
} wave;

layout(location = 0) out vec3 baseWorld;
layout(location = 1) out vec3 displacedWorld;
layout(location = 2) flat out vec3 faceNormal;
layout(location = 3) flat out vec3 facet; // strength, face (0 top, 1 side, 2 bottom, 3 fissure), opening
layout(location = 4) out vec2 groundUv;
layout(location = 5) out float edgeDistance;

float stableNoise(vec2 position) {
    vec3 p = fract(vec3(position.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 surfacePoint(vec2 point) {
    ivec2 cell = clamp(ivec2(floor(point)), ivec2(0), ivec2(resolution - 2));
    int offset = cell.y * resolution + cell.x;
    vec4 heights = vec4(ground.y[offset], ground.y[offset + 1],
        ground.y[offset + resolution], ground.y[offset + resolution + 1]);
    float low = min(min(heights.x, heights.y), min(heights.z, heights.w));
    float high = max(max(heights.x, heights.y), max(heights.z, heights.w));
    vec2 weight = point - vec2(cell);
    float height = low == missing || high - low > wave.grid.x * 1.35
        ? missing : mix(mix(heights.x, heights.y, weight.x), mix(heights.z, heights.w, weight.x), weight.y);
    vec2 xz = (point - float(resolution - 1) * 0.5) * wave.grid.x;
    return vec3(xz.x, height, xz.y);
}

vec3 rotateAround(vec3 vector, vec3 axis, float sine, float cosine) {
    return vector * cosine + cross(axis, vector) * sine + axis * dot(axis, vector) * (1.0 - cosine);
}

void hideVertex() {
    gl_Position = vec4(2.0, 0.0, 0.0, 1.0);
}

vec2 surfaceUv(MaterialMapping material, vec2 offset) {
    return vec2(material.uAndSlab.x, material.v.x) +
        vec2(dot(material.uAndSlab.yz, offset), dot(material.v.yz, offset));
}

float distanceToEdge(vec3 center, vec3 a, vec3 b) {
    vec2 edge = b.xz - a.xz;
    vec2 offset = center.xz - a.xz;
    return abs(edge.x * offset.y - edge.y * offset.x) / max(0.0001, length(edge));
}

void main() {
    MaterialMapping material = surfaces.mappings[gl_InstanceIndex];
    int slabIndex = int(material.uAndSlab.w);
    vec4 slab = slabs.pieces[slabIndex].centerAndHinge;
    // The first draw starts past the slab vertices and shades their resting footprint.
    bool fissure = gl_VertexIndex >= slabVertexCount;
    int vertexIndex = fissure ? gl_VertexIndex - slabVertexCount : gl_VertexIndex;
    int stride = fissure ? fissureVerticesPerCorner : verticesPerCorner;
    int edge = vertexIndex / stride;
    int cornerVertex = vertexIndex % stride;
    if (edge >= int(slab.z)) {
        hideVertex();
        return;
    }

    vec2 planarCenter = (slab.xy - float(resolution - 1) * 0.5) * wave.grid.x;
    float planarRadius = length(planarCenter);
    if (planarRadius > wave.originRadius.w + wave.shape.x * 2.1 ||
        planarRadius + wave.shape.x * 5.8 + 12.0 < wave.originRadius.w) {
        hideVertex();
        return;
    }
    vec3 center = surfacePoint(slab.xy);
    if (center.y == missing) {
        hideVertex();
        return;
    }

    vec2 worldXZ = center.xz + wave.originRadius.xz;
    float seed = stableNoise(worldXZ * 1.73);
    float densitySample = textureLod(waveCells, worldXZ * (wave.shape.z * 0.18) + vec2(0.47, 0.28), 1.0).r;
    float participation = mix(0.62, 0.94, smoothstep(0.20, 0.74, densitySample));
    bool loose = stableNoise(worldXZ + vec2(75.3, 21.7)) < participation;
    float timing = stableNoise(worldXZ + vec2(31.6, 68.2));
    float heightVariation = stableNoise(worldXZ + vec2(16.5, 48.1));
    float cells = textureLod(waveCells, worldXZ * wave.shape.z, 0.0).r;
    float seamPhase = (wave.originRadius.w - length(center)) / wave.shape.x + (cells - 0.5) * 0.46;
    float phase = seamPhase + (timing - 0.5) * 0.98;
    float tail = 1.0 - smoothstep(mix(0.62, 1.30, seed), mix(2.85, 4.42, seed), phase);
    float crest = smoothstep(-0.58, 0.46, phase) * tail;
    float pulse = crest * wave.shape.y;
    float liftVariation = mix(0.12, 1.57, pow(heightVariation, 1.25));
    float clearance = smoothstep(0.45, 2.15, planarRadius);
    float rise = loose ? wave.shape.w * pulse * liftVariation * clearance : 0.0;
    float visibility = pulse * smoothstep(0.008, 0.12, rise);
    float opening = smoothstep(0.022, 0.33, rise);
    float seam = smoothstep(-1.16, -0.40, seamPhase) *
        (1.0 - smoothstep(1.8, 4.65, seamPhase)) * wave.shape.y *
        smoothstep(0.012, 0.19, wave.shape.w);
    if ((fissure && seam < 0.018 && opening < 0.018) ||
        (!fissure && (visibility < 0.018 || (cornerVertex >= 3 && wave.grid.z <= 0.0)))) {
        hideVertex();
        return;
    }

    int next = (edge + 1) % int(slab.z);
    vec2 boundaryA = slabs.pieces[slabIndex].corners[edge];
    vec2 boundaryB = slabs.pieces[slabIndex].corners[next];
    if (fissure) {
        vec3 a = surfacePoint(boundaryA);
        vec3 b = surfacePoint(boundaryB);
        if (min(a.y, b.y) == missing ||
            max(abs(a.y - center.y), abs(b.y - center.y)) > wave.grid.x * 2.4) {
            hideVertex();
            return;
        }
        vec3 point = cornerVertex == 0 ? center : cornerVertex == 1 ? b : a;
        edgeDistance = cornerVertex == 0 ? distanceToEdge(center, a, b) : 0.0;
        baseWorld = wave.originRadius.xyz + point;
        // Keep the exposed footprint above the source surface for depth testing.
        displacedWorld = baseWorld + vec3(0.0, 0.022, 0.0);
        groundUv = surfaceUv(material, point.xz - planarCenter);
        faceNormal = vec3(0.0, 1.0, 0.0);
        facet = vec3(seam, 3.0, opening);
        gl_Position = wave.viewProjection * vec4(displacedWorld, 1.0);
        return;
    }

    int hingeIndex = int(slab.w);
    int hingeNext = (hingeIndex + 1) % int(slab.z);
    // The plate retains its tessellated outline until the crack opens.
    float footprint = 1.0 - opening * mix(0.025, 0.12, seed);
    vec3 a = surfacePoint(mix(slab.xy, boundaryA, footprint));
    vec3 b = surfacePoint(mix(slab.xy, boundaryB, footprint));
    vec3 hingeA = surfacePoint(mix(slab.xy, slabs.pieces[slabIndex].corners[hingeIndex], footprint));
    vec3 hingeB = surfacePoint(mix(slab.xy, slabs.pieces[slabIndex].corners[hingeNext], footprint));
    if (min(min(a.y, b.y), min(hingeA.y, hingeB.y)) == missing ||
        max(abs(a.y - center.y), abs(b.y - center.y)) > wave.grid.x * 2.4 ||
        max(abs(hingeA.y - center.y), abs(hingeB.y - center.y)) > wave.grid.x * 2.4) {
        hideVertex();
        return;
    }

    vec3 hinge = (hingeA + hingeB) * 0.5;
    vec3 hingeLine = hingeB - hingeA;
    float hingeLength = length(hingeLine);
    if (hingeLength < 0.0001) {
        hideVertex();
        return;
    }
    vec3 axis = hingeLine / hingeLength;
    if (cross(axis, center - hinge).y < 0.0) axis = -axis;
    float angle = min(1.10, rise * mix(0.92, 1.44, seed) / max(0.49, length(center - hinge)));
    float sine = sin(angle), cosine = cos(angle);
    float thickness = wave.grid.z * mix(0.83, 1.64, seed) *
        clamp(length(center - hinge) * 1.35, 0.70, 1.44);
    vec3 bottomA = mix(a, center, mix(0.055, 0.15, seed));
    vec3 bottomB = mix(b, center, mix(0.055, 0.15, seed));
    bottomA.y = a.y - thickness;
    bottomB.y = b.y - thickness;

    // Each boundary edge emits top, two cut-side triangles and a bottom wedge.
    const int cornerIds[verticesPerCorner] = int[verticesPerCorner](0, 2, 1, 1, 2, 3, 2, 4, 3, 5, 3, 4);
    vec3 positions[6] = vec3[6](center, a, b, bottomA, bottomB, center - vec3(0.0, thickness, 0.0));
    int vertex = cornerIds[cornerVertex];
    int face = cornerVertex < 3 ? 0 : cornerVertex < 9 ? 1 : 2;
    vec3 topNormal = normalize(cross(b - center, a - center));
    vec3 sideNormal = normalize(cross(b - a, bottomA - a));
    vec3 normal = face == 0 ? topNormal : face == 1 ? sideNormal : -topNormal;
    float throwDistance = rise * pulse * mix(0.03, 0.19, seed);
    vec3 translation = vec3(0, 0.012 * smoothstep(0.012, 0.08, rise) + rise * mix(0.14, 0.37, seed), 0);
    translation.xz = planarCenter / max(planarRadius, 0.1) * throwDistance;
    vec3 point = positions[vertex];
    vec3 displaced = hinge + rotateAround(point - hinge, axis, sine, cosine) + translation;
    vec3 base = positions[vertex == 3 ? 1 : vertex == 4 ? 2 : vertex == 5 ? 0 : vertex];
    baseWorld = wave.originRadius.xyz + base;
    displacedWorld = wave.originRadius.xyz + displaced;
    groundUv = surfaceUv(material, base.xz - planarCenter);
    edgeDistance = vertex == 0 ? distanceToEdge(center, a, b) : 0.0;
    faceNormal = rotateAround(normal, axis, sine, cosine);
    facet = vec3(visibility, float(face), opening);
    gl_Position = wave.viewProjection * vec4(displacedWorld, 1.0);
}
