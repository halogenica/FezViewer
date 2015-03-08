#pragma once

#include "Common.h"

class ArtObject
{
public:
    
    TriMesh m_mesh;
    Surface m_surface;
    gl::Texture* m_pTexture;

    ArtObject(const XmlTree& ao, const Vec3f& aoPos, const Quatf& aoRot, const Vec3f& aoScale, const Vec3f& offset, const fs::path& surfPng)
    {
        vector<Vec3f> positions;
        vector<Vec3f> normals;
        vector<Vec2f> texcoords;
        vector<uint32_t> indices;
        const XmlTree& xmlVertices = ao.getChild("ArtObject/ShaderInstancedIndexedPrimitives/Vertices");
        
        for (auto vertex : xmlVertices)
        {
            const XmlTree& posXml = vertex.getChild("Position/Vector3");
            Vec3f pos = Vec3f(posXml["x"].getValue<float>(),
                              posXml["y"].getValue<float>(),
                              posXml["z"].getValue<float>());
            pos = pos * aoRot;
            pos *= aoScale;
            pos += aoPos + offset;
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
        
        const XmlTree& xmlIndices = ao.getChild("ArtObject/ShaderInstancedIndexedPrimitives/Indices");
        for (auto index : xmlIndices)
        {
            indices.push_back(index.getValue<uint32_t>());
        }
        m_mesh.appendIndices(&indices[0], indices.size());
        
        m_surface = loadImage(surfPng);
        m_pTexture = nullptr;
    }
    
    ArtObject(const TriMesh& mesh, const Surface& surf)
    {
        m_mesh = mesh;
        m_surface = surf;
        m_pTexture = nullptr;
    }
    
    ~ArtObject()
    {
        delete m_pTexture;
    }

    void Draw()
    {
        if (!m_pTexture)
        {
            m_pTexture = new gl::Texture(m_surface);
            m_pTexture->setMinFilter(GL_NEAREST);
            m_pTexture->setMagFilter(GL_NEAREST);
        }
        else
        {
            m_pTexture->enableAndBind();
            gl::draw(m_mesh);
            m_pTexture->disable();
            m_pTexture->unbind();
        }
    }
};