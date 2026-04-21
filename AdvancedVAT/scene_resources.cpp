#include "stdafx.h"
#include "types.h"
#include "texture.h"
#include "scene_resources.h"
#include "root_signature.h"
#include "shader.h"
#include <map>
#include <d3d12.h>

namespace {

// 文字列マップ管理より定数スロットの方が高速だが、この教材ではそこまで詰めない
std::map<std::string, SimpleShader_VS_PS *>		shaderSet;		// シェーダーを保持 (教材では vertex/pixel のみ扱う)
std::map<std::string, RootSignature *>			signatures;		// パイプライン毎のリソース設定
std::map<std::string, ID3D12PipelineState *>	pipelineStates;	// pipeline state は enum class DrawFunc に紐付いて生成する設計
std::map<std::string, ID3D12PipelineState *>	pipelineStatesDepth;
std::map<std::string, SimpleTexture *>			textures;		// テクスチャを保持

template<typename T> bool AddResource(const std::string &name, T *resource, std::map<std::string, T *> &holder)
{
	if(!name.empty() && resource != nullptr) {
		if(holder.find(name) == holder.end()) {
			holder[name] = resource;
			return true;
		}
	}
	return false;
}

template<typename T> T *GetResource(const std::string &name, std::map<std::string, T *> &holder)
{
	auto it = holder.find(name);
	if(it != holder.end()) {
		return (*it).second;
	}
	return nullptr;
}

} // unnamed

namespace Scene {

bool AddShader(const std::string &name, SimpleShader_VS_PS *shader)
{
	return AddResource(name, shader, shaderSet);
}

SimpleShader_VS_PS *GetShader(const std::string &name)
{
	return GetResource(name, shaderSet);
}

bool AddSignature(const std::string &name, RootSignature *signature)
{
	return AddResource(name, signature, signatures);
}

RootSignature *GetSignature(const std::string &name)
{
	return GetResource(name, signatures);
}

bool AddPso(const std::string &name, ID3D12PipelineState *pso)
{
	return AddResource(name, pso, pipelineStates);
}

ID3D12PipelineState *GetPso(const std::string &name)
{
	return GetResource(name, pipelineStates);
}

bool AddPsoDepth(const std::string &name, ID3D12PipelineState *pso)
{
	return AddResource(name, pso, pipelineStatesDepth);
}

ID3D12PipelineState *GetPsoDepth(const std::string &name)
{
	return GetResource(name, pipelineStatesDepth);
}

bool AddTexture(const std::string &name, SimpleTexture *texture)
{
	return AddResource(name, texture, textures);
}

SimpleTexture *GetTexture(const std::string &name)
{
	return GetResource(name, textures);
}

void ReleaseResources()
{
	for(auto &shaderSet : shaderSet) {
		delete shaderSet.second;
	}
	shaderSet.clear();

	for(auto &tex : textures) {
		delete tex.second;
	}
	textures.clear();

	for(auto &pso : pipelineStates) {
		SAFE_RELEASE(pso.second);
	}
	pipelineStates.clear();

	for(auto &pso : pipelineStatesDepth) {
		SAFE_RELEASE(pso.second);
	}
	pipelineStatesDepth.clear();

	for(auto &signature : signatures) {
		delete signature.second;
	}
	signatures.clear();
}

} // Scene
