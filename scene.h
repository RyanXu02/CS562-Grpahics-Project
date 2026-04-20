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
#include "HDR.h"

#include "pencil_preprocess.h"

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
	int noLocalLight = 0; // 1 to disable local lights (test IBL + direct light)
    int noDirectLight = 0; // 1 to disable direct lights (test diffuse IBL)

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

    // shadows
    ShaderProgram* shadowProgram;
    FBO shadowFBO;
    // shadows blur
    ShaderProgram* blurHProgram;
    ShaderProgram* blurVProgram;
    ShaderProgram* shadowDebugProgram;
    FBO shadowBlurFBO;
    int blurRadius = 30;
    bool shadowShowBlurred = true;
    float z0;
    float z1;

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

    //HDR
	HDR* hdr;
    HDR* hdrIrr;
	float exposure = 2.0f;
    float teapotAlpha = 120.0f;

    // Pencil rendering preprocessed assets
    unsigned int pencilTones3D = 0; // GL_TEXTURE_3D:  WxHx32 tone stack
    unsigned int paperNormalMap = 0; // GL_TEXTURE_2D:  paper tooth normals
    unsigned int contourPencilTex = 0; // GL_TEXTURE_2D: strokes for contour tint

    // Ambient Occlusion
    ShaderProgram* aoProgram;
    ShaderProgram* aoBlurHProgram;
    ShaderProgram* aoBlurVProgram;
    FBO aoFBO;
    FBO aoBlurFBO;
    // fullscreen triangle VAO for AO pass
    unsigned int aoVAO; 
    // AO parameters
    float aoRadius = 1.0f; // R
    int aoSamples = 15; // n
    float aoScale = 1.0f; // s
    float aoContrast = 1.0f; // k
    float aoDelta = 0.001f; // depth bias
    float aoSigma = 0.01f; // bilateral range variance
    int aoBlurRadius = 5;
    // AO debug shader
    ShaderProgram* aoDebugProgram;


#pragma region PencilRendering
    // Pencil contour pass
    ShaderProgram* contourProgram;
    ShaderProgram* contourDebugProgram;
    FBO contourFBO;
    // Contour pass parameters
    float contourNormalThreshold = 0.6f;  // 1=coplanar only, 0.5=60° crease
    float contourDepthThreshold = 0.5f;  // worldspace units
    float contourShakeAmp = 0.002f; // in UV
    float contourShakeFreq = 40.0f;
    int   contourNumShakes = 4;     // 3-5 per paper
    float contourPencilTile = 4.0f;

    // Pencil interior pass
    ShaderProgram* interiorProgram;
    ShaderProgram* interiorDebugProgram;
    FBO interiorFBO;
    // interior pass parameters
    float interiorPencilTile = 6.0f;
    float interiorPaperStrength = 0.05f;  // mu_p in the paper (0..0.1)
    float interiorPaperTile = 2.0f;   // paper tooth frequency
    float interiorCrossHatchBelow = 0.35f; // cross-hatch below this brightness
    
    // pencil composite pass
    ShaderProgram* pencilCompositeProgram;
    FBO deferredOutputFBO;  // for deferred+local lights
    int pencilMode = 0;
    float pencilContrast = 0.3f;
    glm::vec3 pencilPaperColor = glm::vec3(0.96f, 0.95f, 0.93f); // warm cream

#pragma endregion
    // Options menu stuff
    bool show_demo_window;

    void InitializeScene();
    void BuildTransforms();
    void DrawMenu();
    void DrawScene();

};
