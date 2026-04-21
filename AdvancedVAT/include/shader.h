#pragma once
// C++17 以上で
#include <WTypesbase.h>
#include <string>
#include <d3d12.h>
#include <DirectXMath.h>
#include "types.h"

class Dx12Util;
class SimpleShader_VS_PS;
class SimpleCamera;

class Dx12Shader {
public:
	Dx12Shader(LPCWSTR szFileName, LPSTR szFuncName, LPSTR szProfileName);
	Dx12Shader(LPCWSTR csoFilePath);
	virtual ~Dx12Shader();

private:
	ID3DBlob		*m_code;
	std::wstring	m_path;
	std::string		m_error;
	HRESULT			m_state;

public:
	bool	IsValid() const;

	HRESULT	State() const {
		return m_state;
	}

	std::string	&Error() {
		return m_error;
	}

	D3D12_SHADER_BYTECODE ByteCode() const;
};


// 頂点シェーダー、ピクセルシェーダーのペア構成を保持
class SimpleShader_VS_PS {
public:
	static SimpleShader_VS_PS *Initialize(LPCWSTR vs_path, LPCWSTR ps_path);

	SimpleShader_VS_PS() noexcept;

	virtual ~SimpleShader_VS_PS();

	void Release();

	HRESULT InitializeInternal(LPCWSTR vs_path, LPCWSTR ps_path);

	inline Dx12Shader *VS() const { return vs; }
	inline Dx12Shader *PS() const { return ps; }

protected:
	HRESULT Result(Dx12Shader *p);

private:
	Dx12Shader			*vs, *ps;
	ID3D12PipelineState	*pso;

public:

};
