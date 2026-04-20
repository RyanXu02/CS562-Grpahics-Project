////////////////////////////////////////////////////////////////////////
// The scene class contains all the parameters needed to define and
// draw a simple scene, including:
//   * Geometry
//   * Light parameters
//   * Material properties
//   * viewport size parameters
//   * Viewing transformation values
//   * others ...
//
// Some of these parameters are set when the scene is built, and
// others are set by the framework in response to user mouse/keyboard
// interactions.  All of them can be used to draw the scene.

#include "math.h"
#include <iostream>
#include <stdlib.h>

#include <glbinding/gl/gl.h>
#include <glbinding/Binding.h>
using namespace gl;

#include <glu.h>                // For gluErrorString

#define GLM_FORCE_CTOR_INIT
#define GLM_FORCE_RADIANS
#define GLM_SWIZZLE
#include <glm/glm.hpp>
#include <glm/ext.hpp>          // For printing GLM objects with to_string

#include "framework.h"
#include "shapes.h"
#include "object.h"
#include "texture.h"
#include "transform.h"

#include <random>               // For generating many lights
static float Random(float minValue, float maxValue)
{
    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(rng);
}
const bool fullPolyCount = true; // Use false when emulating the graphics pipeline in software

const float PI = 3.14159f;
const float rad = PI/180.0f;    // Convert degrees to radians

glm::mat4 Identity(1.0);

const float grndSize = 100.0;    // Island radius;  Minimum about 20;  Maximum 1000 or so
const float grndOctaves = 4.0;  // Number of levels of detail to compute
const float grndFreq = 0.03;    // Number of hills per (approx) 50m
const float grndPersistence = 0.03; // Terrain roughness: Slight:0.01  rough:0.05
const float grndLow = -3.0;         // Lowest extent below sea level
const float grndHigh = 5.0;        // Highest extent above sea level

float shadowFBOres = 4096.0;

////////////////////////////////////////////////////////////////////////
// This macro makes it easy to sprinkle checks for OpenGL errors
// throughout your code.  Most OpenGL calls can record errors, and a
// careful programmer will check the error status *often*, perhaps as
// often as after every OpenGL call.  At the very least, once per
// refresh will tell you if something is going wrong.
#define CHECKERROR {GLenum err = glGetError(); if (err != GL_NO_ERROR) { fprintf(stderr, "OpenGL error (at line scene.cpp:%d): %s\n", __LINE__, gluErrorString(err)); exit(-1);} }

// Create an RGB color from human friendly parameters: hue, saturation, value
glm::vec3 HSV2RGB(const float h, const float s, const float v)
{
    if (s == 0.0)
        return glm::vec3(v,v,v);

    int i = (int)(h*6.0) % 6;
    float f = (h*6.0f) - i;
    float p = v*(1.0f - s);
    float q = v*(1.0f - s*f);
    float t = v*(1.0f - s*(1.0f-f));
    if      (i == 0)     return glm::vec3(v,t,p);
    else if (i == 1)  return glm::vec3(q,v,p);
    else if (i == 2)  return glm::vec3(p,v,t);
    else if (i == 3)  return glm::vec3(p,q,v);
    else if (i == 4)  return glm::vec3(t,p,v);
    else   /*i == 5*/ return glm::vec3(v,p,q);
}

////////////////////////////////////////////////////////////////////////
// Constructs a hemisphere of spheres of varying hues
Object* SphereOfSpheres(Shape* SpherePolygons)
{
    Object* ob = new Object(NULL, nullId);
    
    for (float angle=0.0;  angle<360.0;  angle+= 18.0)
        for (float row=0.075;  row<PI/2.0;  row += PI/2.0/6.0) {   
            glm::vec3 hue = HSV2RGB(angle/360.0, 1.0f-2.0f*row/PI, 1.0f);

            Object* sp = new Object(SpherePolygons, spheresId,
                                    hue, glm::vec3(0.0001, 0.0001, 0.0001), 1000.0);
            float s = sin(row);
            float c = cos(row);
            ob->add(sp, Rotate(2,angle)*Translate(c,0,s)*Scale(0.075*c,0.075*c,0.075*c));
        }
    return ob;
}

////////////////////////////////////////////////////////////////////////
// Constructs a -1...+1  quad (canvas) framed by four (elongated) boxes
Object* FramedPicture(const glm::mat4& modelTr, const int objectId, 
                      Shape* BoxPolygons, Shape* QuadPolygons, Texture* texure, Texture* normal, Texture* display=NULL, Texture* displayNormal=NULL)
{
    // This draws the frame as four (elongated) boxes of size +-1.0
    float w = 0.05;             // Width of frame boards.
    
    Object* frame = new Object(NULL, nullId);
    Object* ob;
    
    glm::vec3 woodColor(87.0/255.0,51.0/255.0,35.0/255.0);
    ob = new Object(BoxPolygons, frameId,
                    woodColor, glm::vec3(0.2, 0.2, 0.2), 10.0
                    );
    frame->add(ob, Translate(0.0, 0.0, 1.0+w)*Scale(1.0, w, w));
    frame->add(ob, Translate(0.0, 0.0, -1.0-w)*Scale(1.0, w, w));
    frame->add(ob, Translate(1.0+w, 0.0, 0.0)*Scale(w, w, 1.0+2*w));
    frame->add(ob, Translate(-1.0-w, 0.0, 0.0)*Scale(w, w, 1.0+2*w));

    ob = new Object(QuadPolygons, objectId,
                    woodColor, glm::vec3(0.0, 0.0, 0.0), 10.0,
                    display, displayNormal);
    frame->add(ob, Rotate(0,90));

    return frame;
}

////////////////////////////////////////////////////////////////////////
// InitializeScene is called once during setup to create all the
// textures, shape VAOs, and shader programs as well as setting a
// number of other parameters.
void Scene::InitializeScene()
{
#pragma region Initialization
    glEnable(GL_DEPTH_TEST);
    CHECKERROR;

    // @@ Initialize interactive viewing variables here. (spin, tilt, ry, front back, ...)
    spin = 0.0;
    tilt = 30.0;
    tx = 0.0;
    ty = 0.0;
    zoom = 25.0;
    ry = 0.4;
    front = 0.5;
    back = 5000.0;

    WorldProj = glm::mat4(0.0);
    WorldView = glm::mat4(0.0);

    eye = glm::vec3(0.0, -20.0, 0.0);
    speed = 10.0;
    step = 0.0;

    lastRefreshTime = 0.0;
    
    // Set initial light parameters
    lightSpin = 150.0;
    lightTilt = -45.0;
    lightDist = 100.0;
    // @@ Perhaps initialize additional scene lighting values here. (lightVal, lightAmb)
    Light = glm::vec3(3.0, 3, 3);
	Ambient = glm::vec3(0.4, 0.4, 0.4);
    onlyLocalLights = 0;
	noLocalLight = 1;
    noDirectLight = 0;
    // ao parameters
    aoRadius = 2.0f;
    aoSamples = 64;
    aoScale = 3.5f;
    aoContrast = 2.5f;
    aoDelta = 0.001f;
    aoSigma = 0.01f;
    aoBlurRadius = 5;
#pragma endregion

#pragma region LocalLights
    for (int i = 0; i < localLightCount; ++i)
    {
        LocalLight L;
        L.position = glm::vec3(
            Random(-20.0f, 20.0f),
            Random(-20.0f, 20.0f),
            Random(0.0f, 20.0f));
        L.color = glm::vec3(
            Random(0.5f, 50.0f),
            Random(0.5f, 50.0f),
            Random(0.5f, 50.0f));
        L.radius = Random(1.0f, 10.0f);
        localLights.push_back(L);
    }
    LocalLight L0;
    L0.position = glm::vec3(0.0f, 0.0f, 3.0f);
    L0.color = glm::vec3(20.0f, 20.0f, 20.0f);
    L0.radius = 5.0f;
    LocalLight L1;
	L1.position = glm::vec3(5.0f, 5.0f, 1.0f);
	L1.color = glm::vec3(10.0f, 10.0f, 10.0f);
	L1.radius = 5.0f;
	LocalLight L2;
	L2.position = glm::vec3(-5.0f, 5.0f, 1.0f);
	L2.color = glm::vec3(10.0f, 10.0f, 10.0f);
	L2.radius = 5.0f;
    LocalLight L3;
	L3.position = glm::vec3(5.0f, -5.0f, 1.0f);
	L3.color = glm::vec3(10.0f, 10.0f, 10.0f);
	L3.radius = 5.0f;
    LocalLight L4;
	L4.position = glm::vec3(-5.0f, -5.0f, 1.0f);
	L4.color = glm::vec3(10.0f, 10.0f, 10.0f);
    L4.radius = 5.0f;
	localLights.push_back(L0);
    localLights.push_back(L1);
	localLights.push_back(L2);
	localLights.push_back(L3);
	localLights.push_back(L4);

    z0 = lightDist - 120.0f;
	z1 = lightDist + 120.0f;
#pragma endregion
    CHECKERROR;
    objectRoot = new Object(NULL, nullId);

    
    // Enable OpenGL depth-testing
    glEnable(GL_DEPTH_TEST);

#pragma region Shaders
    // Create the lighting shader program from source code files.
    // @@ Initialize additional shaders if necessary

	// shadow shader program
    shadowProgram = new ShaderProgram();
	shadowProgram->AddShader("shadow.vert", GL_VERTEX_SHADER);
	shadowProgram->AddShader("shadow.frag", GL_FRAGMENT_SHADER);
    glBindAttribLocation(shadowProgram->programId, 0, "vertex");
    shadowProgram->LinkProgram();
	// Create shadow FBO
	shadowFBO.CreateFBO(shadowFBOres, shadowFBOres);

    // shadow blur shader programs
    blurHProgram = new ShaderProgram();
    blurHProgram->AddShader("blur_h.comp", GL_COMPUTE_SHADER);
    blurHProgram->LinkProgram();
    blurVProgram = new ShaderProgram();
    blurVProgram->AddShader("blur_v.comp", GL_COMPUTE_SHADER);
    blurVProgram->LinkProgram();
    shadowDebugProgram = new ShaderProgram();
    shadowDebugProgram->AddShader("gbuffer_debug.vert", GL_VERTEX_SHADER); // resuse gbuffer debug vert shader is ok
    shadowDebugProgram->AddShader("shadow_debug.frag", GL_FRAGMENT_SHADER);
    shadowDebugProgram->LinkProgram();
	// Create shadow blur FBO
    shadowBlurFBO.CreateFBO(shadowFBOres, shadowFBOres);

    // gbuffer shader program
    gbufferProgram = new ShaderProgram();
    gbufferProgram->AddShader("gbuffer.vert", GL_VERTEX_SHADER);
    gbufferProgram->AddShader("gbuffer.frag", GL_FRAGMENT_SHADER);
    glBindAttribLocation(gbufferProgram->programId, 0, "vertex");
    glBindAttribLocation(gbufferProgram->programId, 1, "vertexNormal");
    glBindAttribLocation(gbufferProgram->programId, 2, "vertexTexture");
    glBindAttribLocation(gbufferProgram->programId, 3, "vertexTangent");
    glBindAttribLocation(gbufferProgram->programId, 4, "vertexCurvDir");
    gbufferProgram->LinkProgram();

	// gbuffer debug shader program
    gbufferDebugProgram = new ShaderProgram();
    gbufferDebugProgram->AddShader("gbuffer_debug.vert", GL_VERTEX_SHADER);
    gbufferDebugProgram->AddShader("gbuffer_debug.frag", GL_FRAGMENT_SHADER);
    gbufferDebugProgram->LinkProgram();
	// create gbuffer FBO
    gbufferFBO.CreateGbufferFBO(width, height);
	// create gbuffer debug VAO
    glGenVertexArrays(1, &gbufferDebugVAO);

	// deferred shading shader program
    deferredProgram = new ShaderProgram();
    deferredProgram->AddShader("deferred.vert", GL_VERTEX_SHADER);
    deferredProgram->AddShader("deferred.frag", GL_FRAGMENT_SHADER);
    deferredProgram->LinkProgram();
	// create deferred shading VAO
    glGenVertexArrays(1, &deferredVAO);

    // AO program
    aoProgram = new ShaderProgram();
    aoProgram->AddShader("deferred.vert", GL_VERTEX_SHADER);  // reuse fullscreen-triangle VS
    aoProgram->AddShader("ao.frag", GL_FRAGMENT_SHADER);
    aoProgram->LinkProgram();
    // bilateral blur programs for AO
    aoBlurHProgram = new ShaderProgram();
    aoBlurHProgram->AddShader("ao_blur_h.comp", GL_COMPUTE_SHADER);
    aoBlurHProgram->LinkProgram();
    aoBlurVProgram = new ShaderProgram();
    aoBlurVProgram->AddShader("ao_blur_v.comp", GL_COMPUTE_SHADER);
    aoBlurVProgram->LinkProgram();
    // AO fbos
    aoFBO.CreateAoFBO(width, height);
    aoBlurFBO.CreateAoFBO(width, height);
    glGenVertexArrays(1, &aoVAO);
    // AO debug shader
    aoDebugProgram = new ShaderProgram();
    aoDebugProgram->AddShader("gbuffer_debug.vert", GL_VERTEX_SHADER);
    aoDebugProgram->AddShader("ao_debug.frag", GL_FRAGMENT_SHADER);
    aoDebugProgram->LinkProgram();


#pragma region pencil rendering
    // Pencil contour pass
    contourProgram = new ShaderProgram();
    contourProgram->AddShader("deferred.vert", GL_VERTEX_SHADER);
    contourProgram->AddShader("contour.frag", GL_FRAGMENT_SHADER);
    contourProgram->LinkProgram();
    contourDebugProgram = new ShaderProgram();
    contourDebugProgram->AddShader("deferred.vert", GL_VERTEX_SHADER);
    contourDebugProgram->AddShader("contour_debug.frag", GL_FRAGMENT_SHADER);
    contourDebugProgram->LinkProgram();
    // AO FBO has R32F single color attachment no depth
    contourFBO.CreateAoFBO(width, height);
    CHECKERROR;

    // Pencil interior pass
    interiorProgram = new ShaderProgram();
    interiorProgram->AddShader("deferred.vert", GL_VERTEX_SHADER);
    interiorProgram->AddShader("interior.frag", GL_FRAGMENT_SHADER);
    interiorProgram->LinkProgram();
    interiorDebugProgram = new ShaderProgram();
    interiorDebugProgram->AddShader("deferred.vert", GL_VERTEX_SHADER);
    interiorDebugProgram->AddShader("interior_debug.frag", GL_FRAGMENT_SHADER);
    interiorDebugProgram->LinkProgram();
    // AO FBO has R32F single color attachment no depth
    interiorFBO.CreateAoFBO(width, height);
    CHECKERROR;

    // Pencil composition
    pencilCompositeProgram = new ShaderProgram();
    pencilCompositeProgram->AddShader("deferred.vert", GL_VERTEX_SHADER);
    pencilCompositeProgram->AddShader("pencil_composite.frag", GL_FRAGMENT_SHADER);
    pencilCompositeProgram->LinkProgram();
    // 
    deferredOutputFBO.CreateFBO(width, height);
    CHECKERROR;

#pragma endregion

	// local light shader program
	localLightProgram = new ShaderProgram();
	localLightProgram->AddShader("local_light.vert", GL_VERTEX_SHADER);
	localLightProgram->AddShader("local_light.frag", GL_FRAGMENT_SHADER);
    glBindAttribLocation(localLightProgram->programId, 0, "vertex");
	localLightProgram->LinkProgram();
#pragma endregion

#pragma region Hammersley
    const int N_SAMPLES = 40;
    struct {
        float N;
        float hammersley[2 * 100];  // sized for max 100 samples
    } block;
    block.N = float(N_SAMPLES);

    int pos = 0;
    for (int k = 0; k < N_SAMPLES; k++) {
        float u = 0.0f;
        int kk = k;
        for (float p = 0.5f; kk; p *= 0.5f, kk >>= 1) {
            if (kk & 1) u += p;
        }
        float v = (k + 0.5f) / float(N_SAMPLES);
        block.hammersley[pos++] = u;
        block.hammersley[pos++] = v;
    }

    unsigned int hammersleyBufferId;
    glGenBuffers(1, &hammersleyBufferId);
    const unsigned int bindpoint = 1;
    glBindBufferBase(GL_UNIFORM_BUFFER, bindpoint, hammersleyBufferId);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(block), &block, GL_STATIC_DRAW);

    int loc = glGetUniformBlockIndex(deferredProgram->programId, "HammersleyBlock");
    glUniformBlockBinding(deferredProgram->programId, loc, bindpoint);
#pragma endregion

#pragma region objects and textures
    // create all the Polygon shapes
    proceduralground = new ProceduralGround(grndSize, 400,
                                     grndOctaves, grndFreq, grndPersistence,
                                     grndLow, grndHigh);
    
    Shape* TeapotPolygons =  new Teapot(fullPolyCount?12:2);
    Shape* BoxPolygons = new Box();
    Shape* SpherePolygons = new Sphere(32);
    Shape* RoomPolygons = new Ply("room.ply");
    Shape* FloorPolygons = new Plane(10.0, 10);
    Shape* QuadPolygons = new Quad();
    Shape* SeaPolygons = new Plane(2000.0, 50);
    Shape* GroundPolygons = proceduralground;

    AttachCurvatureAttribute(TeapotPolygons);
    AttachCurvatureAttribute(BoxPolygons);
    AttachCurvatureAttribute(SpherePolygons);
	AttachCurvatureAttribute(RoomPolygons);
	AttachCurvatureAttribute(FloorPolygons);
	AttachCurvatureAttribute(QuadPolygons);
	AttachCurvatureAttribute(SeaPolygons);
	AttachCurvatureAttribute(GroundPolygons);

    sphere_light = new Sphere(32);

    // Various colors used in the subsequent models
    const glm::vec3 woodColor(87.0/255.0, 51.0/255.0, 35.0/255.0);
    const glm::vec3 brickColor(134.0/255.0, 60.0/255.0, 56.0/255.0);
    const glm::vec3 floorColor(6*16/255.0, 5.5*16/255.0, 3*16/255.0);
    const glm::vec3 brassColor(0.5, 0.5, 0.1);
    const glm::vec3 grassColor(62.0/255.0, 102.0/255.0, 38.0/255.0);
    const glm::vec3 waterColor(0.3, 0.3, 1.0);

    // Ks values in a range appropriate range for BRDF calculations. (Phong needs 10* this.)
    const glm::vec3 noSpec(0.0, 0.0, 0.0);
    const glm::vec3 lowSpec(0.01, 0.01, 0.01);
    const glm::vec3 brightSpec(0.03, 0.03, 0.03);
	const glm::vec3 evenBrighterSpec(0.1, 0.1, 0.1);
    const glm::vec3 brightestSpec(1.0, 1.0, 1.0);
 
    // Creates all the models from which the scene is composed.  Each
    // is created with a polygon shape (possibly NULL), a
    // transformation, and the surface lighting parameters Kd, Ks, and
    // alpha.

    // @@ This is where you could read in all the textures and
    // associate them with the various objects being created in the
    // next dozen lines of code.
	Texture* brickTexture = new Texture("textures/bricks/TCom_Wall_Stone3_2x2_2K_albedo.png");
	Texture* floorTexture = new Texture("textures/tiles/TCom_Pavement_TerracottaAntique_2K_albedo.png");
    Texture* teapotTexture = new Texture("textures/teapot/TCom_Paint_Chipped_2K_albedo.png");
	Texture* rockTexture = new Texture("textures/rock/TCom_Rock_CliffVolcanic_2K_albedo.png");
	Texture* grassTexture = new Texture("textures/grass/Poliigon_GrassPatchyGround_4585_BaseColor.jpg");
	Texture* woodTexture = new Texture("textures/Brazilian_rosewood_pxr128.png");
    Texture* skyTexture = new Texture("skys/kloofendal_48d_partly_cloudy_puresky_4k.jpg");

    Texture* displayTexture1 = new Texture("textures/my-house-01.png");
    Texture* displayTexture2 = new Texture("textures/6670-usage.png");

	Texture* brickNormalMap = new Texture("textures/bricks/TCom_Wall_Stone3_2x2_2K_normal.png");
	Texture* floorNormalMap = new Texture("textures/tiles/TCom_Pavement_TerracottaAntique_2K_normal.png");
	Texture* teapotNormalMap = new Texture("textures/teapot/TCom_Paint_Chipped_2K_normal.png");
	Texture* rockNormalMap = new Texture("textures/rock/TCom_Rock_CliffVolcanic_2K_normal.png");
	Texture* grassNormalMap = new Texture("textures/grass/Poliigon_GrassPatchyGround_4585_Normal.png");
	Texture* woodNormalMap = new Texture("textures/Brazilian_rosewood_pxr128_normal.png");
    Texture* waterNormalMap = new Texture("Textures/ripples_normalmap.png");

    hdr = new HDR("skys/Newport_Loft_Ref.hdr");
    hdrIrr = new HDR("skys/Newport_Loft_Ref.irr.hdr");

    pencilTones3D = CreatePencilTonesTexture(256, 32);
    paperNormalMap = CreatePaperNormalMap(512);
    contourPencilTex = CreateContourPencilTex(256, 256);
    CHECKERROR;

    // @@ To change an object's surface parameters (Kd, Ks, or alpha),
    // modify the following lines.
    
    central    = new Object(NULL, nullId);
    anim       = new Object(NULL, nullId);
    room       = new Object(RoomPolygons, roomId, brickColor, noSpec, 1, brickTexture, brickNormalMap);
    floor      = new Object(FloorPolygons, floorId, floorColor, noSpec, 1, floorTexture, floorNormalMap);
    teapot     = new Object(TeapotPolygons, teapotId, brassColor, brightSpec, 120, teapotTexture, teapotNormalMap);
    teapot2    = new Object(TeapotPolygons, teapotId, brassColor, brightestSpec, 120, teapotTexture, teapotNormalMap);
	teapot3    = new Object(TeapotPolygons, teapotId, brassColor, noSpec, 120, teapotTexture, teapotNormalMap);
    podium     = new Object(BoxPolygons, boxId, glm::vec3(woodColor), lowSpec, 10, rockTexture, rockNormalMap);
    sky        = new Object(SpherePolygons, skyId, noSpec, noSpec, 0, skyTexture);
    objectRoot->add(sky, Scale(2000.0, 2000.0, 2000.0));
    ground     = new Object(GroundPolygons, groundId, grassColor, noSpec, 1, grassTexture, grassNormalMap);
    sea = new Object(SeaPolygons, seaId, waterColor, brightSpec, 120, nullptr, waterNormalMap);
    leftFrame  = FramedPicture(Identity, lPicId, BoxPolygons, QuadPolygons, woodTexture, woodNormalMap, displayTexture1);
    rightFrame = FramedPicture(Identity, rPicId, BoxPolygons, QuadPolygons, woodTexture, woodNormalMap);
    spheres    = SphereOfSpheres(SpherePolygons);
#ifdef REFL
    spheres->drawMe = true;
#else
    spheres->drawMe = true;
    room->drawMe = true;
	ground->drawMe = false;
	sea->drawMe = false;
#endif


    // @@ To change the scene hierarchy, examine the hierarchy created
    // by the following object->add() calls and adjust as you wish.
    // The objects being manipulated and their polygon shapes are
    // created above here.

    // Scene is composed of sky, ground, sea, room and some central models
    if (fullPolyCount) {
        objectRoot->add(sky, Scale(2000.0, 2000.0, 2000.0));
        objectRoot->add(sea); 
        objectRoot->add(ground); }
    objectRoot->add(central);
#ifndef REFL
    objectRoot->add(room,  Translate(0.0, 0.0, 0.02));
#endif
    objectRoot->add(floor, Translate(0.0, 0.0, 0.02));

    // Central model has a rudimentary animation (constant rotation on Z)
    animated.push_back(anim);

    // Central contains a teapot on a podium and an external sphere of spheres
    central->add(podium, Translate(0.0, 0,0));
    central->add(anim, Translate(0.0, 0,0));
    // 3 teapots
    anim->add(teapot, Translate(0,0,1)*Scale(0.31,0.31,0.31));
    anim->add(teapot2, Translate(-1.5,0,1)*Scale(0.31,0.31,0.31));
    anim->add(teapot3, Translate(1.5,0,1)*Scale(0.31,0.31,0.31));

    if (fullPolyCount)
        anim->add(spheres, Translate(0.0, 0.0, 0.0)*Scale(16, 16, 16));
    
    // Room contains two framed pictures
    if (fullPolyCount) {
        room->add(leftFrame, Translate(-1.5, 9.85, 1.)*Scale(0.8, 0.8, 0.8));
        room->add(rightFrame, Translate( 1.5, 9.85, 1.)*Scale(0.8, 0.8, 0.8)); }

    CHECKERROR;
#pragma endregion

    // Options menu stuff
    show_demo_window = true;



    printf("shadowFBO.textureID[0] = %u\n", shadowFBO.textureID[0]);
    printf("shadowBlurFBO.textureID[0] = %u\n", shadowBlurFBO.textureID[0]);
    printf("gbufferFBO.textureID[0..3] = %u %u %u %u\n",
        gbufferFBO.textureID[0], gbufferFBO.textureID[1],
        gbufferFBO.textureID[2], gbufferFBO.textureID[3]);
    printf("aoFBO.textureID[0] = %u\n", aoFBO.textureID[0]);
    printf("aoBlurFBO.textureID[0] = %u\n", aoBlurFBO.textureID[0]);
    printf("shadowFBO.fboID = %u, aoFBO.fboID = %u\n",
        shadowFBO.fboID, aoFBO.fboID);
}

void Scene::DrawMenu()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // UI toggles
    static bool showStatsWindow = false;
    static bool showCameraWindow = false;
    static bool showLightingWindow = false;
    static bool showMaterialWindow = false;
    static bool showShadowWindow = false;
    static bool showAOWindow = false;
    static bool showContourWindow = false;
    static bool wireframe = false;
    static bool vsync = true;
    static bool MSAA = true;
    static bool showDemoWindow = false;

    if (ImGui::BeginMainMenuBar()) {

        // scene visibility
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Draw spheres", "", spheres->drawMe)) { spheres->drawMe ^= true; }
            if (ImGui::MenuItem("Draw walls", "", room->drawMe)) { room->drawMe ^= true; }
            if (ImGui::MenuItem("Draw ground/sea", "", ground->drawMe)) {
                ground->drawMe ^= true;
                sea->drawMe = ground->drawMe;
            }
			if (ImGui::MenuItem("Draw floor", "", floor->drawMe)) { floor->drawMe ^= true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Pencil mode", "", (bool)pencilMode)) { pencilMode ^= 1; }
            ImGui::Separator();
            if (ImGui::MenuItem("Only local lights", "", onlyLocalLights)) { onlyLocalLights ^= true; }
            if (ImGui::MenuItem("Disable direct light", "", noDirectLight)) { noDirectLight ^= true; }
            if (ImGui::MenuItem("Disable local lights", "", noLocalLight)) { noLocalLight ^= true; }
            ImGui::EndMenu();
        }

        // camera
        if (ImGui::BeginMenu("View")) {
            bool isFirstPerson = transformation_mode;
            if (ImGui::MenuItem("First-person", "", isFirstPerson)) { transformation_mode = true; }
            if (ImGui::MenuItem("Orbit (world)", "", !isFirstPerson)) { transformation_mode = false; }
            if (ImGui::MenuItem("Reset camera")) {
                spin = 0.0f;  tilt = 30.0f;
                tx = 0.0f;    ty = 0.0f;
                zoom = 25.0f; ry = 0.4f;
                front = 0.5f; back = 5000.0f;
                eye = glm::vec3(0.0f, -20.0f, 0.0f);
                speed = 10.0f;
            }
            ImGui::EndMenu();
        }

        // render mode
        if (ImGui::BeginMenu("Render Mode")) {
            if (ImGui::MenuItem("Default", "", mode == 0)) { mode = 0; }
            ImGui::Separator();
            ImGui::TextDisabled("G-Buffer");
            if (ImGui::MenuItem("  WorldPos", "", mode == 1)) { mode = 1; }
            if (ImGui::MenuItem("  Normal", "", mode == 2)) { mode = 2; }
            if (ImGui::MenuItem("  Kd", "", mode == 3)) { mode = 3; }
            if (ImGui::MenuItem("  Ks / Alpha", "", mode == 4)) { mode = 4; }
            ImGui::Separator();
            ImGui::TextDisabled("Shadow Map");
            if (ImGui::MenuItem("  Shadow Map", "", mode == 5)) { mode = 5; }
            ImGui::Separator();
            ImGui::TextDisabled("Ambient Occlusion");
            if (ImGui::MenuItem("  AO (after blur)", "", mode == 6)) { mode = 6; }
            ImGui::Separator();
            ImGui::TextDisabled("Pencil");
            if (ImGui::MenuItem("  Contour edges", "", mode == 7)) { mode = 7; pencilMode = 1; }
            if (ImGui::MenuItem("  Interior shading", "", mode == 8)) { mode = 8; pencilMode = 1; }
            if (ImGui::MenuItem("  Combined full pencil", "", mode == 9)) { mode = 9; pencilMode = 1; }
            ImGui::EndMenu();
        }

        // windows
        if (ImGui::BeginMenu("Windows")) {
            if (ImGui::MenuItem("Lighting", "", showLightingWindow)) { showLightingWindow ^= true; }
            if (ImGui::MenuItem("Material", "", showMaterialWindow)) { showMaterialWindow ^= true; }
            if (ImGui::MenuItem("Shadows", "", showShadowWindow)) { showShadowWindow ^= true; }
            if (ImGui::MenuItem("Ambient Occlusion", "", showAOWindow)) { showAOWindow ^= true; }
            if (ImGui::MenuItem("Camera", "", showCameraWindow)) { showCameraWindow ^= true; }
            if (ImGui::MenuItem("Pencil", "", showContourWindow)) { showContourWindow ^= true; }
            if (ImGui::MenuItem("Stats", "", showStatsWindow)) { showStatsWindow ^= true; }
            ImGui::Separator();
            if (ImGui::MenuItem("ImGui Demo", "", showDemoWindow)) { showDemoWindow ^= true; }
            ImGui::EndMenu();
        }

        // gl settings
        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("VSync", "", vsync)) {
                vsync ^= true;
                glfwSwapInterval(vsync ? 1 : 0);
            }
            if (ImGui::MenuItem("MSAA", "", MSAA)) {
                MSAA ^= true;
                if (MSAA) glEnable(GL_MULTISAMPLE);
                else      glDisable(GL_MULTISAMPLE);
            }
            if (ImGui::MenuItem("Draw light spheres", "", localLightDebug)) {
                localLightDebug ^= true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    // lighting window
    if (showLightingWindow) {
        ImGui::Begin("Lighting", &showLightingWindow);
        ImGui::SliderFloat("Distance", &lightDist, 1.0f, 1000.0f);
        ImGui::SliderFloat("Spin (deg)", &lightSpin, -180.0f, 180.0f);
        ImGui::SliderFloat("Tilt (deg)", &lightTilt, -89.9f, 89.9f);
        ImGui::Separator();
        ImGui::Text("Tone Mapping");
        ImGui::SliderFloat("Exposure", &exposure, 0.01f, 20.0f);
        ImGui::End();
        z0 = lightDist - 60.0f;
        z1 = lightDist + 60.0f;
    }

    // material window
    if (showMaterialWindow) {
        ImGui::Begin("Material", &showMaterialWindow);
        ImGui::Text("Teapots");
        ImGui::SliderFloat("Alpha (shininess)", &teapotAlpha, 1.0f, 500.0f);
        teapot->shininess = teapotAlpha;
        teapot2->shininess = teapotAlpha;
        teapot3->shininess = teapotAlpha;
        ImGui::End();
    }

    // shaodw window
    if (showShadowWindow) {
        ImGui::Begin("Shadows", &showShadowWindow);
        ImGui::SliderInt("Blur radius", &blurRadius, 1, 100);
        ImGui::MenuItem("Show blurred", "", &shadowShowBlurred);
        ImGui::End();
    }

        // ambient occlusion window
    if (showAOWindow) {
        ImGui::Begin("Ambient Occlusion", &showAOWindow);
        ImGui::Text("Sampling");
        ImGui::SliderFloat("Radius (R)", &aoRadius, 0.01f, 20.0f, "%.2f");
        ImGui::SliderInt("Samples (n)", &aoSamples, 1, 64);
        ImGui::SliderFloat("Scale (s)", &aoScale, 0.0f, 10.0f, "%.2f");
        ImGui::SliderFloat("Contrast (k)", &aoContrast, 0.1f, 10.0f, "%.2f");
        ImGui::SliderFloat("Depth bias (delta)", &aoDelta, 0.0f, 0.1f, "%.4f");
        ImGui::Separator();
        ImGui::Text("Bilateral Blur");
        ImGui::SliderInt("Blur radius", &aoBlurRadius, 0, 20);
        ImGui::SliderFloat("Sigma (range)", &aoSigma, 0.001f, 1.0f, "%.4f");
        ImGui::Separator();
        if (ImGui::Button("View AO buffer")) { mode = 8; }
        ImGui::SameLine();
        if (mode == 8 && ImGui::Button("Back to default")) { mode = 0; }
        ImGui::End();
    }

    // contour window
    if (showContourWindow) {
        ImGui::Begin("Contour", &showContourWindow);
        ImGui::Text("Contour");
        ImGui::SliderFloat("Normal threshold", &contourNormalThreshold, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Depth threshold", &contourDepthThreshold, 0.01f, 5.0f, "%.2f");
        ImGui::SliderFloat("Shake amplitude", &contourShakeAmp, 0.0f, 0.02f, "%.4f");
        ImGui::SliderFloat("Shake frequency", &contourShakeFreq, 1.0f, 100.0f, "%.1f");
        ImGui::SliderInt("Num shakes", &contourNumShakes, 1, 10);
        ImGui::SliderFloat("Pencil tile", &contourPencilTile, 0.5f, 20.0f, "%.1f");
        ImGui::Separator();
        ImGui::Text("Interior Shading");
        ImGui::SliderFloat("Interior pencil tile", &interiorPencilTile, 0.5f, 20.0f, "%.1f");
        ImGui::SliderFloat("Paper strength", &interiorPaperStrength, 0.0f, 0.2f, "%.4f");
        ImGui::SliderFloat("Paper tile", &interiorPaperTile, 0.5f, 10.0f, "%.1f");
        ImGui::SliderFloat("Cross-hatch below", &interiorCrossHatchBelow, 0.0f, 1.0f, "%.2f");
        ImGui::Separator();
        ImGui::Text("Composition");
        ImGui::SliderFloat("Contrast", &pencilContrast, 0.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3("Paper color", (float*)&pencilPaperColor);
        ImGui::Separator();
        if (ImGui::Button("View contour buffer")) { mode = 7; pencilMode = 1; }
        ImGui::SameLine();
        if (ImGui::Button("View interior buffer")) { mode = 8; pencilMode = 1; }
        ImGui::SameLine();
        if (ImGui::Button("View Combined")) { mode = 9; pencilMode = 1; }
        ImGui::SameLine();
        if ((mode == 7 || mode == 8 || mode == 9) && ImGui::Button("Back to default")) { mode = 0; pencilMode = 0; }
        ImGui::End();
    }

    // camera window
    if (showCameraWindow) {
        ImGui::Begin("Camera", &showCameraWindow);
        ImGui::Text("Mode: %s", transformation_mode ? "First-person" : "Orbit (world)");
        ImGui::SliderFloat("Spin (deg)", &spin, -180.0f, 180.0f);
        ImGui::SliderFloat("Tilt (deg)", &tilt, -89.9f, 89.9f);
        if (!transformation_mode) {
            ImGui::SliderFloat("Zoom", &zoom, 1.0f, 100.0f);
            ImGui::DragFloat("Pan X (tx)", &tx, 0.1f, -100.0f, 100.0f);
            ImGui::DragFloat("Pan Y (ty)", &ty, 0.1f, -100.0f, 100.0f);
        }
        ImGui::DragFloat("Move speed", &speed, 0.1f, 0.1f, 100.0f);
        ImGui::DragFloat("rx", &rx, 0.1f, 0.1f, 10.0f);
        ImGui::DragFloat("ry", &ry, 0.1f, 0.1f, 10.0f);
        ImGui::Separator();
        ImGui::Text("Eye: (%.2f, %.2f, %.2f)", eye.x, eye.y, eye.z);
        ImGui::Text("rx: %.3f  ry: %.3f  front: %.2f  back: %.1f", rx, ry, front, back);
        ImGui::End();
    }

    // stats window
    if (showStatsWindow) {
        ImGui::Begin("Stats", &showStatsWindow);
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, (io.Framerate > 0.0f) ? (1000.0f / io.Framerate) : 0.0f);
        ImGui::Text("Viewport: %d x %d", width, height);
        ImGui::Text("Time: %.3f  dt: %.3f  step: %.3f", currentTime, timeSinceLastRefresh, step);
        ImGui::Text("LightPos: (%.2f, %.2f, %.2f)", lightPos.x, lightPos.y, lightPos.z);
        ImGui::Text("Local light count: %d", localLightCount);
        ImGui::End();
    }

    // demo
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Scene::BuildTransforms()
{
    

    // @@ When you are ready to try interactive viewing, replace the
    // following hard coded values for WorldProj and WorldView with
    // transformation matrices calculated from variables such as spin,
    // tilt, tr, ry, front, and back.

    // first person
    if (transformation_mode) {
        WorldProj = Perspective(rx, ry, front, back);
		WorldView = Rotate(0, tilt - 90) * Rotate(2, spin) * Translate(-eye.x, -eye.y, -eye.z);
    }
    // world
    else {
        WorldProj = Perspective(rx, ry, front, back);
        WorldView = Translate(tx, ty, -zoom) * Rotate(0, tilt - 90) * Rotate(2, spin);
    }

    // @@ Print the two matrices (in column-major order) for
    // comparison with the project document.
    //std::cout << "WorldView: " << glm::to_string(WorldView) << std::endl;
    //std::cout << "WorldProj: " << glm::to_string(WorldProj) << std::endl;
}

////////////////////////////////////////////////////////////////////////
// Procedure DrawScene is called whenever the scene needs to be
// drawn. (Which is often: 30 to 60 times per second are the common
// goals.)
void Scene::DrawScene()
{
#pragma region Setup
    // Set the viewport
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    // Ensure baseline GL state every frame (prevents debug passes from breaking later passes)
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    CHECKERROR;
    // Calculate the light's position from lightSpin, lightTilt, lightDist
    lightPos = glm::vec3(lightDist*cos(lightSpin*rad)*sin(lightTilt*rad),
                         lightDist*sin(lightSpin*rad)*sin(lightTilt*rad), 
                         lightDist*cos(lightTilt*rad));

    // Update position of any continuously animating objects
    double atime = 360.0*glfwGetTime()/36;
    for (Object* obj : animated)
    {
        if (obj != nullptr)
            obj->animTr = Rotate(2, atime);
    }

    // static frame buffer aspect ratio
	rx = ry * (float)width / (float)height;
	// keep eye at constant height above ground
	eye.z = proceduralground->HeightAt(eye.x, eye.y) + 1.5;

    // get step size
    currentTime = glfwGetTime();
    timeSinceLastRefresh = currentTime - lastRefreshTime;
    lastRefreshTime = currentTime;
    step = speed * timeSinceLastRefresh;
    glm::vec3 moveDir(0.0);
    float spin_rad = spin * PI / 180;
    if (w_key) {
        moveDir += glm::vec3(sin(spin_rad), cos(spin_rad), 0.0);
    }
    if (s_key) {
        moveDir -= glm::vec3(sin(spin_rad), cos(spin_rad), 0.0);
    }
    if (d_key) {
        moveDir += glm::vec3(cos(spin_rad), -sin(spin_rad), 0.0);
    }
    if (a_key) {
        moveDir -= glm::vec3(cos(spin_rad), -sin(spin_rad), 0.0);
    }
    if (glm::length(moveDir) > 0.0) {
        moveDir = glm::normalize(moveDir);
        eye += step * moveDir;
    }
    //std::cout << "transfomation mode: " << transformation_mode << std::endl;
    BuildTransforms();

    // The lighting algorithm needs the inverse of the WorldView matrix
    WorldInverse = glm::inverse(WorldView);
#pragma endregion

    ////////////////////////////////////////////////////////////////////////////////
    // Anatomy of a pass:
    //   Choose a shader  (create the shader in InitializeScene above)
    //   Choose and FBO/Render-Target (if needed; create the FBO in InitializeScene above)
    //   Set the viewport (to the pixel size of the screen or FBO)
    //   Clear the screen.
    //   Set the uniform variables required by the shader
    //   Draw the geometry
    //   Unset the FBO (if one was used)
    //   Unset the shader
    ////////////////////////////////////////////////////////////////////////////////

    CHECKERROR;
    int loc, programId;
    ////////////////////////////////////////////////////////////////////////////////
    // Shadow pass
    ////////////////////////////////////////////////////////////////////////////////
#pragma region ShadowPass
    LightView = LookAt(lightPos, glm::vec3(0.0, 0.0, 0.0), glm::vec3(0.0, 0.0, 1.0));
	LightProj = Perspective(40.0/lightDist, 40.0f/lightDist, front, back);

    // shadow matrix
    const glm::mat4 B = Translate(0.5f, 0.5f, 0.5f) * Scale(0.5f, 0.5f, 0.5f);
    ShadowMatrix = B * LightProj * LightView;

	shadowProgram->UseShader();
	shadowFBO.BindFBO();

    glViewport(0, 0, shadowFBOres, shadowFBOres);
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

	loc = glGetUniformLocation(shadowProgram->programId, "LightProj");
	glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(LightProj));
	loc = glGetUniformLocation(shadowProgram->programId, "LightView");
	glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(LightView));

    loc = glGetUniformLocation(shadowProgram->programId, "z0");
    glUniform1f(loc, z0);
    loc = glGetUniformLocation(shadowProgram->programId, "z1");
    glUniform1f(loc, z1);

    CHECKERROR;
	objectRoot->Draw(shadowProgram, Identity);
    CHECKERROR;

    glDisable(GL_CULL_FACE);

	shadowFBO.UnbindFBO();
    shadowProgram->UnuseShader();
#pragma endregion
    ////////////////////////////////////////////////////////////////////////////////
    // Blur pass  (horizontal then vertical)
    ////////////////////////////////////////////////////////////////////////////////
#pragma region BlurPass
    // before blur debug view
    if (mode == 5 && !shadowShowBlurred) {
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        shadowDebugProgram->UseShader();
        int pid = shadowDebugProgram->programId;
        shadowFBO.BindTexture(0, 0, pid, "shadowMap");

        glBindVertexArray(gbufferDebugVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        shadowFBO.UnbindTexture(0);
        shadowDebugProgram->UnuseShader();
        return;
    }

    // blur
    const int res = static_cast<int>(shadowFBOres);
    // horizontal: shadowFBO -> shadowBlurFBO
    blurHProgram->UseShader();
    glBindImageTexture(0, shadowFBO.textureID[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, shadowBlurFBO.textureID[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glUniform1i(glGetUniformLocation(blurHProgram->programId, "blurRadius"), blurRadius);
    glDispatchCompute((res + 127) / 128, res, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    blurHProgram->UnuseShader();
    // vertical: shadowBlurFBO -> shadowFBO
    blurVProgram->UseShader();
    glBindImageTexture(0, shadowBlurFBO.textureID[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, shadowFBO.textureID[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glUniform1i(glGetUniformLocation(blurVProgram->programId, "blurRadius"), blurRadius);
    glDispatchCompute(res, (res + 127) / 128, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    blurVProgram->UnuseShader();

	// after blur debug view
    if (mode == 5 && shadowShowBlurred) {
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        shadowDebugProgram->UseShader();
        int pid = shadowDebugProgram->programId;
        shadowFBO.BindTexture(0, 0, pid, "shadowMap");

        glBindVertexArray(gbufferDebugVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        shadowFBO.UnbindTexture(0);
        shadowDebugProgram->UnuseShader();
        return;
    }
#pragma endregion
    ////////////////////////////////////////////////////////////////////////////////
    // G-buffer pass
    ////////////////////////////////////////////////////////////////////////////////
#pragma region GbufferPass
    gbufferProgram->UseShader();
    programId = gbufferProgram->programId;

    if (gbufferFBO.width != width || gbufferFBO.height != height) {
        gbufferFBO.DestroyFBO();
        gbufferFBO.CreateGbufferFBO(width, height);
    }

    gbufferFBO.BindFBO();
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // scene uniforms
    loc = glGetUniformLocation(programId, "WorldProj");
    glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(WorldProj));
    loc = glGetUniformLocation(programId, "WorldView");
    glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(WorldView));
    loc = glGetUniformLocation(programId, "WorldInverse");
    glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(WorldInverse));
    loc = glGetUniformLocation(programId, "time");
    glUniform1f(loc, currentTime);

    // draw scene geometry into render targeets
    objectRoot->Draw(gbufferProgram, Identity);

    gbufferFBO.UnbindFBO();
    gbufferProgram->UnuseShader();

	// debug gbuffer textures
    if (mode >= 1 && mode <= 4)
    {
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        gbufferDebugProgram->UseShader();
        programId = gbufferDebugProgram->programId;

        // bind gbuffer textures
        gbufferFBO.BindTexture(0, 0, programId, "gWorldPos");
        gbufferFBO.BindTexture(1, 1, programId, "gNormal");
        gbufferFBO.BindTexture(2, 2, programId, "gKd");
        gbufferFBO.BindTexture(3, 3, programId, "gKsAlpha");

        glUniform1i(glGetUniformLocation(programId, "uMode"), mode - 1);
        glBindVertexArray(gbufferDebugVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        // unbind
        gbufferFBO.UnbindTexture(3);
        gbufferFBO.UnbindTexture(2);
        gbufferFBO.UnbindTexture(1);
        gbufferFBO.UnbindTexture(0);

        gbufferDebugProgram->UnuseShader();
        return;
    }
#pragma endregion
    ////////////////////////////////////////////////////////////////////////////////
    // Ambient Occlusion pass
    ////////////////////////////////////////////////////////////////////////////////
#pragma region AoPass
    
    // Resize if window changed
    if (aoFBO.width != width || aoFBO.height != height) {
        aoFBO.DestroyFBO();      aoFBO.CreateAoFBO(width, height);
        aoBlurFBO.DestroyFBO();  aoBlurFBO.CreateAoFBO(width, height);
    }

    aoFBO.BindFBO();
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT);

    aoProgram->UseShader();
    int pid = aoProgram->programId;

    gbufferFBO.BindTexture(0, 0, pid, "gPosition");  // world pos
    gbufferFBO.BindTexture(1, 1, pid, "gNormal");    // world normal

    glUniformMatrix4fv(glGetUniformLocation(pid, "WorldView"),
        1, GL_FALSE, glm::value_ptr(WorldView));
    glUniform2f(glGetUniformLocation(pid, "screenSize"),
        (float)width, (float)height);
    glUniform1f(glGetUniformLocation(pid, "R"), aoRadius);
    glUniform1i(glGetUniformLocation(pid, "n"), aoSamples);
    glUniform1f(glGetUniformLocation(pid, "s"), aoScale);
    glUniform1f(glGetUniformLocation(pid, "k"), aoContrast);
    glUniform1f(glGetUniformLocation(pid, "delta"), aoDelta);
    float projScale = (float)height * 0.5f * WorldProj[1][1];
    glUniform1f(glGetUniformLocation(pid, "projScale"), projScale);


    glBindVertexArray(aoVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    gbufferFBO.UnbindTexture(1);
    gbufferFBO.UnbindTexture(0);
    aoProgram->UnuseShader();
    aoFBO.UnbindFBO();

    // bilateral blur
    // Horizontal: aoFBO -> aoBlurFBO
    aoBlurHProgram->UseShader();
    glBindImageTexture(0, aoFBO.textureID[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
    glBindImageTexture(1, aoBlurFBO.textureID[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
    gbufferFBO.BindTexture(0, 2, aoBlurHProgram->programId, "gPosition");
    gbufferFBO.BindTexture(1, 3, aoBlurHProgram->programId, "gNormal");
    glUniformMatrix4fv(glGetUniformLocation(aoBlurHProgram->programId, "WorldView"), 1, GL_FALSE, glm::value_ptr(WorldView));
    glUniform1i(glGetUniformLocation(aoBlurHProgram->programId, "blurRadius"), aoBlurRadius);
    glUniform1f(glGetUniformLocation(aoBlurHProgram->programId, "sigma"), aoSigma);
    glDispatchCompute((width + 127) / 128, height, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    aoBlurHProgram->UnuseShader();
    // Vertical: aoBlurFBO -> aoFBO
    aoBlurVProgram->UseShader();
    glBindImageTexture(0, aoBlurFBO.textureID[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
    glBindImageTexture(1, aoFBO.textureID[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
    gbufferFBO.BindTexture(0, 2, aoBlurVProgram->programId, "gPosition");
    gbufferFBO.BindTexture(1, 3, aoBlurVProgram->programId, "gNormal");
    glUniformMatrix4fv(glGetUniformLocation(aoBlurVProgram->programId, "WorldView"),
        1, GL_FALSE, glm::value_ptr(WorldView));
    glUniform1i(glGetUniformLocation(aoBlurVProgram->programId, "blurRadius"), aoBlurRadius);
    glUniform1f(glGetUniformLocation(aoBlurVProgram->programId, "sigma"), aoSigma);
    glDispatchCompute(width, (height + 127) / 128, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    aoBlurVProgram->UnuseShader();

    if (mode == 6) {
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        aoDebugProgram->UseShader();
        int pid = aoDebugProgram->programId;
        aoFBO.BindTexture(0, 0, pid, "aoMap");

        glBindVertexArray(gbufferDebugVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        aoFBO.UnbindTexture(0);
        aoDebugProgram->UnuseShader();
        return;
    }
    
#pragma endregion
    ////////////////////////////////////////////////////////////////////////////////
    // Contour pass
    ////////////////////////////////////////////////////////////////////////////////
#pragma region ContourPass
    if(pencilMode){
        if (contourFBO.width != width || contourFBO.height != height) {
            contourFBO.DestroyFBO();
            contourFBO.CreateAoFBO(width, height);
        }

        contourFBO.BindFBO();
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glClear(GL_COLOR_BUFFER_BIT);

        contourProgram->UseShader();
        {
            int pid = contourProgram->programId;

            gbufferFBO.BindTexture(0, 0, pid, "gWorldPos");
            gbufferFBO.BindTexture(1, 1, pid, "gNormal");

            glActiveTexture((gl::GLenum)((int)GL_TEXTURE0 + 2));
            glBindTexture(GL_TEXTURE_2D, contourPencilTex);
            glUniform1i(glGetUniformLocation(pid, "contourPencilTex"), 2);

            glUniform3fv(glGetUniformLocation(pid, "lightPos"), 1, (float*)&lightPos);
            glUniform2f(glGetUniformLocation(pid, "screenSize"), (float)width, (float)height);

            glUniform1f(glGetUniformLocation(pid, "normalThreshold"), contourNormalThreshold);
            glUniform1f(glGetUniformLocation(pid, "depthThreshold"), contourDepthThreshold);
            glUniform1f(glGetUniformLocation(pid, "shakeAmp"), contourShakeAmp);
            glUniform1f(glGetUniformLocation(pid, "shakeFreq"), contourShakeFreq);
            glUniform1i(glGetUniformLocation(pid, "numShakes"), contourNumShakes);
            glUniform1f(glGetUniformLocation(pid, "pencilTile"), contourPencilTile);

            glBindVertexArray(deferredVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);

            glActiveTexture((gl::GLenum)((int)GL_TEXTURE0 + 2));
            glBindTexture(GL_TEXTURE_2D, 0);
            gbufferFBO.UnbindTexture(1);
            gbufferFBO.UnbindTexture(0);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }
        contourProgram->UnuseShader();
        contourFBO.UnbindFBO();

        if (mode == 7) {
            glViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);

            contourDebugProgram->UseShader();
            {
                int pid = contourDebugProgram->programId;
                contourFBO.BindTexture(0, 0, pid, "contourMap");
                glBindVertexArray(deferredVAO);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
                contourFBO.UnbindTexture(0);
            }
            contourDebugProgram->UnuseShader();
            return;
        }
    }
#pragma endregion
    ////////////////////////////////////////////////////////////////////////////////
    // Interior shading pass
    ////////////////////////////////////////////////////////////////////////////////
#pragma region InteriorPass
    if(pencilMode){
        if (interiorFBO.width != width || interiorFBO.height != height) {
            interiorFBO.DestroyFBO();
            interiorFBO.CreateAoFBO(width, height);
        }

        interiorFBO.BindFBO();
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glClear(GL_COLOR_BUFFER_BIT);

        interiorProgram->UseShader();
        {
            int pid = interiorProgram->programId;

            gbufferFBO.BindTexture(0, 0, pid, "gWorldPos");
            gbufferFBO.BindTexture(1, 1, pid, "gNormal");
            gbufferFBO.BindTexture(2, 2, pid, "gKd");
            aoFBO.BindTexture(0, 3, pid, "aoMap");

            // Pencil tones 3D texture
            glActiveTexture((gl::GLenum)((int)GL_TEXTURE0 + 4));
            glBindTexture(GL_TEXTURE_3D, pencilTones3D);
            glUniform1i(glGetUniformLocation(pid, "pencilTones3D"), 4);

            // Paper normal map
            glActiveTexture((gl::GLenum)((int)GL_TEXTURE0 + 5));
            glBindTexture(GL_TEXTURE_2D, paperNormalMap);
            glUniform1i(glGetUniformLocation(pid, "paperNormalMap"), 5);

            glUniform3fv(glGetUniformLocation(pid, "Light"), 1, (float*)&Light);
            glUniform3fv(glGetUniformLocation(pid, "Ambient"), 1, (float*)&Ambient);
            glUniform3fv(glGetUniformLocation(pid, "lightPos"), 1, (float*)&lightPos);

            glm::vec3 eyePosWS = glm::vec3(WorldInverse * glm::vec4(0, 0, 0, 1));
            glUniform3fv(glGetUniformLocation(pid, "eyePos"), 1, (float*)&eyePosWS);

            glUniform1f(glGetUniformLocation(pid, "pencilTile"), interiorPencilTile);
            glUniform1f(glGetUniformLocation(pid, "paperStrength"), interiorPaperStrength);
            glUniform1f(glGetUniformLocation(pid, "paperTile"), interiorPaperTile);
            glUniform1f(glGetUniformLocation(pid, "crossHatchBelow"), interiorCrossHatchBelow);

            glBindVertexArray(deferredVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);

            glActiveTexture((gl::GLenum)((int)GL_TEXTURE0 + 5));
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture((gl::GLenum)((int)GL_TEXTURE0 + 4));
            glBindTexture(GL_TEXTURE_3D, 0);
            aoFBO.UnbindTexture(3);
            gbufferFBO.UnbindTexture(2);
            gbufferFBO.UnbindTexture(1);
            gbufferFBO.UnbindTexture(0);
        }
        interiorProgram->UnuseShader();
        interiorFBO.UnbindFBO();

        if (mode == 8) {
            glViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);

            interiorDebugProgram->UseShader();
            {
                int pid = interiorDebugProgram->programId;
                interiorFBO.BindTexture(0, 0, pid, "interiorMap");
                glBindVertexArray(deferredVAO);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
                interiorFBO.UnbindTexture(0);
            }
            interiorDebugProgram->UnuseShader();
            return;
        }
    }
#pragma endregion
    ////////////////////////////////////////////////////////////////////////////////
    // Deferred shading pass
    ////////////////////////////////////////////////////////////////////////////////
    {
#pragma region DeferredPass
        // When pencil mode is on, render into an FBO so the composition
        // pass can sample the result (needed for sky regions).  When off,
        // render directly to the screen as usual
        if (pencilMode) {
            if (deferredOutputFBO.width != width || deferredOutputFBO.height != height) {
                deferredOutputFBO.DestroyFBO();
                deferredOutputFBO.CreateFBO(width, height);
            }
            deferredOutputFBO.BindFBO();
        }

        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glClear(GL_COLOR_BUFFER_BIT);

        deferredProgram->UseShader();
        programId = deferredProgram->programId;

        // Bind G-buffer textures and shadow map
        gbufferFBO.BindTexture(0, 0, programId, "gWorldPos");
        gbufferFBO.BindTexture(1, 1, programId, "gNormal");
        gbufferFBO.BindTexture(2, 2, programId, "gKd");
        gbufferFBO.BindTexture(3, 3, programId, "gKsAlpha");
        shadowFBO.BindTexture(0, 4, programId, "shadowMap");

        // bind HDR
        hdr->BindTexture(5, programId, "environmentMap");
        hdrIrr->BindTexture(6, programId, "irradianceMap");

        // bind AO map
        aoFBO.BindTexture(0, 7, programId, "aoMap");

        // turn off lights if only local lights mode is on 
        const glm::vec3 nothing(0.0f, 0.0f, 0.0f);
        if (onlyLocalLights) {
            glUniform3fv(glGetUniformLocation(programId, "Light"), 1, (float*)&nothing);
            glUniform3fv(glGetUniformLocation(programId, "Ambient"), 1, (float*)&nothing);
        }
        else {
            glUniform3fv(glGetUniformLocation(programId, "Light"), 1, (float*)&Light);
            glUniform3fv(glGetUniformLocation(programId, "Ambient"), 1, (float*)&Ambient);
        }
        // light position for specular and shadows
        glUniform3fv(glGetUniformLocation(programId, "lightPos"), 1, (float*)&lightPos);
        glm::vec3 eyePosWS = glm::vec3(WorldInverse * glm::vec4(0, 0, 0, 1));
        glUniform3fv(glGetUniformLocation(programId, "eyePos"), 1, (float*)&eyePosWS);
        glUniformMatrix4fv(glGetUniformLocation(programId, "ShadowMatrix"), 1, GL_FALSE, glm::value_ptr(ShadowMatrix));

        // z0 and z1 for reconstructing world position from depth in the shader
        loc = glGetUniformLocation(deferredProgram->programId, "z0");
        glUniform1f(loc, z0);
        loc = glGetUniformLocation(deferredProgram->programId, "z1");
        glUniform1f(loc, z1);

        glUniform1i(glGetUniformLocation(programId, "noDirectLight"), noDirectLight);
        loc = glGetUniformLocation(deferredProgram->programId, "exposure");
        glUniform1f(loc, exposure);

        // draw full-screen triangle to apply lighting to all pixels
        glBindVertexArray(deferredVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        hdr->UnbindTexture(5);
        hdrIrr->UnbindTexture(6);
        shadowFBO.UnbindTexture(4);
        gbufferFBO.UnbindTexture(3);
        gbufferFBO.UnbindTexture(2);
        gbufferFBO.UnbindTexture(1);
        gbufferFBO.UnbindTexture(0);
        aoFBO.UnbindTexture(7);

        deferredProgram->UnuseShader();
#pragma endregion
        ////////////////////////////////////////////////////////////////////////////////
        // local lights pass
        ////////////////////////////////////////////////////////////////////////////////
#pragma region LocalLightsPass
        if (!noDirectLight && !noLocalLight) {
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);

            localLightProgram->UseShader();
            programId = localLightProgram->programId;
            glUniformMatrix4fv(glGetUniformLocation(programId, "WorldProj"), 1, GL_FALSE, glm::value_ptr(WorldProj));
            glUniformMatrix4fv(glGetUniformLocation(programId, "WorldView"), 1, GL_FALSE, glm::value_ptr(WorldView));
            glUniform1i(glGetUniformLocation(programId, "debugDrawSpheres"), localLightDebug);
            // bind G-buffer
            gbufferFBO.BindTexture(0, 0, programId, "gWorldPos");
            gbufferFBO.BindTexture(1, 1, programId, "gNormal");
            gbufferFBO.BindTexture(2, 2, programId, "gKd");
            gbufferFBO.BindTexture(3, 3, programId, "gKsAlpha");
            glUniform2f(glGetUniformLocation(programId, "screenSize"),
                (float)width, (float)height);
            glUniform3fv(glGetUniformLocation(programId, "eyePos"), 1, &eyePosWS[0]);

            for (const LocalLight& L : localLights)
            {
                glm::mat4 ModelTr = glm::translate(glm::mat4(1), L.position) * glm::scale(glm::mat4(1), glm::vec3(L.radius));
                glUniformMatrix4fv(glGetUniformLocation(programId, "ModelTr"), 1, GL_FALSE, glm::value_ptr(ModelTr));
                glUniform3fv(glGetUniformLocation(programId, "lightPos"), 1, &L.position[0]);
                glUniform3fv(glGetUniformLocation(programId, "lightColor"), 1, &L.color[0]);
                glUniform1f(glGetUniformLocation(programId, "lightRadius"), L.radius);

                sphere_light->DrawVAO();
            }

            localLightProgram->UnuseShader();

            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glCullFace(GL_BACK);
        }
#pragma endregion
        ////////////////////////////////////////////////////////////////////////////////
		// pencil composition pass
        ////////////////////////////////////////////////////////////////////////////////
#pragma region PencilCompositionPass
        if (pencilMode) {
            deferredOutputFBO.UnbindFBO();

            ////////////////////////////////////////////////////////////////
            // Pencil composition pass — combines contour + interior +
            // paper tooth + deferred sky into the final screen output.
            ////////////////////////////////////////////////////////////////
            glViewport(0, 0, width, height);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glClear(GL_COLOR_BUFFER_BIT);

            pencilCompositeProgram->UseShader();
            {
                int pid = pencilCompositeProgram->programId;

                contourFBO.BindTexture(0, 0, pid, "contourMap");
                interiorFBO.BindTexture(0, 1, pid, "interiorMap");
                gbufferFBO.BindTexture(0, 2, pid, "gWorldPos");
                deferredOutputFBO.BindTexture(0, 3, pid, "deferredMap");

                // Paper normal map (raw GL handle)
                glActiveTexture((gl::GLenum)((int)GL_TEXTURE0 + 4));
                glBindTexture(GL_TEXTURE_2D, paperNormalMap);
                glUniform1i(glGetUniformLocation(pid, "paperNormalMap"), 4);

                glUniform1f(glGetUniformLocation(pid, "paperTile"), interiorPaperTile);
                glUniform3fv(glGetUniformLocation(pid, "paperColor"), 1, (float*)&pencilPaperColor);
                glUniform1f(glGetUniformLocation(pid, "contrastAmount"), pencilContrast);

                glBindVertexArray(deferredVAO);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);

                glActiveTexture((gl::GLenum)((int)GL_TEXTURE0 + 4));
                glBindTexture(GL_TEXTURE_2D, 0);
                deferredOutputFBO.UnbindTexture(3);
                gbufferFBO.UnbindTexture(2);
                interiorFBO.UnbindTexture(1);
                contourFBO.UnbindTexture(0);
            }
            pencilCompositeProgram->UnuseShader();
        }
#pragma endregion
    }
    ////////////////////////////////////////////////////////////////////////////////
    // End of DrawScene
    ////////////////////////////////////////////////////////////////////////////////
}
