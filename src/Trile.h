#pragma once

#include "Common.h"

class Trile
{
public:
    
    TriMesh m_mesh;
    static gl::Texture* s_pTexture;

    Trile(const XmlTree& trileXml, const Vec3f& trilePos, const uint32_t trileOrient, const Vec3f& trileEmplacement, const Vec3f& offset)
    {
        vector<Vec3f> positions;
        vector<Vec3f> normals;
        vector<Vec2f> texcoords;
        vector<uint32_t> indices;
        XmlTree xmlVertices = trileXml.getChild("Trile/Geometry/ShaderInstancedIndexedPrimitives/Vertices");
    
        for (const auto& vertex : xmlVertices)
        {
            XmlTree posXml = vertex.getChild("Position/Vector3");
            Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                              posXml["y"].getValue<float>(),
                              posXml["z"].getValue<float>());
            // TODO: how to use trileEmplacement?
            pos = pos * gc_orientations[trileOrient];
            pos += trilePos + offset;
            positions.push_back(pos);
        
            const XmlTree& normXml = vertex.getChild("Normal");
            normals.push_back(gc_normals[normXml.getValue<int>()]);
        
            const XmlTree& coordXml = vertex.getChild("TextureCoord/Vector2");
            texcoords.push_back(Vec2f(coordXml["x"].getValue<float>(),
                                      coordXml["y"].getValue<float>()));
        }

        m_mesh.appendVertices(&positions[0], positions.size());
        m_mesh.appendNormals(&normals[0], normals.size());
        m_mesh.appendTexCoords(&texcoords[0], texcoords.size());
    
        const XmlTree& xmlIndices = trileXml.getChild("Trile/Geometry/ShaderInstancedIndexedPrimitives/Indices");
        for (const auto& index : xmlIndices)
        {
            indices.push_back(index.getValue<uint32_t>());
        }
        m_mesh.appendIndices(&indices[0], indices.size());
    }
    
    ~Trile()
    {

    }

    void Draw()
    {
        if (s_pTexture)
        {
            s_pTexture->enableAndBind();
            gl::draw(m_mesh);
            s_pTexture->disable();
            s_pTexture->unbind();
        }
    }
};