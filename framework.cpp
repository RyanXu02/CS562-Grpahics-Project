///////////////////////////////////////////////////////////////////////
// Provides the framework for graphics projects, mostly just GLFW
// initialization and main loop.
////////////////////////////////////////////////////////////////////////

#include "framework.h"

// If you are running on a laptop with both an INTEL integrated GPU and a discrete GPU
// AND if your program is not running correctly on the INTEL GPU
// AND if your laptop is not set up to use the discrete GPU by default
// AND you don't know how to force it to use the discrete GPU
// THEN uncomment the following to force this application to run on the discrete GPU
// OTHERWISE leave this untouched.
//
//#ifdef _WIN32 
//extern "C"
//{
//    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
//    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
//}
//#endif

Scene scene;

static void error_callback(int error, const char* msg)
{
    fputs(msg, stderr);
}

// Force dedicated NVIDIA GPU on Optimus systems
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 0x00000001;
}

////////////////////////////////////////////////////////////////////////
// Do the OpenGL/GLFW setup and then enter the interactive loop.
int main(int argc, char** argv)
{
    glfwSetErrorCallback(error_callback);

    // Initialize the OpenGL bindings
    glbinding::Binding::initialize(false);

    // Initialize glfw open a window
    if (!glfwInit())  exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_RESIZABLE, 1);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, 0);
    glfwWindowHint(GLFW_SAMPLES, 4);
    scene.window = glfwCreateWindow(750,750, "Graphics Framework", NULL, NULL);
    if (!scene.window)  { glfwTerminate();  exit(-1); }

    glfwMakeContextCurrent(scene.window);
    glfwSwapInterval(1);

    ImGui::CreateContext();
    //ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplGlfw_InitForOpenGL(scene.window, true);
    ImGui_ImplOpenGL3_Init();
    
    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    printf("GLSL Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("Rendered by: %s\n", glGetString(GL_RENDERER));
    fflush(stdout);

    // Initialize interaction and the scene to be drawn.
    InitInteraction();
    scene.InitializeScene();
    
    // Enter the event loop.
    while (!glfwWindowShouldClose(scene.window)) {
        glfwPollEvents();

        scene.DrawScene();
        scene.DrawMenu();
        glfwSwapBuffers(scene.window); }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
}
