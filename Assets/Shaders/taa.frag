#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 resolved;
layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D historyColor;
layout(set = 0, binding = 2) uniform sampler2D currentDepth;
layout(set = 0, binding = 3) uniform sampler2D motionTexture;
layout(std140, set = 0, binding = 4) uniform TemporalCamera {
    mat4 currentViewProjection;
    mat4 previousViewProjection;
    mat4 inverseProjection;
    vec4 parameters;
    mat4 inverseViewProjection;
    vec4 currentCameraPosition;
    vec4 previousCameraPosition;
} camera;
vec3 toYCoCg(vec3 c) { return vec3(dot(c, vec3(0.25, 0.5, 0.25)), (c.r - c.b) * 0.5, (-c.r + 2.0*c.g - c.b) * 0.25); }
vec3 fromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
void main() {
    ivec2 size = textureSize(currentColor, 0), pixel = ivec2(gl_FragCoord.xy);
    vec3 current = texelFetch(currentColor, pixel, 0).rgb;
    float depth = texelFetch(currentDepth, pixel, 0).r;
    vec4 view = camera.inverseProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    float linearDepth = depth < 1.0 ? min(abs(1.0 / view.w), 65504.0) : 65504.0;
    resolved = vec4(current, linearDepth);
    vec4 motion = texelFetch(motionTexture, pixel, 0);
    // A skybox has no surface depth or object velocity. Reproject its world
    // direction using camera rotation, without parallax from camera translation.
    if (depth >= 1.0) {
        vec4 farPoint = camera.inverseViewProjection * vec4(uv * 2.0 - 1.0, 0.9999, 1.0);
        vec3 direction = normalize(farPoint.xyz / farPoint.w - camera.currentCameraPosition.xyz);
        vec4 previous = camera.previousViewProjection * vec4(camera.previousCameraPosition.xyz + direction * 100.0, 1.0);
        if (previous.w <= 0.0) return;
        motion = vec4(previous.xy / previous.w * 0.5 + 0.5 - uv, 65504.0, 1.0);
    }
    vec2 previousUv = uv + motion.xy;
    vec2 margin = 0.5 / vec2(size);
    if (camera.parameters.y < 0.5 || motion.w < 0.5 ||
        any(lessThan(previousUv, margin)) || any(greaterThan(previousUv, 1.0 - margin))) return;
    ivec2 previousPixel = clamp(ivec2(previousUv * vec2(size)), ivec2(0), size - 1);
    float previousDepth = texelFetch(historyColor, previousPixel, 0).a;
    if (abs(previousDepth - motion.z) > max(0.025, motion.z * 0.01)) return;
    vec3 low = vec3(1e20), high = vec3(-1e20), mean = vec3(0), squareMean = vec3(0);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        vec3 c = toYCoCg(texelFetch(currentColor, clamp(pixel + ivec2(x,y), ivec2(0), size - 1), 0).rgb);
        low = min(low, c); high = max(high, c); mean += c; squareMean += c*c;
    }
    mean /= 9.0; squareMean /= 9.0;
    vec3 sigma = sqrt(max(squareMean - mean*mean, vec3(0)));
    low = max(low, mean - 1.25 * sigma); high = min(high, mean + 1.25 * sigma);
    vec3 history = clamp(toYCoCg(texture(historyColor, previousUv).rgb), low, high);
    // Keep more current information at high screen-space velocity.
    float speed = length(motion.xy * vec2(size));
    float weight = camera.parameters.x * mix(1.0, 0.65, clamp(speed / 32.0, 0.0, 1.0));
    resolved.rgb = max(mix(current, fromYCoCg(history), weight), vec3(0.0));
}
