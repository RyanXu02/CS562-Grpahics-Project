////////////////////////////////////////////////////////////////////////
// The scene class contains all the parameters needed to define and
// draw a simple scene, including:
//   * Geometry
//   * Light parameters
//   * Material properties
//   * Viewport size parameters
//   * Viewing transformation values
//   * others ...
//
// Some of these parameters are set when the scene is built, and
// others are set by the framework in response to user mouse/keyboard
// interactions.  All of them can be used to draw the scene.

#include "shapes.h"
#include "object.h"
#include "texture.h"
#include "fbo.h"

enum ObjectIds {
    nullId	= 0,
    skyId	= 1,
    seaId	= 2,
    groundId	= 3,
    roomId	= 4,
    boxId	= 5,
    frameId	= 6,
    lPicId	= 7,
    rPicId	= 8,
    teapotId	= 9,
    spheresId	= 10,
    floorId     = 11
};

class Shader;

class Scene
{
public:
    GLFWwindow* window;

    // @@ Declare interactive viewing variables here. (spin, tilt, ry, front back, ...)
	float spin, tilt, tx, ty, zoom, ry, rx, front, back;

    glm::vec3 eye;
    float speed;
    float step;
    float lastRefreshTime;
    float currentTime;
    float timeSinceLastRefresh;

    bool transformation_mode;
    bool w_key = false;
    bool a_key = false;
    bool s_key = false;
    bool d_key = false;

    // Light parameters
    float lightSpin, lightTilt, lightDist;
    glm::vec3 lightPos;
    // @@ Perhaps declare additional scene lighting values here. (lightVal, lightAmb)
    glm::vec3 Light;
	glm::vec3 Ambient;
    
    // many lights
    struct LocalLight
    {
        glm::vec3 position{ 0.0f, 0.0f, 0.0f };
        glm::vec3 color{ 1.0f, 1.0f, 1.0f };
        float radius{ 1.0f };
    };
    std::vector<LocalLight> localLights;
    Shape* sphere_light = nullptr; // spheres for lights
    int localLightDebug = 0; // 1 for debug mode
	int onlyLocalLights = 0; // 1 for only local lights mode
    int localLightCount = 128;


    int mode; // Extra mode indicator hooked up to number keys and sent to shader
    
    // Viewport
    int width, height;

    // Transformations
    glm::mat4 WorldProj, WorldView, WorldInverse;

    // light transformations
	glm::mat4 LightProj, LightView, ShadowMatrix;

    // All objects in the scene are children of this single root object.
    Object* objectRoot;
    Object *central, *anim, *room, *floor, *teapot, *teapot2, *teapot3, *podium, *sky,
            *ground, *sea, *spheres, *leftFrame, *rightFrame;

    std::vector<Object*> animated;
    ProceduralGround* proceduralground;

    // Shader programs

	// non-deferred shading
    ShaderProgram* lightingProgram;

    // shadows
    ShaderProgram* shadowProgram;
    FBO shadowFBO;

    // G-buffer
    ShaderProgram* gbufferProgram;
    ShaderProgram* gbufferDebugProgram;
    FBO gbufferFBO;
    // full screen quad for gbuffer debugging
    unsigned int gbufferDebugVAO;

	// Deferred shading
    ShaderProgram* deferredProgram;
	// full screen quad for deferred shading
    unsigned int deferredVAO;
    
    // Local lights
	ShaderProgram* localLightProgram;

    // Options menu stuff
    bool show_demo_window;

    void InitializeScene();
    void BuildTransforms();
    void DrawMenu();
    void DrawScene();

};
