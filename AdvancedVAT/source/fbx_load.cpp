#include "stdafx.h"
#include "fbx_load.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {

void LoadMesh(Mesh &dst, const aiMesh *src, bool inverseU, bool inverseV)
{
    const aiVector3D zero3D(0.0f, 0.0f, 0.0f);
    const aiColor4D zeroColor(0.0f, 0.0f, 0.0f, 0.0f);

    dst.Vertices.resize(src->mNumVertices);

    for(unsigned i = 0; i < src->mNumVertices; i++) {
        const aiVector3D &position = src->mVertices[i];
        const aiVector3D &normal = src->mNormals[i];
        aiVector3D uv = src->HasTextureCoords(0) ? src->mTextureCoords[0][i] : zero3D;
        const aiVector3D tangent = src->HasTangentsAndBitangents() ? src->mTangents[i] : zero3D;
        const aiColor4D &color = src->HasVertexColors(0) ? src->mColors[0][i] : zeroColor;

        if(inverseU) {
            uv.x = 1.0f - uv.x;
        }
        if(inverseV) {
            uv.y = 1.0f - uv.y;
        }

        Vertex vertex = {};
        vertex.Position = DirectX::XMFLOAT3(position.x, position.y, position.z);
        vertex.Normal = DirectX::XMFLOAT3(normal.x, normal.y, normal.z);
        vertex.UV = DirectX::XMFLOAT2(uv.x, uv.y);
        vertex.Tangent = DirectX::XMFLOAT3(tangent.x, tangent.y, tangent.z);
        vertex.Color = DirectX::XMFLOAT4(color.r, color.g, color.b, color.a);

        dst.Vertices[i] = vertex;
    }

    dst.Indices.resize(src->mNumFaces * 3);

    for(unsigned i = 0; i < src->mNumFaces; i++) {
        const aiFace& face = src->mFaces[i];

        dst.Indices[i * 3 + 0] = face.mIndices[0];
        dst.Indices[i * 3 + 1] = face.mIndices[1];
        dst.Indices[i * 3 + 2] = face.mIndices[2];
    }
}

}   // unnamed


namespace AssImport {

bool LoadFbx(ImportDesc &desc)
{
    if(desc.filename.empty()) {
        return false;
    }

    auto &meshes = desc.meshes;
    bool inverseU = desc.inverseU;
    bool inverseV = desc.inverseV;

    Assimp::Importer importer;
    int flag = 0;
    flag |= aiProcess_Triangulate;
    flag |= aiProcess_CalcTangentSpace;
    flag |= aiProcess_GenSmoothNormals;
    flag |= aiProcess_GenUVCoords;
    flag |= aiProcess_RemoveRedundantMaterials,
    flag |= aiProcess_MakeLeftHanded | aiProcess_FixInfacingNormals | aiProcess_FlipWindingOrder,
    flag |= aiProcess_LimitBoneWeights;

    const aiScene *scene = importer.ReadFile(desc.filename, flag);

    if(scene == nullptr) {
        desc.error = importer.GetErrorString();
        return false;
    }

    meshes.clear();
    meshes.resize(scene->mNumMeshes);

    for(size_t i = 0; i < meshes.size(); i++) {
        const auto pMesh = scene->mMeshes[i];
        LoadMesh(meshes[i], pMesh, inverseU, inverseV);
        const auto pMaterial = scene->mMaterials[i];
        //LoadTexture(desc.filename, meshes[i], pMaterial);
    }

    scene = nullptr;

    return true;
}

}   // AssImport