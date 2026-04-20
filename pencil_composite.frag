#version 330

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D contourMap;
uniform sampler2D interiorMap;
uniform sampler2D paperNormalMap;
uniform sampler2D gWorldPos;
uniform sampler2D deferredMap;

uniform float paperTile;
uniform vec3 paperColor;
uniform float contrastAmount; // eq6

void main()
{
    float isGeometry = texture(gWorldPos, vUV).w;
    if (isGeometry < 0.5) {
        FragColor = texture(deferredMap, vUV);
        return; // sky
    }

    float contour  = texture(contourMap,  vUV).r;  // 1 = clean, <1 = stroke
    float interior = texture(interiorMap, vUV).r;  // 1 = bright, <1 = shaded


    float interiorAtContour = mix(interior, 1.0, (1.0 - contour) * 0.5);
    float pencil = contour * interiorAtContour;

    vec3  paperNrm   = texture(paperNormalMap, vUV * paperTile).xyz * 2.0 - 1.0;
    float paperTooth = 0.92 + 0.08 * paperNrm.z;

    vec3 result = paperColor * pencil * paperTooth;

    // eq 6
    if (contrastAmount > 0.0) {
        vec3 darkened   = result * result;
        vec3 brightened = sqrt(result);
        vec3 enhanced   = mix(darkened, brightened, result);
        result = mix(result, enhanced, contrastAmount);
    }

    FragColor = vec4(result, 1.0);
}
