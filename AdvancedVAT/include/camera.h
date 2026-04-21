#pragma once
#include <cstdint>
#include <DirectXMath.h>
#include "types.h"

class SimpleCamera {
public:
	SimpleCamera();

	void MouseMove(int x, int y);

public:
	float3		eye, at, up;
	float		znear, zfar;
	float		fovy;	// degree
	uint16_t	operation;
	int			op_x, op_y;

public:
	void GetProjectionMatrixLH(uint32_t width, uint32_t height, float44 mtx) const;
	void GetProjectionMatrixLH(uint32_t width, uint32_t height, DirectX::XMMATRIX &mtx) const;

	void GetLookAtMatrix(float44 mtx);
	void GetLookAtMatrixLH(float44 mtx);
	void GetLookAtMatrix(DirectX::XMMATRIX &mtx);
	void GetLookAtMatrixLH(DirectX::XMMATRIX &mtx);

	static void GetProjectionMatrixLH(float fovy, float aspect, float znear, float zfar, float44 mtx);
	static void GetFrustumMatrix(float l, float r, float b, float t, float n, float f, float44 mtx);
	static void GetFrustumMatrixLH(float l, float r, float b, float t, float n, float f, float44 mtx);
	static void GetFrustumMatrixLHReversedZ(float l, float r, float b, float t, float n, float f, float44 mtx);

private:
	void	Identity(float44 m);
	void	MatrixRotationX(float44 m, float a);
	void	MatrixRotationY(float44 m, float a);
};
