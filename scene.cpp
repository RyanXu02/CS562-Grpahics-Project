
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
                                    hue, glm::vec3(1.0, 1.0, 1.0), 120.0);
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

    CHECKERROR;
    objectRoot = new Object(NULL, nullId);

    
    // Enable OpenGL depth-testing
    glEnable(GL_DEPTH_TEST);

    // Create the lighting shader program from source code files.
    // @@ Initialize additional shaders if necessary
    lightingProgram = new ShaderProgram();
    lightingProgram->AddShader("lighting.vert", GL_VERTEX_SHADER);
    lightingProgram->AddShader("lighting.frag", GL_FRAGMENT_SHADER);

    glBindAttribLocation(lightingProgram->programId, 0, "vertex");
    glBindAttribLocation(lightingProgram->programId, 1, "vertexNormal");
    glBindAttribLocation(lightingProgram->programId, 2, "vertexTexture");
    glBindAttribLocation(lightingProgram->programId, 3, "vertexTangent");
    lightingProgram->LinkProgram();

	// shadow shader program
    shadowProgram = new ShaderProgram();
	shadowProgram->AddShader("shadow.vert", GL_VERTEX_SHADER);
	shadowProgram->AddShader("shadow.frag", GL_FRAGMENT_SHADER);
    glBindAttribLocation(shadowProgram->programId, 0, "vertex");
    shadowProgram->LinkProgram();
	// Create shadow FBO
	shadowFBO.CreateFBO(shadowFBOres, shadowFBOres);

    // gbuffer shader program
    gbufferProgram = new ShaderProgram();
    gbufferProgram->AddShader("gbuffer.vert", GL_VERTEX_SHADER);
    gbufferProgram->AddShader("gbuffer.frag", GL_FRAGMENT_SHADER);
    glBindAttribLocation(gbufferProgram->programId, 0, "vertex");
    glBindAttribLocation(gbufferProgram->programId, 1, "vertexNormal");
    glBindAttribLocation(gbufferProgram->programId, 2, "vertexTexture");
    glBindAttribLocation(gbufferProgram->programId, 3, "vertexTangent");
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

	// local light shader program
	localLightProgram = new ShaderProgram();
	localLightProgram->AddShader("local_light.vert", GL_VERTEX_SHADER);
	localLightProgram->AddShader("local_light.frag", GL_FRAGMENT_SHADER);
    glBindAttribLocation(localLightProgram->programId, 0, "vertex");
	localLightProgram->LinkProgram();
    
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


    // @@ To change an object's surface parameters (Kd, Ks, or alpha),
    // modify the following lines.
    
    central    = new Object(NULL, nullId);
    anim       = new Object(NULL, nullId);
    room       = new Object(RoomPolygons, roomId, brickColor, noSpec, 1, brickTexture, brickNormalMap);
    floor      = new Object(FloorPolygons, floorId, floorColor, brightSpec, 10, floorTexture, floorNormalMap);
    teapot     = new Object(TeapotPolygons, teapotId, brassColor, evenBrighterSpec, 120, teapotTexture, teapotNormalMap);
    teapot2    = new Object(TeapotPolygons, teapotId, brassColor, brightestSpec, 120, teapotTexture, teapotNormalMap);
	teapot3    = new Object(TeapotPolygons, teapotId, brassColor, noSpec, 120, teapotTexture, teapotNormalMap);
    podium     = new Object(BoxPolygons, boxId, glm::vec3(woodColor), brightSpec, 10, rockTexture, rockNormalMap); 
    sky        = new Object(SpherePolygons, skyId, noSpec, noSpec, 0, skyTexture);
    objectRoot->add(sky, Scale(2000.0, 2000.0, 2000.0));
    ground     = new Object(GroundPolygons, groundId, grassColor, noSpec, 1, grassTexture, grassNormalMap);
    sea        = new Object(SeaPolygons, seaId, waterColor, brightSpec, 120, skyTexture, waterNormalMap);
    leftFrame  = FramedPicture(Identity, lPicId, BoxPolygons, QuadPolygons, woodTexture, woodNormalMap, displayTexture1);
    rightFrame = FramedPicture(Identity, rPicId, BoxPolygons, QuadPolygons, woodTexture, woodNormalMap);
    spheres    = SphereOfSpheres(SpherePolygons);
#ifdef REFL
    spheres->drawMe = true;
#else
    spheres->drawMe = true;
    room->drawMe = false;
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

    // Options menu stuff
    show_demo_window = true;
}

void Scene::DrawMenu()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // UI toggles/state
    static bool showStatsWindow = false;
    static bool showControlsWindow = false;
    static bool showLightingWindow = false;
    static bool wireframe = false;
    static bool vsync = true;
    static bool MSAA = true;
	static bool showDemoWindow = false;
    if (ImGui::BeginMainMenuBar()) {
        // This menu demonstrates how to provide the user a list of toggleable settings.
        if (ImGui::BeginMenu("Objects")) {
            if (ImGui::MenuItem("Draw spheres", "", spheres->drawMe))  {spheres->drawMe ^= true; }
            if (ImGui::MenuItem("Draw walls", "", room->drawMe))       {room->drawMe ^= true; }
            if (ImGui::MenuItem("Draw ground/sea", "", ground->drawMe)){ground->drawMe ^= true;
                							sea->drawMe = ground->drawMe;}
            ImGui::EndMenu(); }
        // camera controls
        if (ImGui::BeginMenu("View")) {
            bool isFirstPerson = transformation_mode;
            if (ImGui::MenuItem("First-person", "", isFirstPerson)) { transformation_mode = true; }
            if (ImGui::MenuItem("Orbit (world)", "", !isFirstPerson)) { transformation_mode = false; }
            if (ImGui::MenuItem("Reset camera")) {
                spin = 0.0f;
                tilt = 30.0f;
                tx = 0.0f;
                ty = 0.0f;
                zoom = 25.0f;
                ry = 0.4f;
                front = 0.5f;
                back = 5000.0f;
                eye = glm::vec3(0.0f, -20.0f, 0.0f);
                speed = 10.0f;
            }
            ImGui::EndMenu();
        }
        // utility
        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::MenuItem("Draw local light spheres", "", localLightDebug)) {
                localLightDebug ^= true;
            }
            if (ImGui::MenuItem("Only use local lights", "", onlyLocalLights)) {
                onlyLocalLights ^= true;
            }
            if (ImGui::MenuItem("VSync", "", vsync)) {
                vsync ^= true;
                glfwSwapInterval(vsync ? 1 : 0);
            }
            if (ImGui::MenuItem("MSAA", "", MSAA)) {
                MSAA ^= true;
                if (MSAA)
                    glEnable(GL_MULTISAMPLE);
                else
                    glDisable(GL_MULTISAMPLE);
			}
            if (ImGui::MenuItem("Stats window", "", showStatsWindow)) { showStatsWindow ^= true; }
            if (ImGui::MenuItem("Controls window", "", showControlsWindow)) { showControlsWindow ^= true; }
            if (ImGui::MenuItem("Lighting window", "", showLightingWindow)) { showLightingWindow ^= true; }
			if (ImGui::MenuItem("Show ImGui demo", "", showDemoWindow)) { showDemoWindow ^= true; }
            ImGui::EndMenu();
        }
        // This menu demonstrates how to provide the user a choice
        // among a set of choices.  The current choice is stored in a
        // variable named "mode" in the application, and sent to the
        // shader to be used as you wish.
        if (ImGui::BeginMenu("Render Mode ")) {
            if (ImGui::MenuItem("Forward shading", "", mode == 0)) { mode = 0; }

            // G-buffer debug views (mode 1..4)
            if (ImGui::MenuItem("GBuffer: WorldPos", "", mode == 1)) { mode = 1; }
            if (ImGui::MenuItem("GBuffer: Normal", "", mode == 2)) { mode = 2; }
            if (ImGui::MenuItem("GBuffer: Kd", "", mode == 3)) { mode = 3; }
            if (ImGui::MenuItem("GBuffer: Ks/Alpha", "", mode == 4)) { mode = 4; }

            // Deferred shading + local lights
            if (ImGui::MenuItem("Deferred + Local lights", "", mode == 5)) { mode = 5; }

            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar(); }
  

    // camera contrls window
    if (showControlsWindow) {
        ImGui::Begin("Camera Controls");
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

    // lighting controls window
    if (showLightingWindow) {
        ImGui::Begin("Lighting");
        ImGui::SliderFloat("Light distance", &lightDist, 1.0f, 1000.0f);
        ImGui::SliderFloat("Light spin (deg)", &lightSpin, -180.0f, 180.0f);
        ImGui::SliderFloat("Light tilt (deg)", &lightTilt, -89.9f, 89.9f);
        ImGui::End();
    }

    // stats window
    if (showStatsWindow) {
        ImGui::Begin("Stats");
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, (io.Framerate > 0.0f) ? (1000.0f / io.Framerate) : 0.0f);
        ImGui::Text("Viewport: %d x %d", width, height);
        ImGui::Text("Time: %.3f  dt: %.3f  step: %.3f", currentTime, timeSinceLastRefresh, step);
        ImGui::Text("LightPos: (%.2f, %.2f, %.2f)", lightPos.x, lightPos.y, lightPos.z);
        ImGui::Text("Local light count: %d", localLightCount);
        ImGui::End();
    }
	// ImGui demo window
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

    CHECKERROR;
	objectRoot->Draw(shadowProgram, Identity);
    CHECKERROR;

    glDisable(GL_CULL_FACE);

	shadowFBO.UnbindFBO();
    shadowProgram->UnuseShader();



    ////////////////////////////////////////////////////////////////////////////////
    // G-buffer pass
    ////////////////////////////////////////////////////////////////////////////////
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

    ////////////////////////////////////////////////////////////////////////////////
    // Deferred shading pass + local lights
    ////////////////////////////////////////////////////////////////////////////////
    if (mode == 5)
    {
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
        glUniform3fv(glGetUniformLocation(programId, "lightPos"), 1, (float*)&lightPos);
        glm::vec3 eyePosWS = glm::vec3(WorldInverse * glm::vec4(0, 0, 0, 1));
        glUniform3fv(glGetUniformLocation(programId, "eyePos"), 1, (float*)&eyePosWS);
        glUniformMatrix4fv(glGetUniformLocation(programId, "ShadowMatrix"), 1, GL_FALSE, glm::value_ptr(ShadowMatrix));

        // draw full-screen triangle to apply lighting to all pixels
        glBindVertexArray(deferredVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        deferredProgram->UnuseShader();
		////////////////////////////////////////////////////////////////////////////////
        // local lights pass
        ////////////////////////////////////////////////////////////////////////////////
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

        return;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Non-deferred shading pass
    ////////////////////////////////////////////////////////////////////////////////



    // Choose the lighting shader
    lightingProgram->UseShader();
    programId = lightingProgram->programId;

    // Set the viewport, and clear the screen
    glViewport(0, 0, width, height);
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT| GL_DEPTH_BUFFER_BIT);


    // @@ The scene specific parameters (uniform variables) used by
    // the shader are set here.  Object specific parameters are set in
    // the Draw procedure in object.cpp
    
    loc = glGetUniformLocation(programId, "WorldProj");
    glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(WorldProj));
    loc = glGetUniformLocation(programId, "WorldView");
    glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(WorldView));
    loc = glGetUniformLocation(programId, "WorldInverse");
    glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(WorldInverse));
    loc = glGetUniformLocation(programId, "lightPos");
    glUniform3fv(loc, 1, &(lightPos[0]));   
    loc = glGetUniformLocation(programId, "mode");
    glUniform1i(loc, mode);

    loc = glGetUniformLocation(programId, "Light");
    glUniform3fv(loc, 1, &(Light[0]));
    loc = glGetUniformLocation(programId, "Ambient");
    glUniform3fv(loc, 1, &(Ambient[0]));

    loc = glGetUniformLocation(programId, "time");
    glUniform1f(loc, currentTime);

	loc = glGetUniformLocation(programId, "ShadowMatrix");
	glUniformMatrix4fv(loc, 1, GL_FALSE, Pntr(ShadowMatrix));

    shadowFBO.BindTexture(0, 3, programId, "shadowMap");

    CHECKERROR;

    // Draw all objects (This recursively traverses the object hierarchy.)
    CHECKERROR;
    objectRoot->Draw(lightingProgram, Identity);
    CHECKERROR; 

	shadowFBO.UnbindTexture(3);
    // Turn off the shader
    lightingProgram->UnuseShader();

    ////////////////////////////////////////////////////////////////////////////////
    // End of Lighting pass
    ////////////////////////////////////////////////////////////////////////////////
}
