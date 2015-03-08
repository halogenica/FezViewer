#pragma once

#include "Common.h"

#define TEX_EPSILON 0.005f  // offsets edges of sprite to prevent texture bleeding

const Vec3f c_positions[4] = { Vec3f( -0.5f,  0.5f,  0.f ),   // 0
                               Vec3f(  0.5f,  0.5f,  0.f ),   // 1
                               Vec3f(  0.5f, -0.5f,  0.f ),   // 2
                               Vec3f( -0.5f, -0.5f,  0.f ) }; // 3
    
const Vec2f c_texcoords[4] = { Vec2f( 0.f + TEX_EPSILON, 0.f + TEX_EPSILON ),    // 0
                               Vec2f( 1.f - TEX_EPSILON, 0.f + TEX_EPSILON ),    // 1
                               Vec2f( 1.f - TEX_EPSILON, 1.f - TEX_EPSILON ),    // 2
                               Vec2f( 0.f + TEX_EPSILON, 1.f - TEX_EPSILON ) };  // 3
    
const uint32_t c_indices[6] = { 0, 1, 2, 2, 3, 0 };

class BackgroundPlane
{
public:
   
    TriMesh m_mesh;
    Surface m_surface;
    gl::Texture* m_pTexture;
    deque<uint32_t> m_frames;
    deque<Vec2f> m_texIndices;
    uint32_t m_numFrames;
    Vec2f m_spriteScale;
    Vec2f m_packedScale;
    Vec2f m_packOffset;
    bool m_doubleSided;
    bool m_billboard;
    bool m_lightmap;
    bool m_pixelatedLightmap;
    bool m_clampTexture;
    Vec2d m_repeat;
    uint32_t m_totalDuration;
    Vec3f m_pos;
    Vec3f m_scale;
    Quatf m_rot;

    BackgroundPlane(const Vec3f& bpPos,
                    const Quatf& bpRot,
                    const Vec3f& bpScale,
                    XmlTree const * pAnimXml,
                    const Vec3f& offset,
                    const bool doubleSided,
                    const bool billboard,
                    const bool lightmap,
                    const bool pixelatedLightmap,
                    const bool clampTexture,
                    const Vec2d& repeat,
                    const fs::path& surfPng) :
        m_numFrames(1),
        m_spriteScale(Vec2f(1.f,1.f)),
        m_packedScale(Vec2f(1.f,1.f)),
        m_packOffset(Vec2f(0.f,0.f)),
        m_doubleSided(doubleSided),
        m_billboard(billboard),
        m_lightmap(lightmap),
        m_pixelatedLightmap(pixelatedLightmap),
        m_clampTexture(clampTexture),
        m_repeat(repeat),
        m_totalDuration(0)
    {
        vector<Vec3f> positions;
        vector<Vec3f> normals;
        vector<Vec2f> texcoords;
        const auto image = loadImage(surfPng);
        Vec2f bpDim;
        Vec2f bpActualDim;
        Vec2f imageDim;
        
        imageDim.x = image->getWidth();
        imageDim.y = image->getHeight();
        
        if (pAnimXml)
        {
            if (pAnimXml->hasChild("AnimatedTexture"))
            {
                // The XBOX content format
                // Animation frames are always laid out vertically, a padded pow2 apart (bpDim)

                // bpDim represents the pow2 dimensions of a single sprite
                bpDim.x = pAnimXml->getChild("AnimatedTexture")["width"].getValue<float>();
                bpDim.y = pAnimXml->getChild("AnimatedTexture")["height"].getValue<float>();
                // bpActualDim represents the actual (non-pow2) width/height of a single sprite
                bpActualDim.x = pAnimXml->getChild("AnimatedTexture")["actualWidth"].getValue<float>();
                bpActualDim.y = pAnimXml->getChild("AnimatedTexture")["actualHeight"].getValue<float>();

                m_spriteScale = bpDim / imageDim;
                m_packedScale = bpActualDim/bpDim;
                m_packOffset = ((Vec2f(1.f,1.f) - m_packedScale) / 2) / m_packedScale;

                uint32_t i = 0;
                for (const auto& frame : pAnimXml->getChild("AnimatedTexture/Frames"))
                {
                    const uint32_t frameTime = frame["duration"].getValue<uint32_t>();
                    m_frames.push_back(frameTime);
                    m_totalDuration += frameTime;
                    m_texIndices.push_back(Vec2f(0, i));
                    ++i;
                }
            }
            else
            {
                // The PC content format
                // Animation frames are tiled in X and Y (indexed by the "Rectangle" attribute)
                // The frames are tightly packed the actual (non-pow2) width/height apart (bpActualDim)
                // The entire sprite sheet is finally padded to have a pow2 dimensions (bpDim)

                // bpDim represents the pow2 dimensions of the entire sprite sheet
                bpDim.x = pAnimXml->getChild("AnimatedTexturePC")["width"].getValue<float>();
                bpDim.y = pAnimXml->getChild("AnimatedTexturePC")["height"].getValue<float>();
                ASSERT(bpDim.x == imageDim.x);
                ASSERT(bpDim.y == imageDim.y);
                // bpActualDim represents the actual (non-pow2) width/height of a single sprite
                bpActualDim.x = pAnimXml->getChild("AnimatedTexturePC")["actualWidth"].getValue<float>();
                bpActualDim.y = pAnimXml->getChild("AnimatedTexturePC")["actualHeight"].getValue<float>();
 
                m_spriteScale = bpActualDim / imageDim;

                for (const auto& frame : pAnimXml->getChild("AnimatedTexturePC/Frames"))
                {
                    const uint32_t frameTime = frame["duration"].getValue<uint32_t>();
                    m_frames.push_back(frameTime);
                    m_totalDuration += frameTime;

                    ASSERT(frame.getChild("Rectangle")["w"].getValue<uint32_t>() == bpActualDim.x);
                    ASSERT(frame.getChild("Rectangle")["h"].getValue<uint32_t>() == bpActualDim.y);

                    const uint32_t x = frame.getChild("Rectangle")["x"].getValue<uint32_t>();
                    const uint32_t y = frame.getChild("Rectangle")["y"].getValue<uint32_t>();
                    const Vec2f frameIdx = Vec2f(x, y); 
                    m_texIndices.push_back(frameIdx / bpActualDim);
                }
            }

            m_numFrames = m_frames.size();
        }
        else
        {
            bpDim = imageDim;
            bpActualDim = imageDim;
            m_texIndices.push_back(Vec2f(0.f, 0.f));
        }
        
        Vec3f norm = Vec3f(0, 0, 1) * bpRot;
        Vec3f extraOffset = Vec3f(-0.5f, -0.5f, -0.5f);    // TODO: why is this offset needed?
        m_pos = bpPos + offset + norm * 0.0005f + extraOffset;
        m_scale = bpScale * Vec3f(bpActualDim.x / 16, bpActualDim.y / 16, 1.f);
        m_rot = bpRot;

        for (uint32_t i = 0; i < 4; i++)
        {
            normals.push_back(norm);
            
            Vec3f pos = c_positions[i];
            positions.push_back(pos);
            
            Vec2f texcoord = c_texcoords[i];
            if (m_repeat.x || m_clampTexture)
            {
                texcoord.x *= bpScale.x;
            }
            if (m_repeat.y || m_clampTexture)
            {
                texcoord.y *= bpScale.y;
            }
            texcoords.push_back(texcoord);
        }

        m_mesh.appendVertices(&positions[0], 4);
        m_mesh.appendNormals(&normals[0], 4);
        m_mesh.appendTexCoords(&texcoords[0], 4);
        m_mesh.appendIndices(&c_indices[0], 6);
                
        m_surface = loadImage(surfPng);
        m_pTexture = nullptr;
    }
    
    ~BackgroundPlane()
    {
        delete m_pTexture;
    }

    void Draw(const CameraPersp& camera)
    {
        if (!m_pTexture)
        {
            m_pTexture = new gl::Texture(m_surface);

            if (m_lightmap && !m_pixelatedLightmap)
            {
                m_pTexture->setMinFilter(GL_LINEAR);
                m_pTexture->setMagFilter(GL_LINEAR);
            }
            else
            {
                m_pTexture->setMinFilter(GL_NEAREST);
                m_pTexture->setMagFilter(GL_NEAREST);
            }

            if (m_repeat.x)
            {
                m_pTexture->setWrapS(GL_REPEAT);
            }
            else
            {
                m_pTexture->setWrapS(GL_CLAMP);
            }

            if (m_repeat.y)
            {
                m_pTexture->setWrapT(GL_REPEAT);
            }
            else
            {
                m_pTexture->setWrapT(GL_CLAMP);
            }
        }
        else
        {
            uint32_t index = 0;
            
            if (m_frames.size() > 0 && m_totalDuration > 0)
            {
                uint32_t timeOffset = (uint32_t)(getElapsedSeconds() * 10000000) % m_totalDuration;
                ASSERT(timeOffset < m_totalDuration);
                
                while (timeOffset > m_frames[index])
                {
                    timeOffset -= m_frames[index];
                    index++;
                    ASSERT(index < m_frames.size());
                }
            }
            
            m_pTexture->enableAndBind();
            
            if (m_doubleSided)
            {
                glDisable(GL_CULL_FACE);
            }
            if (m_lightmap)
            {
                glBlendFunc(GL_ONE, GL_ONE);
            }

            glMatrixMode(GL_TEXTURE);
            glPushMatrix();
            gl::scale(m_spriteScale * m_packedScale);
            gl::translate(m_packOffset + m_texIndices[index] + 2 * m_packOffset * m_texIndices[index]);

            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();

            if (m_billboard)
            {
                Vec3f mRight, mUp;
                camera.getBillboardVectors(&mRight, &mUp);
                float angleRad = ci::math<float>::acos(mRight.dot(Vec3f(1.f, 0.f, 0.f)));
                float angleDeg = angleRad * 180.0 / M_PI;
                angleDeg *= (mRight.z > 0 ? -1 : 1);    // get full 360 degree (signed) rotation
                gl::translate(m_pos);
                gl::rotate(Vec3f(0.f, angleDeg, 0.f));
                gl::scale(m_scale);
                gl::draw(m_mesh);
            }
            else
            {
                gl::translate(m_pos);
                gl::rotate(m_rot);
                gl::scale(m_scale);
                gl::draw(m_mesh);
            }

            glMatrixMode(GL_TEXTURE);
            glPopMatrix();

            glMatrixMode(GL_MODELVIEW);
            glPopMatrix();

            if (m_doubleSided)
            {
                glEnable(GL_CULL_FACE);
            }
            if (m_lightmap)
            {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }

            m_pTexture->disable();
            m_pTexture->unbind();
        }
    }
};