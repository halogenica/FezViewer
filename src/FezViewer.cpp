#include "Common.h"
#include "Trile.h"
#include "ArtObject.h"
#include "BackgroundPlane.h"

gl::Texture* Trile::s_pTexture;

class FezViewer : public AppBasic
{
  public:
    void prepareSettings(Settings* pSettings);
    void setup();
    void shutdown();
    void setDisplayString(const string& str);
    void spawnLoader(fs::path file);
    void loadArtObject();
    void loadLevel();
    void resize();
    void resetCamera(float zoom);
    void mouseDown(MouseEvent event);
    void mouseDrag(MouseEvent event);
    void mouseWheel(MouseEvent event);
    void keyDown(KeyEvent event);
    void fileDrop(FileDropEvent event);
    void update();
    void draw();

    MayaCamUI               m_camera;
    fs::path                m_file;
    bool                    m_verbose;
    Vec3f                   m_dimensions;
    shared_ptr<thread>      m_thread;
    mutex                   m_mutex;
    bool                    m_exit;
    bool                    m_quit;

    deque<Trile>            m_triles;
    Surface                 m_trileSurface;
    gl::Texture             m_trileTexture;
    bool                    m_trileTexReload;

    deque<ArtObject>        m_artObjects;

    deque<BackgroundPlane>  m_backgroundPlanes;
    
    TextBox*                m_pText;    // Cinder bug requires this to be a pointer, initialized in setup()
    gl::Texture             m_textTexture;
    Anim<float>             m_textAlpha;
    bool                    m_textReload;
};

void FezViewer::prepareSettings(Settings* pSettings)
{
    pSettings->setTitle("Fez Viewer");
    pSettings->setWindowSize(1280, 720);
    pSettings->setFullScreen(false);
    pSettings->setFrameRate(60.0f);
}

void FezViewer::setup()
{
    m_file = "";
#ifdef DEBUG
    m_verbose = true;
#else 
    m_verbose = false;
#endif
    m_dimensions = Vec3f::zero();
    m_thread = nullptr;
    m_exit = false;
    m_quit = false;
    
    m_trileTexReload = false;

    m_pText = new TextBox();
    m_pText->setFont(Font(app::loadResource(RES_MY_FONT), 30));
    m_pText->setColor(ci::Colorf(1.f, 1.f, 1.f));
    m_pText->setAlignment(TextBox::LEFT);
    m_pText->setSize(Vec2f(getWindowWidth(), TextBox::GROW));
    m_pText->setText("FezViewer v0.2 \nPress 'O' or drag and drop file to open");
    m_textTexture = gl::Texture(m_pText->render());
    m_textAlpha = 1.f;
    m_textReload = false;
    timeline().apply(&m_textAlpha, 1.f, 8.f);
    timeline().apply(&m_textAlpha, 0.f, 1.f, EaseOutExpo()).appendTo(&m_textAlpha);
    
    const auto args = getArgs();
    for (auto arg : args)
    {
        if (arg == "-verbose")
        {
            m_verbose = true;
        }
        // TODO: how to handle loading file from command line?
    }
    
    if (m_verbose)
    {
        int i = 0;
        for (auto arg : args)
        {
            console() << "Arg " << i << ": " << arg << endl;
            ++i;
        }
    }
    
    gl::enableDepthRead();
    gl::enableDepthWrite();
    gl::enableAlphaBlending();
    gl::enable(GL_CULL_FACE);
    
    resetCamera(25.f);
    
    const Vec3f vertices[] = { Vec3f( -8.f,  8.f,  8.f ),   // 0
                               Vec3f( -8.f, -8.f,  8.f ),   // 1
                               Vec3f(  8.f, -8.f,  8.f ),   // 2
                               Vec3f(  8.f,  8.f,  8.f ),   // 3
                               Vec3f( -8.f,  8.f, -8.f ),   // 4
                               Vec3f( -8.f, -8.f, -8.f ),   // 5
                               Vec3f(  8.f, -8.f, -8.f ),   // 6
                               Vec3f(  8.f,  8.f, -8.f ) }; // 7
    
    const Vec2f texcoords[] = { Vec2f( 0.f, 0.f ),    // 0
                                Vec2f( 0.f, 0.f ),    // 1
                                Vec2f( 0.f, 0.f ),    // 2
                                Vec2f( 0.f, 0.f ),    // 3
                                Vec2f( 0.f, 0.f ),    // 4
                                Vec2f( 0.f, 0.f ),    // 5
                                Vec2f( 0.f, 0.f ),    // 6
                                Vec2f( 0.f, 0.f ) };  // 7
    
    const uint32_t indices[] = { 0, 1, 2, 2, 3, 0,   // front
                                 7, 6, 5, 5, 4, 7,   // back
                                 4, 5, 1, 1, 0, 4,   // left
                                 3, 2, 6, 6, 7, 3,   // right
                                 4, 0, 3, 3, 7, 4,   // top
                                 6, 2, 1, 1, 5, 6 }; // bottom
    
    TriMesh mesh;
    mesh.appendVertices(vertices, extent<decltype(vertices)>::value);
    mesh.appendTexCoords(texcoords, extent<decltype(texcoords)>::value);
    mesh.appendIndices(indices, extent<decltype(indices)>::value);
    
    Surface surf = Surface(1, 1, false);
    surf.setPixel(Vec2i::zero(), ColorAf(0.7f, 0.7f, 0.7f));
    
    m_artObjects.push_back(ArtObject(mesh, surf));
}

void FezViewer::shutdown()
{
    m_exit = true;
    if (m_thread)
    {
        m_thread->join();
        m_thread = nullptr;
    }
    delete m_pText;
}

void FezViewer::setDisplayString(const string& str)
{
    
    lock_guard<mutex> lock( m_mutex );
    m_pText->setText(str);
    m_textAlpha = 1.f;
    m_textReload = true;
     
    if (m_verbose)
    {
        console() << str << endl;
    }
}

void FezViewer::spawnLoader(const fs::path file)
{
    m_exit = 1;
    if (m_thread)
    {
        m_thread->join();
        m_thread = nullptr;
    }
    
    m_triles.clear();
    m_trileTexReload = false;
    Trile::s_pTexture = nullptr;
    
    m_artObjects.clear();
    
    m_backgroundPlanes.clear();

    m_file = file;
    m_exit = 0;
    
    const string directory = (--file.parent_path().end())->string();
    if (directory == "levels")
    {
        resetCamera(25.f);
        m_thread = shared_ptr<thread>( new thread( bind( &FezViewer::loadLevel, this ) ) );
    }
    else if (directory == "art objects")
    {
        resetCamera(15.f);
        m_thread = shared_ptr<thread>( new thread( bind( &FezViewer::loadArtObject, this ) ) );
    }
    else
    {
        ostringstream displayString;
        displayString << "ERROR! Expected a 'level' or 'art object' .xml file:\n" << file;
        setDisplayString(displayString.str());
    }
}

void FezViewer::loadArtObject()
{
    // TODO: path.preferred_separator doesn't work on windows
    const auto sep = '/';
    
    // Load art objects
    fs::path artObjectXml = m_file;
    if (getPathExtension(artObjectXml.string()) != "xml")
    {
        ostringstream displayString;
        displayString << "ERROR! Art Object extension is not .xml: " << artObjectXml;
        setDisplayString(displayString.str());
        return;
    }

    if (exists(artObjectXml))
    {
        ostringstream displayString;
        displayString << "Loading Art Object .xml: " << artObjectXml.filename();
        setDisplayString(displayString.str());
    }
    else
    {
        ostringstream displayString;
        displayString << "ERROR! Missing Art Object .xml: " << artObjectXml;
        setDisplayString(displayString.str());
        return;
    }
    const XmlTree aoXml = XmlTree(loadFile(artObjectXml));
    const XmlTree& lookAtXml = aoXml.getChild("ArtObject/Size/Vector3");
    m_dimensions = Vec3f(lookAtXml.getAttributeValue<float>("x"),
                         lookAtXml.getAttributeValue<float>("y"),
                         lookAtXml.getAttributeValue<float>("z"));

    string aoPngName;
    if (aoXml.getChild("ArtObject").hasAttribute("cubemapPath"))
    {
        // The XBOX content contains a "cubemapPath" attribute with the .png name
        aoPngName = aoXml.getChild("ArtObject")["cubemapPath"].getValue();
    }
    else
    {
        // The PC content infers the .png name from the "name" attribute
        aoPngName = aoXml.getChild("ArtObject")["name"].getValue();
    }

    boost::algorithm::to_lower(aoPngName);
    fs::path artObjectPng = m_file.parent_path().string() + sep + aoPngName + ".png";
    if (exists(artObjectPng))
    {
        ostringstream displayString;
        displayString << "Loading Art Object .png: " << artObjectPng.filename();
        setDisplayString(displayString.str());
    }
    else
    {
        ostringstream displayString;
        displayString << "ERROR! Missing Art Object .png: " << artObjectPng;
        setDisplayString(displayString.str());
        return;
    }
    
    Vec3f pos = Vec3f::zero(); // counteract offset from level loader
    Quatf rot = Quatf(0,0,0);
    Vec3f scale = Vec3f(1.f, 1.f, 1.f);
    Vec3f offset = -m_dimensions/2;
    {
        lock_guard<mutex> lock( m_mutex );
        if (m_exit) { return; }
        m_artObjects.push_back(ArtObject(aoXml, pos, rot, scale, offset, artObjectPng));
    }
    
    ostringstream displayString;
    displayString << "Finished Loading " << m_file.filename().string();
    setDisplayString(displayString.str());
}

void FezViewer::loadLevel()
{
    ci::ThreadSetup threadSetup; // Required for cinder multithreading
    double startTime = getElapsedSeconds();
    
    // TODO: path.preferred_separator doesn't work on windows
    const auto sep = '/';
    
    // Load the level data
    console() << "Loading Level: " << m_file.string() << endl;
    XmlTree level = XmlTree(loadFile(m_file));
    const XmlTree& lookAtXml = level.getChild("Level/Size/Vector3");
    m_dimensions = Vec3f(lookAtXml.getAttributeValue<float>("x"),
                         lookAtXml.getAttributeValue<float>("y"),
                         lookAtXml.getAttributeValue<float>("z"));

    // Load trile sets
    string trileSetName = level.getChild("Level")["trileSetName"].getValue();
    if (m_verbose)
    {
        console() << "Trile Set Name: " << trileSetName << endl;
    }
    boost::algorithm::to_lower(trileSetName);
    fs::path trileSetPath = m_file.parent_path().string() + sep + ".." + sep + "trile sets" + sep + trileSetName;
    fs::path trileSetPng = trileSetPath.string() + ".png";
    fs::path trileSetXml = trileSetPath.string() + ".xml";

    if (exists(trileSetPng))
    {
        ostringstream displayString;
        displayString << "Loading Trile Set .png: " << trileSetPng.filename();
        setDisplayString(displayString.str());
    }
    else
    {
        ostringstream displayString;
        displayString << "ERROR! Missing Trile Set .png: " << trileSetPng;
        setDisplayString(displayString.str());
        return;
    }

    m_trileSurface = loadImage(trileSetPng);
    m_trileTexReload = true;

    if (exists(trileSetXml))
    {
        ostringstream displayString;
        displayString << "Loading Trile Set .xml: " << trileSetXml.filename();
        setDisplayString(displayString.str());
    }
    else
    {
        ostringstream displayString;
        displayString << "ERROR! Missing Trile Set .xml: " << trileSetXml;
        setDisplayString(displayString.str());
        return;
    }
    const XmlTree trileSet = XmlTree(loadFile(trileSetXml));
    string trileSetName2 = trileSet.getChild("TrileSet")["name"].getValue();
    boost::algorithm::to_lower(trileSetName2);
    if (trileSetName != trileSetName2)
    {
        console() << "WARNING! Trile Set Name Mismatch: " << trileSetName << ", " << trileSetName2 << endl;
    }
    map<uint32_t, XmlTree> trileMap;
    for (const auto& trileEntry : trileSet.getChild("TrileSet/Triles"))
    {
        uint32_t key = trileEntry["key"].getValue<int>();
        if (trileMap.find(key) != trileMap.end())
        {
            console() << "WARNING! Duplicate trile key: " << key << endl;
        }
        else
        {
            ostringstream displayString;
            displayString << "Mapping Trile #" << key;
            setDisplayString(displayString.str());
            trileMap.insert(make_pair(key, trileEntry));
        }
        if (m_exit) { return; }
    }
    
    // Load all level triles
    const XmlTree& levelTriles = level.getChild("Level/Triles");
    int numLevelTriles = 0;
    for (const auto& trile : levelTriles)
    {
        const XmlTree& emplacementXml = trile.getChild("TrileEmplacement");
        Vec3f emplacement = Vec3f(emplacementXml["x"].getValue<float>(),
                                  emplacementXml["y"].getValue<float>(),
                                  emplacementXml["z"].getValue<float>());
        {
            numLevelTriles++;
            int tid = trile.getChild("TrileInstance")["trileId"].getValue<int>();

            ostringstream displayString;
            displayString << "Loading Trile " << numLevelTriles << " - Map ID: " << tid;
            setDisplayString(displayString.str());

            if (trileMap.find(tid) != trileMap.end())
            {
                const XmlTree& posXml = trile.getChild("TrileInstance/Position/Vector3");
                Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                                  posXml["y"].getValue<float>(),
                                  posXml["z"].getValue<float>());
                int orient = trile.getChild("TrileInstance")["orientation"].getValue<int>();
                
                {
                    lock_guard<mutex> lock( m_mutex );
                    if (m_exit) { return; }
                    m_triles.push_back(Trile(trileMap[tid], pos, orient, emplacement, -m_dimensions/2));
                }
            }
            else
            {
                ostringstream displayString;
                displayString << "Skipping Trile " << numLevelTriles;
                setDisplayString(displayString.str());
            }
        }

        if (trile.hasChild("TrileInstance/OverlappedTriles"))
        {
            numLevelTriles++;
            int tid = trile.getChild("TrileInstance/OverlappedTriles/TrileInstance")["trileId"].getValue<int>();

            ostringstream displayString;
            displayString << "Loading Trile " << numLevelTriles << " - Map ID: " << tid;
            setDisplayString(displayString.str());
            
            if (trileMap.find(tid) != trileMap.end())
            {
                const XmlTree& posXml = trile.getChild("TrileInstance/OverlappedTriles/TrileInstance/Position/Vector3");
                Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                                  posXml["y"].getValue<float>(),
                                  posXml["z"].getValue<float>());
                int orient = trile.getChild("TrileInstance/OverlappedTriles/TrileInstance")["orientation"].getValue<int>();
                
                {
                    lock_guard<mutex> lock( m_mutex );
                    if (m_exit) { return; }
                    m_triles.push_back(Trile(trileMap[tid], pos, orient, emplacement, -m_dimensions/2));
                }
            }
            else
            {
                ostringstream displayString;
                displayString << "Skipping Trile " << numLevelTriles;
                setDisplayString(displayString.str());
            }
        }
    }
    console() << "Loaded " << numLevelTriles << " Level Triles" << endl;

    // Load art objects
    fs::path artObjectsPath = m_file.parent_path().string() + sep + ".." + sep + "art objects" + sep;
    const XmlTree& levelArtObjects = level.getChild("Level/ArtObjects");
    int numLevelArtObjects = 0;
    for (const auto& object : levelArtObjects)
    {
        numLevelArtObjects++;
        string aoName = object.getChild("ArtObjectInstance")["name"].getValue();

        ostringstream displayString;
        displayString << "Loading Art Object " << numLevelArtObjects << ": " << aoName;
        setDisplayString(displayString.str());
        
        boost::algorithm::to_lower(aoName);
        fs::path artObjectXml = artObjectsPath.string() + aoName + ".xml";
        
        if (exists(artObjectXml))
        {
            ostringstream displayString;
            displayString << "Loading Art Object .xml: " << artObjectXml.filename();
            setDisplayString(displayString.str());
        }
        else
        {
            ostringstream displayString;
            displayString << "ERROR! Missing Art Object .xml: " << artObjectXml;
            setDisplayString(displayString.str());
            return;
        }
        const XmlTree aoXml = XmlTree(loadFile(artObjectXml));
        string aoName2 = aoXml.getChild("ArtObject")["name"].getValue();
        boost::algorithm::to_lower(aoName2);
        if (aoName != aoName2)
        {
            console() << "WARNING! Art Object Name Mismatch: " << aoName << ", " << aoName2 << endl;
        }
        
        string aoPngName;
        if (aoXml.getChild("ArtObject").hasAttribute("cubemapPath"))
        {
            // The XBOX content contains a "cubemapPath" attribute with the .png name
            aoPngName = aoXml.getChild("ArtObject")["cubemapPath"].getValue();
            boost::algorithm::to_lower(aoPngName);
        }
        else
        {
            // The PC content infers the .png name from the "name" attribute
            aoPngName = aoName2;
        }

        fs::path artObjectPng = artObjectsPath.string() + aoPngName + ".png";
        if (exists(artObjectPng))
        {
            ostringstream displayString;
            displayString << "Loading Art Object .png: " << artObjectPng.filename();
            setDisplayString(displayString.str());
        }
        else
        {
            ostringstream displayString;
            displayString << "ERROR! Missing Art Object .png: " << artObjectPng;
            setDisplayString(displayString.str());
            return;
        }
        
        const XmlTree& posXml = object.getChild("ArtObjectInstance/Position/Vector3");
        Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                          posXml["y"].getValue<float>(),
                          posXml["z"].getValue<float>());
        const XmlTree& rotXml = object.getChild("ArtObjectInstance/Rotation/Quaternion");
        Quatf rot = Quatf(rotXml["w"].getValue<float>(),
                          rotXml["x"].getValue<float>(),
                          rotXml["y"].getValue<float>(),
                          rotXml["z"].getValue<float>() );
        const XmlTree& scaleXml = object.getChild("ArtObjectInstance/Scale/Vector3");
        Vec3f scale = Vec3f(scaleXml["x"].getValue<float>(),
                            scaleXml["y"].getValue<float>(),
                            scaleXml["z"].getValue<float>());
        Vec3f offset = -m_dimensions/2 - Vec3f(0.5, 0.5, 0.5);
        {
            lock_guard<mutex> lock( m_mutex );
            if (m_exit) { return; }
            m_artObjects.push_back(ArtObject(aoXml, pos, rot, scale, offset, artObjectPng));
        }
    }
    console() << "Loaded " << numLevelArtObjects << " Art Objects" << endl;

    // Load background planes
    fs::path backgroundPlanePath = m_file.parent_path().string() + sep + ".." + sep + "background planes" + sep;
    const XmlTree& levelBackgroundPlanes = level.getChild("Level/BackgroundPlanes");
    int numLevelBackgroundPlanes = 0;

    for (const auto& plane : levelBackgroundPlanes)
    {
        numLevelBackgroundPlanes++;
        string bpName = plane.getChild("BackgroundPlane")["textureName"].getValue();
        std::replace(bpName.begin(), bpName.end(), '\\', '/');    // Mac doesn't like backslash separators
        boost::algorithm::to_lower(bpName);

        ostringstream displayString;
        displayString << "Loading Background Plane " << numLevelBackgroundPlanes << ": " << bpName;
        setDisplayString(displayString.str());
        
        fs::path backgroundPlanePng;
        XmlTree* pAnimXml = nullptr;
        XmlTree animXml;
        
        if (plane.getChild("BackgroundPlane")["animated"].getValue() == "True")
        {
            fs::path backgroundPlaneXml = backgroundPlanePath.string() + bpName + ".xml";
            backgroundPlanePng = backgroundPlanePath.string() + bpName + ".ani.png";

            if (exists(backgroundPlaneXml))
            {
                ostringstream displayString;
                displayString << "Loading Background Plane .xml: " << backgroundPlaneXml.filename();
                setDisplayString(displayString.str());
            }
            else
            {
                ostringstream displayString;
                displayString << "ERROR! Missing Trile Set .xml: " << backgroundPlaneXml;
                setDisplayString(displayString.str());
                return;
            }
            animXml = XmlTree(loadFile(backgroundPlaneXml));
            pAnimXml = &animXml;
        }
        else
        {
            backgroundPlanePng = backgroundPlanePath.string() + bpName + ".png";
        }

        if (exists(backgroundPlanePng))
        {
            ostringstream displayString;
            displayString << "Loading Background Plane .png: " << backgroundPlanePng.filename();
            setDisplayString(displayString.str());
        }
        else
        {
            ostringstream displayString;
            displayString << "ERROR! Missing Background Plane .png: " << backgroundPlanePng;
            setDisplayString(displayString.str());
            return;
        }
        
        const XmlTree& posXml = plane.getChild("BackgroundPlane/Position/Vector3");
        Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                          posXml["y"].getValue<float>(),
                          posXml["z"].getValue<float>());
        const XmlTree& rotXml = plane.getChild("BackgroundPlane/Rotation/Quaternion");
        Quatf rot = Quatf(rotXml["w"].getValue<float>(),
                          rotXml["x"].getValue<float>(),
                          rotXml["y"].getValue<float>(),
                          rotXml["z"].getValue<float>() );
        const XmlTree scaleXml = plane.getChild("BackgroundPlane/Scale/Vector3");
        Vec3f scale = Vec3f(scaleXml["x"].getValue<float>(),
                            scaleXml["y"].getValue<float>(),
                            scaleXml["z"].getValue<float>());
        Vec3f offset = Vec3f(-m_dimensions/2);
        
        bool doubleSided = plane.getChild("BackgroundPlane")["doubleSided"].getValue() == "True";
        bool billboard = plane.getChild("BackgroundPlane")["billboard"].getValue() == "True";
        bool lightmap = plane.getChild("BackgroundPlane")["lightMap"].getValue() == "True";
        bool pixelatedLightmap = plane.getChild("BackgroundPlane")["pixelatedLightmap"].getValue() == "True";
        bool clampTexture = plane.getChild("BackgroundPlane")["clampTexture"].getValue() == "True";
        
        Vec2d repeat = Vec2d(false, false);
        if (pAnimXml)
        {
            repeat.x = plane.getChild("BackgroundPlane")["xTextureRepeat"].getValue() == "True";
            repeat.y = plane.getChild("BackgroundPlane")["yTextureRepeat"].getValue() == "True";
        }
        {
            lock_guard<mutex> lock( m_mutex );
            if (m_exit) { return; }
            m_backgroundPlanes.push_back(BackgroundPlane(pos, rot, scale, pAnimXml, offset, doubleSided, billboard,
                                                         lightmap, pixelatedLightmap, clampTexture, repeat,
                                                         backgroundPlanePng));
        }
    }
    console() << "Loaded " << numLevelBackgroundPlanes << " Background Planes" << endl;
    ostringstream displayString;
    displayString << "Finished Loading " << m_file.filename().string() <<
                     "  (" << numLevelTriles << " Triles, " << numLevelArtObjects << " Art Objects, " << numLevelBackgroundPlanes << " Background Planes)";
    if (m_verbose)
    {
        displayString << endl << getElapsedSeconds() - startTime << " Seconds";
    }
    setDisplayString(displayString.str());
}

void FezViewer::resize()
{
    CameraPersp cam(m_camera.getCamera());
    cam.setPerspective(60, getWindowAspectRatio(), 1, 1000);
    m_camera.setCurrentCam(cam);

    lock_guard<mutex> lock(m_mutex);
    m_pText->setSize(Vec2f(getWindowWidth(), TextBox::GROW));
}

void FezViewer::resetCamera(float zoom)
{
    CameraPersp initialCam;
    initialCam.setPerspective(60, getWindowAspectRatio(), 1, 1000);
    initialCam.lookAt(Vec3f(0, 0, zoom), Vec3f::zero());
    initialCam.setCenterOfInterestPoint(Vec3f::zero());
	m_camera.setCurrentCam( initialCam );
}

void FezViewer::mouseDown(MouseEvent event)
{
    m_camera.mouseDown(event.getPos());
}

void FezViewer::mouseDrag(MouseEvent event)
{
    m_camera.mouseDrag(event.getPos(),
                       event.isLeftDown() && !event.isAltDown(),
                       event.isMiddleDown() || event.isAltDown(),
                       event.isRightDown());
}

void FezViewer::mouseWheel(MouseEvent event)
{
    /*
    m_zoom -= event.getWheelIncrement();
    // TOOD: use clamp()
    if (m_zoom < 5)
    { m_zoom = 5; }
    if (m_zoom > 100)
    { m_zoom = 100; }
    */
}

void FezViewer::keyDown(KeyEvent event)
{
    if (event.getChar() == 'o')
    {
        spawnLoader(getOpenFilePath(getAppPath()));
    }
    if (event.getChar() == 'f')
    {
        setFullScreen(!isFullScreen());
        gl::enableAlphaBlending();
    }
    if (event.getChar() == KeyEvent::KEY_ESCAPE)
    {
        m_exit = true;
        if (m_thread)
        {
            m_thread->join();
            m_thread = nullptr;
        }
        setDisplayString("FezViewer by Michael Romero - www.halogenica.net - @halogenica");
        m_quit = true;
    }
}

void FezViewer::fileDrop(FileDropEvent event)
{
    spawnLoader(event.getFile(0));
}

void FezViewer::update()
{
    if (m_quit && m_textAlpha == 0.f)
    {
        quit();
    }
}

void FezViewer::draw()
{
    {
        lock_guard<mutex> lock( m_mutex );

        if (m_trileTexReload)
        {
            m_trileTexture = gl::Texture(m_trileSurface);
            m_trileTexture.setMinFilter(GL_NEAREST);
            m_trileTexture.setMagFilter(GL_NEAREST);
            Trile::s_pTexture = &m_trileTexture;
            m_trileTexReload = false;
        }
        
        if (m_textReload)
        {
            m_textTexture = gl::Texture(m_pText->render());
            m_textReload = false;
            timeline().clear();
            timeline().apply(&m_textAlpha, 1.f, 5.f);
            timeline().apply(&m_textAlpha, 0.f, 1.f, EaseOutExpo()).appendTo(&m_textAlpha);
        }
    }
    
    gl::clear(Color(0.2f, 0.2f, 0.3f));
    
    gl::pushModelView();
    
    const float scale = getWindow()->getContentScale();
    gl::setViewport(Area(0, 0, getWindowWidth() * scale, getWindowHeight() * scale));

    gl::setMatrices(m_camera.getCamera());

    gl::color(ColorA(1.0f, 1.0f, 1.0f, 1.0f));
    glFrontFace(GL_CW);
    gl::translate(Vec3f::zero());
    gl::rotate(Vec3f::zero());
    gl::scale(Vec3f::one());

    // TODO: Why do the dimensions of the level not bound the level?
    // gl::drawStrokedCube(Vec3f::zero(), m_dimensions/2);
    
    glEnable(GL_TEXTURE_2D);
    {
        lock_guard<mutex> lock( m_mutex );

        // Draw Triles
        for (Trile& trile : m_triles)
        {
            trile.Draw();
        }

        // Draw Art Objects
        for (ArtObject& ao : m_artObjects)
        {
            ao.Draw();
        }
        
        // Draw Background Plane
        for (BackgroundPlane& bp : m_backgroundPlanes)
        {
            bp.Draw(m_camera.getCamera());
        }
        
        gl::popModelView();
        
        gl::pushMatrices();
        gl::setMatricesWindow(getWindowSize());
        gl::disableDepthRead();
        gl::disableDepthWrite();
        gl::color(ColorA(1.f, 1.f, 1.f, m_textAlpha));
        glFrontFace(GL_CCW);
        
        gl::draw(m_textTexture, Vec2f(0, getWindowHeight() - m_textTexture.getHeight()));
        
        gl::enableDepthRead();
        gl::enableDepthWrite();
        gl::popMatrices();
    }
}

// This line tells Cinder to actually create the application
CINDER_APP_BASIC(FezViewer, RendererGl)