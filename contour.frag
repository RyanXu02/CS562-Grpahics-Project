#version 330
// Output: intensity in [0,1] 1 = clean paper, darker values = pencil strokes

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D gWorldPos;       // xyz = world pos, w = isGeometry
uniform sampler2D gNormal;         // xyz = world normal, w = angle

// pencil material texture from pencil_preprocess
uniform sampler2D contourPencilTex;

uniform vec3  lightPos;
uniform vec2  screenSize;

uniform float normalThreshold;
uniform float depthThreshold;
uniform float shakeAmp;
uniform float shakeFreq;
uniform int   numShakes;
uniform float pencilTile;

const float PI = 3.14159265358979323846;


vec2 ShakeOffset(vec2 uv, int i)
{
    float fi = float(i);
    float b1 = shakeFreq + fi * 7.3;
    float b2 = shakeFreq + fi * 5.1 + 2.0;
    float c1 = fi * 1.73;
    float c2 = fi * 2.41 + 1.1;
    return vec2(shakeAmp * sin(b1 * uv.y + c1),
                shakeAmp * sin(b2 * uv.x + c2));
}


float DetectEdge(vec2 uv)
{
    vec4 G0 = texture(gWorldPos, uv);
    if (G0.a < 0.5) return 0.0; // sky

    vec3 P0 = G0.xyz;
    vec3 N0 = normalize(texture(gNormal, uv).xyz);

    vec2 texel = 1.0 / screenSize;
    float minNdot       = 1.0;   // 1 = aligned, drops toward 0 on crease
    float maxDepthDiff  = 0.0;
    float silhouette    = 0.0;   // 1 if any neighbor is sky

    // 8 neighbor sweep
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            vec2 nuv = uv + vec2(dx, dy) * texel;
            vec4 G = texture(gWorldPos, nuv);

            // Neighbor is sky so we're sitting on the outer silhouette.
            if (G.a < 0.5) { silhouette = 1.0; continue; }

            vec3 Nn = normalize(texture(gNormal, nuv).xyz);
            minNdot      = min(minNdot, dot(N0, Nn));
            maxDepthDiff = max(maxDepthDiff, length(G.xyz - P0));
        }
    }

    float normalEdge = smoothstep(normalThreshold + 0.2,
                                  normalThreshold,
                                  minNdot);
    float depthEdge  = smoothstep(depthThreshold * 0.5,
                                  depthThreshold,
                                  maxDepthDiff);

    return max(max(normalEdge, depthEdge), silhouette);
}

void main()
{
    vec4 G0 = texture(gWorldPos, vUV);
    if (G0.a < 0.5) {
        FragColor = vec4(1.0);
        return; //sky
    }
    vec3 P = G0.xyz;
    vec3 N = normalize(texture(gNormal, vUV).xyz);

    // shake sampling
    float overlap = 0.0;
    for (int i = 0; i < numShakes; ++i) {
        overlap += DetectEdge(vUV + ShakeOffset(vUV, i));
    }
    overlap /= float(numShakes);

    // Paper eq 1
    vec3  L       = normalize(lightPos - P);
    float LN      = max(dot(N, L), 0.0);
    float lightMod = mix(1.0, 0.4, LN);

    float pencilTone   = texture(contourPencilTex, vUV * pencilTile).r;
    float pencilFactor = 0.5 + 0.5 * (1.0 - pencilTone);

    float strokeAmount = overlap * lightMod * pencilFactor;
    float intensity    = clamp(1.0 - strokeAmount, 0.0, 1.0);
    FragColor = vec4(intensity, intensity, intensity, 1.0);
}
