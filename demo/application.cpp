#include "application.hpp"

#include <OgreRoot.h>
#include <OgreCamera.h>
#include <OgreEntity.h>

#include <OgreTimer.h>

#include "sdl4ogre/sdlwindowhelper.hpp"
#include "sdl4ogre/sdlinputwrapper.hpp"

#include <extern/shiny/Main/Factory.hpp>
#include <extern/shiny/Platforms/Ogre/OgrePlatform.hpp>

#include "../terrain/world.hpp"
#include "../terrain/terrainstorage.hpp"

class MyTerrainStorage : public Terrain::TerrainStorage
{
public:
    MyTerrainStorage()
    {
        for (int i=0; i<512; ++i)
            for (int j=0; j<512; ++j)
                mHeightmap[i][j] = std::sin(j/50.f) * 400 + std::cos(i/50.f) * 200;
    }

    virtual int getHeightmapSize()
    {
        return 512;
    }

    virtual int getBlendmapSize()
    {
        return 512;
    }

    virtual int getWorldSize()
    {
        return getHeightmapSize() * 10;
    }

    virtual void loadHeightmap(float* array)
    {
        memcpy(array, mHeightmap, 512*512*sizeof(float));
    }

    virtual void loadBlendmap (std::vector<Terrain::LayerInfo>& layers, int* layerIndices)
    {
        const char* textures[] =
        {
            "adesert_cracks_d.jpg",
            "adesert_mntn2_d.jpg",
            "adesert_mntn4_d.jpg",
            "adesert_mntn4v_d.jpg",
            "adesert_mntn_d.jpg",
            "adesert_rocky_d.jpg",
            "adesert_sand2_d.jpg",
            "adesert_stone_d.jpg"
        };
        int numtextures = sizeof(textures)/sizeof(textures[0]);

        for (int i=0; i<numtextures; ++i)
        {
            Terrain::LayerInfo layerInfo;
            layerInfo.mDiffuseMap = textures[i];
            layers.push_back(layerInfo);
        }

        for (int x=0; x<getBlendmapSize(); ++x)
        {
            for (int y=0; y<getBlendmapSize(); ++y)
            {
                int layerIndex = std::rand() / (RAND_MAX+1.0) * numtextures;
                layerIndices[y*getBlendmapSize() + x] = layerIndex;
            }
        }
    }

private:
    float mHeightmap[512][512];
};


Application::Application()
    : mInputWrapper(NULL)
    , mRoot(NULL)
    , mSDLWindow(NULL)
    , mOgreWindow(NULL)
    , mShutdown(false)
    , mSceneMgr(NULL)
    , mCamera(NULL)
    , mTerrain(NULL)
    , mTimerPrintFPS(1.f)
    , mFreeze(false)
{

}

Application::~Application()
{
    delete mInputWrapper;
}

void Application::run()
{
    Uint32 flags = SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE;
    if(SDL_Init(flags) != 0)
    {
        throw std::runtime_error("Could not initialize SDL! " + std::string(SDL_GetError()));
    }

    // Construct Ogre::Root
    // We handle our stuff manually, so don't want to use any of the files
    mRoot = OGRE_NEW Ogre::Root(
        /* plugins.cfg file*/	"",
        /* config file */ 		"",
        /* log file */ 			""
    );

    // Set render system
    mRoot->loadPlugin(OGRE_PLUGIN_DIR_REL + std::string("/RenderSystem_GL"));
    Ogre::RenderSystem* rs = mRoot->getRenderSystemByName("OpenGL Rendering Subsystem");
    mRoot->setRenderSystem(rs);

    // Fire up Ogre::Root
    mRoot->initialise(false);

    bool fullscreen=false;
    int w = 800;
    int h = 600;
    const std::string title = "Terrain Demo";

    mSDLWindow = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
                     SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    Ogre::NameValuePairList params;
    params.insert(std::make_pair("title", title));
    params.insert(std::make_pair("FSAA", "0"));
    params.insert(std::make_pair("vsync", "true"));
    SFO::SDLWindowHelper helper (mSDLWindow, w, h, title, fullscreen, params);
    mOgreWindow = helper.getWindow();

    Ogre::ResourceGroupManager::getSingleton().addResourceLocation("../media", "FileSystem");

    Ogre::ResourceGroupManager::getSingleton().declareResource("lava.png", "Texture", "General");

    Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

    mInputWrapper = new SFO::InputWrapper(mSDLWindow, mOgreWindow, false);
    mInputWrapper->setWindowEventCallback(this);
    mInputWrapper->setMouseEventCallback(this);
    mInputWrapper->setKeyboardEventCallback(this);
    mInputWrapper->setGrabPointer(true);
    mInputWrapper->setMouseRelative(true);

    mSceneMgr = mRoot->createSceneManager(Ogre::ST_GENERIC);
    mSceneMgr->setAmbientLight(Ogre::ColourValue(0.4, 0.4, 0.4));
    mCamera = mSceneMgr->createCamera("");
    mCamera->setNearClipDistance(0.1);
    mCamera->setFarClipDistance(10000);
    Ogre::Viewport* vp = mOgreWindow->addViewport(mCamera);
    vp->setBackgroundColour(Ogre::ColourValue(0.1,0.1,0.1,1.0));

    Ogre::Light* light = mSceneMgr->createLight();
    light->setType(Ogre::Light::LT_DIRECTIONAL);
    light->setDirection(Ogre::Vector3(0.8, -1, 0.3));
    light->setDiffuseColour(Ogre::ColourValue(1,1,1,1));

    Ogre::TextureManager::getSingleton().setDefaultNumMipmaps(8);
    Ogre::MaterialManager::getSingleton().setDefaultAnisotropy(8);
    Ogre::MaterialManager::getSingleton().setDefaultTextureFiltering(Ogre::TFO_ANISOTROPIC);

    // Create material system
    sh::OgrePlatform* platform = new sh::OgrePlatform("General", "../media");
    std::auto_ptr<sh::Factory> factory (new sh::Factory(platform));
    factory->setCurrentLanguage(sh::Language_GLSL);
    factory->loadAllFiles();
    factory->setGlobalSetting("shadows", "false");
    factory->setGlobalSetting("shadows_pssm", "false");
    factory->setGlobalSetting("num_lights", "1");
    factory->setShaderDebugOutputEnabled(true);

    mCamera->setPosition(0,400,0);

    // Create the terrain
    MyTerrainStorage* storage = new MyTerrainStorage();
    mTerrain = new Terrain::World(mSceneMgr, storage, 1, true, true, Terrain::Align_XZ, 1, 128);

    // Add entities onto the terrain at some random positions
    for (int i=0; i<100; ++i)
    {
        float x = (std::rand() / static_cast<float>(RAND_MAX) - 0.5) * storage->getWorldSize();
        float y = (std::rand() / static_cast<float>(RAND_MAX) - 0.5) * storage->getWorldSize();

        float height = mTerrain->getHeightAt(Ogre::Vector3(x, 0, y));
        Ogre::Vector3 normal = mTerrain->getNormalAt(Ogre::Vector3(x, 0, y));
        normal.normalise();

        Ogre::Quaternion orient = Ogre::Vector3(0,0,1).getRotationTo(normal);

        Ogre::Entity* cubeEnt = mSceneMgr->createEntity(Ogre::SceneManager::PT_PLANE);
        cubeEnt->setMaterialName("BaseWhite");
        Ogre::SceneNode* sceneNode = mSceneMgr->getRootSceneNode()->createChildSceneNode(Ogre::Vector3(x, height+0.1f, y));
        sceneNode->setScale(Ogre::Vector3(0.05, 0.05, 0.05));
        sceneNode->setOrientation(orient);
        sceneNode->attachObject(cubeEnt);
    }

    // Start the rendering loop
    mRoot->addFrameListener(this);
    mRoot->startRendering();

    delete mTerrain;

    factory.reset();

    mRoot->removeFrameListener(this);
    OGRE_DELETE mRoot;

    SDL_DestroyWindow(mSDLWindow);
    SDL_Quit();
}

void Application::windowResized(int x, int y)
{

}

void Application::windowClosed()
{
    mShutdown = true;
}

bool Application::frameRenderingQueued(const Ogre::FrameEvent &evt)
{
    mInputWrapper->capture(false);

    if (!mFreeze)
        mTerrain->update(mCamera->getRealPosition());

    mTimerPrintFPS -= evt.timeSinceLastFrame;
    if (mTimerPrintFPS <= 0)
    {
        mTimerPrintFPS += 1;
        std::cout << "FPS: " << mOgreWindow->getLastFPS() << " batches: "
                  << mOgreWindow->getBatchCount() << " triangles: " << mOgreWindow->getTriangleCount() << std::endl;
    }

    // Camera movement
    Ogre::Vector3 movementVector (0,0,0);
    if (mInputWrapper->isKeyDown(SDL_GetScancodeFromKey(SDLK_w)))
        movementVector.z = -1;
    else if (mInputWrapper->isKeyDown(SDL_GetScancodeFromKey(SDLK_s)))
        movementVector.z = 1;
    if (mInputWrapper->isKeyDown(SDL_GetScancodeFromKey(SDLK_a)))
        movementVector.x = -1;
    else if (mInputWrapper->isKeyDown(SDL_GetScancodeFromKey(SDLK_d)))
        movementVector.x = 1;

    movementVector *= evt.timeSinceLastFrame * 200;

    if (mInputWrapper->isModifierHeld(KMOD_LSHIFT))
        movementVector *= 5;

    mCamera->moveRelative(movementVector);

    return !mShutdown;
}

void Application::keyPressed(const SDL_KeyboardEvent &arg)
{
    if (arg.keysym.sym == SDLK_c)
        mCamera->setPolygonMode(mCamera->getPolygonMode() == Ogre::PM_SOLID ? Ogre::PM_WIREFRAME : Ogre::PM_SOLID);
    if (arg.keysym.sym == SDLK_f)
        mFreeze = !mFreeze;
    if (arg.keysym.sym == SDLK_ESCAPE)
        mShutdown = true;
}

void Application::keyReleased(const SDL_KeyboardEvent &arg)
{

}

void Application::mouseMoved(const SFO::MouseMotionEvent &arg)
{
    mCamera->yaw(Ogre::Radian(-arg.xrel*0.01));
    mCamera->pitch(Ogre::Radian(-arg.yrel*0.01));
}

void Application::mousePressed(const SDL_MouseButtonEvent &arg, Uint8 id)
{

}

void Application::mouseReleased(const SDL_MouseButtonEvent &arg, Uint8 id)
{

}
