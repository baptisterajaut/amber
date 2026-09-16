#version 440

layout(std140, binding = 1) uniform FragParams {
    vec2  resolution;
    float centerX;
    float centerY;
    float maskRadius;
    float maskSoftness;
    float intensity;
    bool  pixelateMode;
};

layout(binding = 2) uniform sampler2D sceneTex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
    if (maskRadius == 0.0) {
        discard;
    }

    // 1. Fetch aspect ratio multiplier scalar (e.g. 1.777 for standard 16:9 widescreen)
    float ar = resolution.x / resolution.y;

    // ====================================================================
    // THE NATIVE SPACE REBALANCING MATRIX:
    // Translates your 0-100 absolute input convention perfectly to OpenGL texture 
    // dimensions (0.0-1.0) while multiplying BOTH by the aspect scalar 
    // to preserve a perfectly round circular mask anywhere on screen!
    // ====================================================================
    vec2 aspectCoord = vec2(vTexCoord.x * ar, vTexCoord.y);
    vec2 aspectCenter = vec2((centerX * 0.01) * ar, centerY * 0.01);

    // 2. Measure precise spatial length between vectors
    float dist = distance(aspectCoord, aspectCenter);
    float size = maskRadius * 0.01;

    // 3. The smoothstep vignette boundary falloff matrix
    float innerEdge = size * (1.0 - maskSoftness * 0.01);
    float maskFactor = smoothstep(size, innerEdge, dist);

    // Pull crisp clear base sample frames out from timeline context
    vec4 sourceColor = texture(sceneTex, vTexCoord);
    vec4 censoredColor = vec4(0.0);

    // 4. GPU Censorship Execution Branches
    if (pixelateMode) {
        // Architecture A: The Censor Mosaic Pixelation Grid
        float blocks = max(4.0, 200.0 - intensity * 1.8);
        vec2 pixelatedCoord = vec2(
            floor(vTexCoord.x * blocks) / blocks,
            floor(vTexCoord.y * (blocks / ar)) / (blocks / ar)
        );
        censoredColor = texture(sceneTex, pixelatedCoord);
    } else {
        // Architecture B: Real-Time Widescreen Box Blur Cross-Sampling Pass
        float blurScale = (intensity * 0.0003);
        vec4 blurAccumulator = vec4(0.0);
        
        blurAccumulator += texture(sceneTex, vTexCoord + vec2(-1.0, -1.0) * blurScale);
        blurAccumulator += texture(sceneTex, vTexCoord + vec2( 0.0, -1.0) * blurScale);
        blurAccumulator += texture(sceneTex, vTexCoord + vec2( 1.0, -1.0) * blurScale);
        blurAccumulator += texture(sceneTex, vTexCoord + vec2(-1.0,  0.0) * blurScale);
        blurAccumulator += texture(sceneTex, vTexCoord + vec2( 0.0,  0.0) * blurScale);
        blurAccumulator += texture(sceneTex, vTexCoord + vec2( 1.0,  0.0) * blurScale);
        blurAccumulator += texture(sceneTex, vTexCoord + vec2(-1.0,  1.0) * blurScale);
        blurAccumulator += texture(sceneTex, vTexCoord + vec2( 0.0,  1.0) * blurScale);
        blurAccumulator += texture(sceneTex, vTexCoord + vec2( 1.0,  1.0) * blurScale);
        
        censoredColor = blurAccumulator / 9.0;
    }

    // 5. Seamlessly blend the source footage with censored pixels using smoothstep!
    fragColor = mix(sourceColor, censoredColor, maskFactor);
}