/////////////////////////////////////////////////////////////////////////
// Pixel shader for lighting
////////////////////////////////////////////////////////////////////////
#version 330

out vec4 FragColor;

// These definitions agree with the ObjectIds enum in scene.h
const int     nullId	= 0;
const int     skyId	    = 1;
const int     seaId	    = 2;
const int     groundId	= 3;
const int     roomId	= 4;
const int     boxId	    = 5;
const int     frameId	= 6;
const int     lPicId	= 7;
const int     rPicId	= 8;
const int     teapotId	= 9;
const int     spheresId	= 10;
const int     floorId	= 11;

const float PI = 3.14159265358979323846264338327950;

in vec3 normalVec, lightVec, eyeVec, tanVec;
in vec2 texCoord;
in vec4 shadowCoord;

uniform sampler2D tex;
uniform int hasTexture;

uniform sampler2D normalMap;
uniform int hasNormalMap;

uniform sampler2D shadowMap; // binded on 3

uniform int objectId;

uniform vec3 diffuse; // Kd
uniform vec3 specular; // Ks
uniform float shininess; // alpha exponent

uniform vec3 Light; // Ii
uniform vec3 Ambient; // Ia

uniform float time;

float distanceToMandelbrot( in vec2 c )
{
    // iterate
    float di =  1.0;
    vec2 z  = vec2(0.0);
    float m2 = 0.0;
    vec2 dz = vec2(0.0);
    for( int i=0; i<300; i++ )
    {
        if( m2>1024.0 ) { di=0.0; break; }

		// Z' -> 2·Z·Z' + 1
        dz = 2.0*vec2(z.x*dz.x-z.y*dz.y, z.x*dz.y + z.y*dz.x) + vec2(1.0,0.0);
			
        // Z -> Z² + c			
        z = vec2( z.x*z.x - z.y*z.y, 2.0*z.x*z.y ) + c;
			
        m2 = dot(z,z);
    }

    // distance	
	// d(c) = |Z|·log|Z|/|Z'|
	float d = 0.5*sqrt(dot(z,z)/dot(dz,dz))*log(dot(z,z));
    //if( di>0.5 ) d=0.0;
	
    return d;
}


void main()
{

    vec3 N = normalize(normalVec);
    vec3 L = normalize(lightVec);
    vec3 V = normalize(eyeVec);

    vec3 T = normalize(tanVec);
    vec3 B = normalize(cross(T,N));

    // skydome
    if (objectId == skyId && hasTexture == 1) {
        vec3 dir = normalize(V);
        vec2 skyUV = vec2(-atan(dir.y, dir.x) / (2.0*PI), acos(dir.z) / PI);
        skyUV.x = skyUV.x - floor(skyUV.x);
        FragColor = texture(tex, skyUV);
        return;
    }

    // pictures
    if (objectId == lPicId || objectId == rPicId) {
        vec2 minUV = vec2(0.1);
        vec2 maxUV = vec2(0.9);

        // we're at gray border area, just gray
        if (any(lessThan(texCoord, minUV)) || any(greaterThan(texCoord, maxUV))) {
            FragColor = vec4(0.5,0.5,0.5, 1.0);
            return;
        }
        // the actual pictures
        vec2 PicUV = (texCoord - minUV) / (maxUV - minUV);
        // left picture use texture
        if (objectId == lPicId) {
            FragColor = texture(tex, PicUV);
        }
        // right picture use fancy procedural texture
        // credit: https://www.shadertoy.com/view/lsX3W4
        else {  
            vec2 p = 2.0 * PicUV - vec2(1.0);

            // animation	
	        float tz = 0.5 - 0.5*cos(0.225*time);
            float zoo = pow( 0.5, 13.0*tz );
	        vec2 c = vec2(-0.05,.6805) + p*zoo;

            // distance to Mandelbrot
            float d = distanceToMandelbrot(c);
    
            // do some soft coloring based on distance
	        d = clamp( pow(4.0*d/zoo,0.2), 0.0, 1.0 );
            //d =pow(d,.1);
            //d = 1.0-1.0/(1.0+1000.0*d);
    
            vec3 col = vec3(d);
    
            FragColor = vec4( col, 1.0 );
        }
        return;
    }
    
    // for different texture sizes
    float tile = 1.0;
    switch (objectId) {
        case boxId:     tile = 1.0; break;
        case groundId:  tile = 16.0; break;
        case floorId:   tile = 4.0; break;
        case roomId:    tile = 32.0; break;
        case seaId:     tile = 256.0; break;
    }
    
    // sample texture
    vec4 color = vec4(1.0);
    if (hasTexture == 1) {
        color  = texture(tex, texCoord*tile);
    }
    vec3 Kd = (hasTexture == 1) ? color.rgb : diffuse;

    // nomral map
    if (hasNormalMap == 1){
        vec2 uv = texCoord * tile;
        // animate water normal (Josh method)
        if (objectId == seaId){
            vec2 uv1 = uv + time * vec2( 0.01,  0.04);
            vec2 uv2 = uv + time * vec2(-0.03,  -0.02);

            vec3 d1 = texture(normalMap, uv1).xyz * 2.0-vec3(1,1,1);
            vec3 d2 = texture(normalMap, uv2).xyz * 2.0-vec3(1,1,1);

            vec3 n1 = normalize(d1.x*T + d1.y*B + d1.z*N);
            vec3 n2 = normalize(d2.x*T + d2.y*B + d2.z*N);

            N = normalize(0.5 * (n1 + n2));
        }
        else{
            vec3 delta = texture(normalMap, uv).xyz * 2.0-vec3(1,1,1);
            N = normalize(delta.x*T + delta.y*B + delta.z*N);
        }
    }

    // water reflection
    if (objectId == seaId) {
        vec3 R = -(2*dot(V,N)*N-V);
        vec2 seaUV = vec2(-atan(R.y, R.x) / (2.0*PI), acos(R.z) / PI);
        FragColor = texture(tex, seaUV);
        return;
    }
    
    vec3 Ks = specular;
    float alpha = shininess;
    vec3 Ii = Light;
    vec3 Ia = Ambient;

    // A checkerboard pattern to break up large flat expanses.  Remove when using textures.
    // if (objectId==groundId || objectId==floorId || objectId==seaId) {
    //     ivec2 uv = ivec2(floor(100.0*texCoord));
    //     if ((uv[0]+uv[1])%2==0)
    //        Kd *= 0.9; }

    vec3 H = normalize(L+V);
    float LN = max(dot(L,N),0.0);
    float HN = max(dot(H,N),0.0);
    float LH = max(dot(L,H),0.0);

    vec3 F = Ks + (vec3(1,1,1)-Ks)*pow((1-LH),5); // Schlick approximation to the Fresnel term F
    float G = 1 / pow(LH,2); // masking term G and part of the denominator lumped together
    float D = ((alpha+2) / (2*PI))*pow(HN,alpha); // normal distribution term D


    // shadows
    vec2 shadowIndex = shadowCoord.xy / shadowCoord.w;
    bool inRange = shadowCoord.w > 0.0 &&
                    all(greaterThanEqual(shadowIndex, vec2(0.0))) &&
                    all(lessThanEqual(shadowIndex, vec2(1.0)));

    float shadowed = 0.0; // 0.0 = lit, 1.0 = in shadow
    if (inRange) {
        float lightDepth = texture2D(shadowMap, shadowIndex).w;
        float pixelDepth = shadowCoord.w;
        shadowed = ((pixelDepth-0.005) > lightDepth) ? 1.0 : 0.0;
    }
    // microfacet BRDF
    vec3 lit = Ii*LN*( (Kd/PI)+(F*G*D/4) );

    FragColor.xyz = Ia*Kd + lit * (1.0 - shadowed); // if shadowed, only ambient
}
