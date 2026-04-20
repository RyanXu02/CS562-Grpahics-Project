#version 330

in vec2 vUV;
out vec4 FragColor;

// G-buffer
uniform sampler2D gWorldPos;  
uniform sampler2D gNormal;
uniform sampler2D gKd;

uniform sampler2D aoMap;

// pencil assets
uniform sampler3D pencilTones3D;
uniform sampler2D paperNormalMap;

// Lighting
uniform vec3 Light;
uniform vec3 Ambient;
uniform vec3 lightPos;
uniform vec3 eyePos;


uniform float pencilTile;
uniform float paperStrength; // mu_p in eq 5
uniform float paperTile;
uniform float crossHatchBelow;

const float PI = 3.14159265358979323846;


vec2 Rotate2D(vec2 p, float angle)
{
    float s = sin(angle), c = cos(angle);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}


float SamplePencil(vec2 uv, float angle, float darkness)
{
    // Rotate around the screen center so tiling stays stable as the camera moves
    // Rotating around (0,0) would shift the tile origin with the rotation
    vec2 centered = uv - 0.5;
    vec2 rotated  = Rotate2D(centered, angle);
    vec2 tiled    = rotated * pencilTile + 0.5;
    return texture(pencilTones3D, vec3(tiled, clamp(darkness, 0.0, 1.0))).r;
}

void main()
{
    vec4 G0 = texture(gWorldPos, vUV);
    if (G0.a < 0.5) {
        FragColor = vec4(1.0);
        return; //sky
    }

    vec3  P      = G0.xyz;
    vec4  Nw     = texture(gNormal, vUV);
    vec3  N      = normalize(Nw.xyz);
    float thetaN = Nw.w;                 // [0,1], 1 means theta=PI
    float theta  = thetaN * PI;          // back to radians in [0, PI)

    vec3 Kd = pow(texture(gKd, vUV).rgb, vec3(1.0 / 2.2)); // srgb to linear because too bright
    float ao = texture(aoMap, vUV).r;

    // 5.2 brightness
    vec3  L  = normalize(lightPos - P);
    float NL = max(dot(N, L), 0.0);

    // want to use kd, so use rec 709 coefficients to get a single brightness value
    vec3  diffuse = (Ambient + Light * NL) * Kd * ao;
    float brightness = dot(diffuse, vec3(0.2126, 0.7152, 0.0722));
    brightness = clamp(brightness, 0.0, 1.0);
    float darkness = 1.0 - brightness;


    float tone = SamplePencil(vUV, -theta, darkness);
    if (crossHatchBelow > 0.0 && brightness < crossHatchBelow) {
        float hatchMix = smoothstep(crossHatchBelow, 0.0, brightness);
        float tone2 = SamplePencil(vUV, -theta + 0.5 * PI, darkness);
        tone = mix(tone, min(tone, tone2), hatchMix);
    }

    // eq5
    vec2 strokeDir2D = vec2(cos(theta), sin(theta));
    vec3 paperN = texture(paperNormalMap, vUV * paperTile).xyz * 2.0 - 1.0;
    float paperMod = paperStrength * dot(strokeDir2D, paperN.xy);
    tone = clamp(tone + paperMod, 0.0, 1.0);

    FragColor = vec4(tone, tone, tone, 1.0);
}
