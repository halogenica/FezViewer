#include "cinder/app/AppBasic.h"
#include "cinder/Camera.h"
#include "cinder/MayaCamUI.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "cinder/TriMesh.h"
#include "cinder/Xml.h"
#include "cinder/Filesystem.h"
#include "cinder/ImageIo.h"
#include "cinder/Text.h"
#include "cinder/Timeline.h"
#include "boost/algorithm/String.hpp"
#include <list>
#include <map>
#include <vector>
#include "Resources.h"

using namespace ci;
using namespace ci::app;
using namespace std;

const Vec3f gc_normals[] = { Vec3f(-1., 0., 0.), Vec3f( 0., -1., 0.), Vec3f(0., 0., -1.),
                             Vec3f( 1., 0., 0.), Vec3f( 0.,  1., 0.), Vec3f(0., 0.,  1.) };
const Quatf gc_orientations[] = { Quatf(0, M_PI, 0), Quatf(0, -M_PI/2, 0), Quatf(0, 0, 0), Quatf(0, M_PI/2, 0) };

struct RenderObject
{
    TriMesh     mesh;
    uint32_t    textureIndex;
};

class FezViewer : public AppBasic
{
  public:
    void prepareSettings(Settings* pSettings);
    void setup();
    void shutdown();
    void setDisplayString(string str);
    void spawnLoader(fs::path file);
    void loadArtObject();
    void loadLevel();
    void loadTrile(XmlTree trile, Vec3f trilePos, uint32_t trileOrient, Vec3f trileEmplacement);
    void loadAO(XmlTree ao, Vec3f aoPos, Quatf aoRot, Vec3f aoScale, uint32_t texIndex);
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
    vector<RenderObject>    m_renderObjects;
    vector<Surface>         m_surfaces;
    vector<gl::Texture>     m_textures;
    fs::path                m_file;
    bool                    m_verbose;
    Vec3f                   m_dimensions;
    shared_ptr<thread>      m_thread;
    mutex                   m_mutex;
    bool                    m_exit;
    bool                    m_quit;
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
    m_verbose = false;
    m_dimensions = Vec3f::zero();
    m_thread = nullptr;
    m_exit = false;
    m_quit = false;

    m_pText = new TextBox();
    m_pText->setFont(Font(app::loadResource(RES_MY_FONT), 30));
    m_pText->setColor(ci::Colorf(1.f, 1.f, 1.f));
    m_pText->setAlignment(TextBox::LEFT);
    m_pText->setSize(Vec2f(getWindowWidth(), TextBox::GROW));
    m_pText->setText("FezViewer v0.1 \nPress 'O' or drag and drop file to open");
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
    
    gl::Texture texture = gl::Texture(surf);
    texture.setMinFilter(GL_NEAREST);
    texture.setMagFilter(GL_NEAREST);
    m_textures.push_back(texture);
    
    RenderObject ro;
    ro.mesh = mesh;
    ro.textureIndex = 0;
    m_renderObjects.push_back(ro);
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

void FezViewer::setDisplayString(string str)
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
    m_renderObjects.clear();
    m_surfaces.clear();
    m_textures.clear();
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
    XmlTree aoXml = XmlTree(loadFile(artObjectXml));
    m_dimensions = Vec3f(aoXml.getChild("ArtObject/Size/Vector3")["x"].getValue<float>(),
                         aoXml.getChild("ArtObject/Size/Vector3")["y"].getValue<float>(),
                         aoXml.getChild("ArtObject/Size/Vector3")["z"].getValue<float>());

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
    
    {
        lock_guard<mutex> lock( m_mutex );
        if (m_exit) { return; }
        m_surfaces.push_back(loadImage(artObjectPng));
    }
    
    Vec3f pos = Vec3f(0.5, 0.5, 0.5) + m_dimensions/2;  // counteract offset from level loader
    Quatf rot = Quatf(0,0,0);
    Vec3f scale = Vec3f(1.f, 1.f, 1.f);
    {
        lock_guard<mutex> lock( m_mutex );
        if (m_exit) { return; }
        loadAO(aoXml, pos, rot, scale, 0);
    }
    
    ostringstream displayString;
    displayString << "Finished Loading " << m_file.filename().string();
    setDisplayString(displayString.str());
}

void FezViewer::loadLevel()
{
    ci::ThreadSetup threadSetup; // Required for cinder multithreading
    
    // TODO: path.preferred_separator doesn't work on windows
    const auto sep = '/';
    
    // Load the level data
    console() << "Loading Level: " << m_file.string() << endl;
    XmlTree level = XmlTree(loadFile(m_file));
    XmlTree lookAtXml = level.getChild("Level/Size/Vector3");
    m_dimensions = Vec3f(lookAtXml["x"].getValue<float>(),
                         lookAtXml["y"].getValue<float>(),
                         lookAtXml["z"].getValue<float>());
    
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
    
    {
        lock_guard<mutex> lock( m_mutex );
        if (m_exit) { return; }
        m_surfaces.push_back(loadImage(trileSetPng));
    }
    
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
    XmlTree trileSet = XmlTree(loadFile(trileSetXml));
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
    XmlTree levelTriles = level.getChild("Level/Triles");
    int numLevelTriles = 0;
    for (const auto& trile : levelTriles)
    {
        XmlTree emplacementXml = trile.getChild("TrileEmplacement");
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
                XmlTree posXml = trile.getChild("TrileInstance/Position/Vector3");
                Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                                  posXml["y"].getValue<float>(),
                                  posXml["z"].getValue<float>());
                int orient = trile.getChild("TrileInstance")["orientation"].getValue<int>();
                
                {
                    lock_guard<mutex> lock( m_mutex );
                    if (m_exit) { return; }
                    loadTrile(trileMap[tid], pos, orient, emplacement);
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
                XmlTree posXml = trile.getChild("TrileInstance/OverlappedTriles/TrileInstance/Position/Vector3");
                Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                                  posXml["y"].getValue<float>(),
                                  posXml["z"].getValue<float>());
                int orient = trile.getChild("TrileInstance/OverlappedTriles/TrileInstance")["orientation"].getValue<int>();
                
                {
                    lock_guard<mutex> lock( m_mutex );
                    if (m_exit) { return; }
                    loadTrile(trileMap[tid], pos, orient, emplacement);
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
    XmlTree levelArtObjects = level.getChild("Level/ArtObjects");
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
        XmlTree aoXml = XmlTree(loadFile(artObjectXml));
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
        
        {
            lock_guard<mutex> lock( m_mutex );
            if (m_exit) { return; }
            m_surfaces.push_back(loadImage(artObjectPng));
        }
        
        XmlTree posXml = object.getChild("ArtObjectInstance/Position/Vector3");
        Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                          posXml["y"].getValue<float>(),
                          posXml["z"].getValue<float>());
        XmlTree rotXml = object.getChild("ArtObjectInstance/Rotation/Quaternion");
        Quatf rot = Quatf(rotXml["w"].getValue<float>(),
                          rotXml["x"].getValue<float>(),
                          rotXml["y"].getValue<float>(),
                          rotXml["z"].getValue<float>() );
        XmlTree scaleXml = object.getChild("ArtObjectInstance/Scale/Vector3");
        Vec3f scale = Vec3f(scaleXml["x"].getValue<float>(),
                            scaleXml["y"].getValue<float>(),
                            scaleXml["z"].getValue<float>());
        {
            lock_guard<mutex> lock( m_mutex );
            if (m_exit) { return; }
            loadAO(aoXml, pos, rot, scale, numLevelArtObjects);
        }
    }
    console() << "Loaded " << numLevelArtObjects << " Art Objects" << endl;

    ostringstream displayString;
    displayString << "Finished Loading " << m_file.filename().string() <<
                     "  (" << numLevelTriles << " Triles, " << numLevelArtObjects << " Art Objects)";
    setDisplayString(displayString.str());
}

void FezViewer::loadTrile(XmlTree trile, Vec3f trilePos, uint32_t trileOrient, Vec3f trileEmplacement)
{
    TriMesh mesh;
    vector<Vec3f> positions;
    vector<Vec3f> normals;
    vector<Vec2f> texcoords;
    vector<uint32_t> indices;
    XmlTree xmlVertices = trile.getChild("Trile/Geometry/ShaderInstancedIndexedPrimitives/Vertices");
    
    for (auto vertex : xmlVertices)
    {
        XmlTree posXml = vertex.getChild("Position/Vector3");
        Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                          posXml["y"].getValue<float>(),
                          posXml["z"].getValue<float>());
        // TODO: how to use trileEmplacement?
        pos = pos * gc_orientations[trileOrient];
        pos += trilePos - (m_dimensions/2);
        positions.push_back(pos);
        
        XmlTree normXml = vertex.getChild("Normal");
        normals.push_back(gc_normals[normXml.getValue<int>()]);
        
        XmlTree coordXml = vertex.getChild("TextureCoord/Vector2");
        texcoords.push_back(Vec2f(coordXml["x"].getValue<float>(),
                                  coordXml["y"].getValue<float>()));
    }

    mesh.appendVertices(&positions[0], positions.size());
    mesh.appendNormals(&normals[0], normals.size());
    mesh.appendTexCoords(&texcoords[0], texcoords.size());
    
    XmlTree xmlIndices = trile.getChild("Trile/Geometry/ShaderInstancedIndexedPrimitives/Indices");
    for (auto index : xmlIndices)
    {
        indices.push_back(index.getValue<uint32_t>());
    }
    mesh.appendIndices(&indices[0], indices.size());
    
    RenderObject ro;
    ro.mesh = mesh;
    ro.textureIndex = 0;
    m_renderObjects.push_back(ro);
}

void FezViewer::loadAO(XmlTree ao, Vec3f aoPos, Quatf aoRot, Vec3f aoScale, uint32_t texIndex)
{
    TriMesh mesh;
    vector<Vec3f> positions;
    vector<Vec3f> normals;
    vector<Vec2f> texcoords;
    vector<uint32_t> indices;
    XmlTree xmlVertices = ao.getChild("ArtObject/ShaderInstancedIndexedPrimitives/Vertices");
    
    for (auto vertex : xmlVertices)
    {
        XmlTree posXml = vertex.getChild("Position/Vector3");
        Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                          posXml["y"].getValue<float>(),
                          posXml["z"].getValue<float>());
        pos = pos * aoRot;
        pos *= aoScale;
        pos += aoPos - (m_dimensions/2);
        pos -= Vec3f(0.5, 0.5, 0.5);    // TODO: why is this offset needed?
        positions.push_back(pos);
        
        XmlTree normXml = vertex.getChild("Normal");
        normals.push_back(gc_normals[normXml.getValue<int>()]);
        
        XmlTree coordXml = vertex.getChild("TextureCoord/Vector2");
        texcoords.push_back(Vec2f(coordXml["x"].getValue<float>(),
                                  coordXml["y"].getValue<float>()));
    }
    
    mesh.appendVertices(&positions[0], positions.size());
    mesh.appendNormals(&normals[0], normals.size());
    mesh.appendTexCoords(&texcoords[0], texcoords.size());
    
    XmlTree xmlIndices = ao.getChild("ArtObject/ShaderInstancedIndexedPrimitives/Indices");
    for (auto index : xmlIndices)
    {
        indices.push_back(index.getValue<uint32_t>());
    }
    mesh.appendIndices(&indices[0], indices.size());
    
    RenderObject ro;
    ro.mesh = mesh;
    ro.textureIndex = texIndex;
    m_renderObjects.push_back(ro);
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
    }
    if (event.getChar() == KeyEvent::KEY_ESCAPE)
    {
        m_exit = true;
        if (m_thread)
        {
            m_thread->join();
            m_thread = nullptr;
        }
        setDisplayString("FezViewer by Michael Romero - www.halogenica.net");
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
        if (m_textReload)
        {
            m_textTexture = gl::Texture(m_pText->render());
            m_textReload = false;
            timeline().clear();
            timeline().apply(&m_textAlpha, 1.f, 5.f);
            timeline().apply(&m_textAlpha, 0.f, 1.f, EaseOutExpo()).appendTo(&m_textAlpha);
        }
        for (auto& surf : m_surfaces)
        {
            gl::Texture trileTexture = gl::Texture(surf);
            trileTexture.setMinFilter(GL_NEAREST);
            trileTexture.setMagFilter(GL_NEAREST);
            m_textures.push_back(trileTexture);
        }
        m_surfaces.clear();
    }
    
    gl::clear(Color(0.2f, 0.2f, 0.3f));
    
    gl::pushModelView();
    
    const float scale = getWindow()->getContentScale();
    gl::setViewport(Area(0, 0, getWindowWidth() * scale, getWindowHeight() * scale));

    gl::setMatrices(m_camera.getCamera());

    glCullFace(GL_NONE);
    gl::color(ColorA(1.0f, 1.0f, 1.0f, 1.0f));
    gl::translate(Vec3f::zero());
    gl::rotate(Vec3f::zero());
    gl::scale(Vec3f::one());

    // TODO: Why do the dimensions of the level not bound the level?
    // gl::drawStrokedCube(Vec3f::zero(), m_dimensions/2);
    
    glEnable(GL_TEXTURE_2D);
    {
        lock_guard<mutex> lock( m_mutex );
        for (const auto& ro : m_renderObjects)
        {
            if (m_textures.size() > ro.textureIndex)
            {
                m_textures[ro.textureIndex].enableAndBind();
                gl::draw(ro.mesh);
                m_textures[ro.textureIndex].disable();
                m_textures[ro.textureIndex].unbind();
            }
        }
        
        gl::popModelView();
        
        gl::pushMatrices();
        gl::setMatricesWindow(getWindowSize());
        gl::disableDepthRead();
        gl::disableDepthWrite();
        gl::enableAlphaBlending();
        gl::color(ColorA(1.f, 1.f, 1.f, m_textAlpha));
        
        gl::draw(m_textTexture, Vec2f(0, getWindowHeight() - m_textTexture.getHeight()));
        
        gl::disableAlphaBlending();
        gl::enableDepthRead();
        gl::enableDepthWrite();
        gl::popMatrices();
    }
}

// This line tells Cinder to actually create the application
CINDER_APP_BASIC(FezViewer, RendererGl)