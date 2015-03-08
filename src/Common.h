#pragma once

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
#include <deque>
#include "Resources.h"

using namespace ci;
using namespace ci::app;
using namespace std;

// Macros
#ifdef DEBUG
    #define ASSERT(x) {if (!(x)) {abort();}} // could also use abort()
#else
    #define ASSERT(x)
#endif

// Globals
const Vec3f gc_normals[] = { Vec3f(-1.f, 0.f, 0.f), Vec3f( 0.f, -1.f, 0.f), Vec3f(0.f, 0.f, -1.f),
                             Vec3f( 1.f, 0.f, 0.f), Vec3f( 0.f,  1.f, 0.f), Vec3f(0.f, 0.f,  1.f) };
const Quatf gc_orientations[] = { Quatf(0, M_PI, 0), Quatf(0, -M_PI/2, 0), Quatf(0, 0, 0), Quatf(0, M_PI/2, 0) };

// Structs
struct RenderObject
{
    TriMesh     mesh;
    uint32_t    textureIndex;
};