#ifndef STEER_PORTAL_WAVE_MASK_GLSL
#define STEER_PORTAL_WAVE_MASK_GLSL

vec3 waveWorldPosition(vec2 uv, float depth, mat4 worldFromClip) {
    vec4 world = worldFromClip * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return world.xyz / world.w;
}

float waveCellMask(sampler2D waveCells, vec3 world, vec3 radial, float textureScale) {
    vec3 weights = abs(radial) + 0.0001;
    weights *= weights;
    weights /= weights.x + weights.y + weights.z;
    vec3 tiles = world * textureScale;
    return dot(weights, vec3(textureLod(waveCells, tiles.yz, 1.0).r,
        textureLod(waveCells, tiles.xz + vec2(0.34, 0.73), 1.0).r,
        textureLod(waveCells, tiles.xy + vec2(0.62, 0.18), 1.0).r));
}

vec2 portalWaveMask(vec2 uv, sampler2D sceneDepth, sampler2D waveCells, mat4 worldFromClip,
    vec4 originRadius, vec3 shape, vec4 clipPlane) {
    ivec2 extent = textureSize(sceneDepth, 0);
    ivec2 pixel = clamp(ivec2(uv * vec2(extent)), ivec2(0), extent - 1);
    vec2 depthUv = (vec2(pixel) + 0.5) / vec2(extent);
    float depth = texelFetch(sceneDepth, pixel, 0).r;
    vec3 surface = waveWorldPosition(depthUv, min(depth, 0.999999), worldFromClip);
    vec3 radial = surface - originRadius.xyz;
    float radius = originRadius.w;
    float width = shape.x;
    float distanceFromFront = length(radial) - radius;

    if (depth < 1.0 && abs(distanceFromFront) < width * 1.7) {
        float cells = waveCellMask(waveCells, surface, radial, shape.z);
        // Move the front in and out in world units and break up its hottest edge.
        float front = distanceFromFront / width + (cells - 0.5) * 1.55;
        float band = 1.0 - smoothstep(0.13, 1.0, abs(front));
        float core = 1.0 - smoothstep(0.02, 0.28, abs(front));
        float coverage = mix(0.13, 1.0, smoothstep(0.24, 0.72, cells));
        return vec2(band * coverage, core * smoothstep(0.37, 0.69, cells));
    }

    // A faint grazing edge also shows the hemisphere between scene surfaces.
    vec3 nearPoint = waveWorldPosition(depthUv, 0.0, worldFromClip);
    vec3 segment = surface - nearPoint;
    float segmentLength = length(segment);
    vec3 ray = segment / max(segmentLength, 0.0001);
    vec3 toCenter = originRadius.xyz - nearPoint;
    float along = dot(toCenter, ray);
    vec3 closest = ray * along - toCenter;
    float discriminant = radius * radius - dot(closest, closest);
    if (discriminant <= 0.0) return vec2(0.0);
    float chord = sqrt(discriminant);
    float hit = along - chord;
    if (hit <= 0.0 || hit >= segmentLength) return vec2(0.0);
    vec3 shellPoint = nearPoint + hit * ray;
    if (dot(clipPlane, vec4(shellPoint, 1.0)) < 0.0) return vec2(0.0);
    float rim = 1.0 - smoothstep(0.03, 0.50, chord / radius);
    float cells = waveCellMask(waveCells, shellPoint, shellPoint - originRadius.xyz, shape.z);
    return vec2(rim * smoothstep(0.26, 0.78, cells) * 0.23, 0.0);
}

vec3 applyPortalWaveColor(vec3 color, vec2 mask) {
    vec3 cooled = color * vec3(0.74, 0.87, 0.94) + vec3(0.025, 0.068, 0.102);
    color = mix(color, cooled, mask.x * 0.68);
    return mix(color, vec3(0.46, 0.74, 0.84), mask.y * 0.18);
}

#endif
