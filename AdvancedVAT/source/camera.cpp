#include "stdafx.h"
#include "camera.h"
#include "dx12_util.h"
#include <WinUser.h>

namespace {

void cross(float3 v1, float3 v2, float3 result)
{
	result[0] = v1[1] * v2[2] - v1[2] * v2[1];
	result[1] = v1[2] * v2[0] - v1[0] * v2[2];
	result[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

float dot(float3 a, float3 b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void normalize(float3 v)
{
	float r = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if(r > 0.00001f) {
		r = 1.0f / r;
		v[0] *= r;
		v[1] *= r;
		v[2] *= r;
	}
}

void rot_(const float44 m, const float3 v, float3 result)
{
	result[0] = m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2];
	result[1] = m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2];
	result[2] = m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2];
}

void rev_(const float44 m, const float3 v, float3 result)
{
	result[0] = m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2];
	result[1] = m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2];
	result[2] = m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2];
}

void mul33(const float44 m0, const float44 m1, float44 m)
{
	m[0][0] = (m0[0][0] * m1[0][0]) + (m0[0][1] * m1[1][0]) + (m0[0][2] * m1[2][0]);
	m[0][1] = (m0[0][0] * m1[0][1]) + (m0[0][1] * m1[1][1]) + (m0[0][2] * m1[2][1]);
	m[0][2] = (m0[0][0] * m1[0][2]) + (m0[0][1] * m1[1][2]) + (m0[0][2] * m1[2][2]);
	m[1][0] = (m0[1][0] * m1[0][0]) + (m0[1][1] * m1[1][0]) + (m0[1][2] * m1[2][0]);
	m[1][1] = (m0[1][0] * m1[0][1]) + (m0[1][1] * m1[1][1]) + (m0[1][2] * m1[2][1]);
	m[1][2] = (m0[1][0] * m1[0][2]) + (m0[1][1] * m1[1][2]) + (m0[1][2] * m1[2][2]);
	m[2][0] = (m0[2][0] * m1[0][0]) + (m0[2][1] * m1[1][0]) + (m0[2][2] * m1[2][0]);
	m[2][1] = (m0[2][0] * m1[0][1]) + (m0[2][1] * m1[1][1]) + (m0[2][2] * m1[2][1]);
	m[2][2] = (m0[2][0] * m1[0][2]) + (m0[2][1] * m1[1][2]) + (m0[2][2] * m1[2][2]);
}

}


SimpleCamera::SimpleCamera()
	: znear(0.01f)
	, zfar(1000.0f)
	, fovy(35.0f)
	, operation(0)
	, op_x(-1)
	, op_y(-1)
{
	eye[0] = eye[1] = 0;
	eye[2] = 10;
	at[0] = at[1] = at[2] = 0;
	up[0] = 0;
	up[1] = 1;
	up[2] = 0;
}


void SimpleCamera::MouseMove(int x, int y)
{
	const float radian_scale = 0.01f;
	const float track_scale = 0.002f;

	if(operation == WM_LBUTTONDOWN) {
		// 回転
		float lx = static_cast<float>(x - op_x);
		float ly = static_cast<float>(y - op_y);

		float xa = lx * radian_scale;
		float ya = ly * radian_scale;

		float44	m, xm, ym, modelview;

		MatrixRotationX(xm, ya);
		MatrixRotationY(ym, xa);
		Identity(m);
		mul33(xm, ym, m);

		GetLookAtMatrixLH(modelview);

		float3 v, pt = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
		rot_(modelview, pt, v);
		rev_(m, v, pt);
		rev_(modelview, pt, eye);

		eye[0] += at[0];
		eye[1] += at[1];
		eye[2] += at[2];

		rot_(modelview, up, v);
		rev_(m, v, pt);
		rev_(modelview, pt, up);
	} else if(operation == WM_MBUTTONDOWN) {
		// 上下左右
		float lx = static_cast<float>(op_x - x);
		float ly = static_cast<float>(op_y - y);

		float44 modelview;
		GetLookAtMatrixLH(modelview);

		float3 pt, v = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
		float t = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
		if(t > 0.00001f) t = sqrtf(t);
		// 画面サイズによって変わるので調整する？
		float xa = lx * track_scale;
		float ya = ly * track_scale;
		float x0 = -sinf(xa) * t;
		float y0 = t - cosf(xa) * t;
		float x1 = -sinf(ya) * t;
		float y1 = t - cosf(ya) * t;

		float x_ = x0 * x0 + y0 * y0;
		float y_ = x1 * x1 + y1 * y1;
		if(x_ > 0.00001f) x_ = sqrtf(x_);
		if(y_ > 0.00001f) y_ = sqrtf(y_);
		if(lx < 0) x_ = -x_;
		if(ly < 0) y_ = -y_;

		rot_(modelview, eye, pt);
		pt[0] += x_;
		pt[1] -= y_;
		rev_(modelview, pt, eye);

		rot_(modelview, at, pt);
		pt[0] += x_;
		pt[1] -= y_;
		rev_(modelview, pt, at);
	} else if(operation == WM_RBUTTONDOWN) {
		// 
		float xa = static_cast<float>(op_x - x);
		float ya = static_cast<float>(op_y - y);

		float ry = 1.0f;
		ry *= xa > 0.0f ? powf(0.99f, -xa) : powf(1.01f, xa);
		ry *= ya > 0.0f ? powf(0.99f, -ya) : powf(1.01f, ya);

		float3 vec = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
		float dist = vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2];
		if(ry >= 1 || (dist > 0.001f && sqrtf(dist) > (znear * 2.0f))) {
			float3 v = {vec[0] * ry, vec[1] * ry, vec[2] * ry};
			eye[0] = v[0] + at[0];
			eye[1] = v[1] + at[1];
			eye[2] = v[2] + at[2];
		}
	}

	op_x = x;
	op_y = y;
}


void SimpleCamera::Identity(float44 m)
{
	m[0][0] = 1;
	m[0][1] = 0;
	m[0][2] = 0;
	m[0][3] = 0;
	m[1][0] = 0;
	m[1][1] = 1;
	m[1][2] = 0;
	m[1][3] = 0;
	m[2][0] = 0;
	m[2][1] = 0;
	m[2][2] = 1;
	m[2][3] = 0;
	m[3][0] = 0;
	m[3][1] = 0;
	m[3][2] = 0;
	m[3][3] = 1;
}

void SimpleCamera::MatrixRotationX(float44 m, float a)
{
	Identity(m);
	m[1][1] = m[2][2] = cosf(a);
	m[1][2] = -sinf(a);
	m[2][1] = -m[1][2];
}

void SimpleCamera::MatrixRotationY(float44 m, float a)
{
	Identity(m);
	m[0][0] = m[2][2] = cosf(a);
	m[0][2] = sinf(a);
	m[2][0] = -m[0][2];
}


void SimpleCamera::GetLookAtMatrix(float44 m)
{
	float3 X, Y, Z = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
	float3 T = {-eye[0], -eye[1], -eye[2]};

	cross(up, Z, X);
	cross(Z, X, Y);

	normalize(X);
	normalize(Y);
	normalize(Z);

	/*
	M[0]=X[0];  M[4]=X[1];  M[8]=X[2];  M[12]= X*T;
	M[1]=Y[0];  M[5]=Y[1];  M[9]=Y[2];  M[13]= Y*T;
	M[2]=Z[0];  M[6]=Z[1];  M[10]=Z[2]; M[14]= Z*T;
	M[3]=0;     M[7]=0;     M[11]=0;    M[15]=1;
	*/
	m[0][0] = X[0];
	m[0][1] = Y[0];
	m[0][2] = Z[0];
	m[0][3] = 0;

	m[1][0] = X[1];
	m[1][1] = Y[1];
	m[1][2] = Z[1];
	m[1][3] = 0;

	m[2][0] = X[2];
	m[2][1] = Y[2];
	m[2][2] = Z[2];
	m[2][3] = 0;

	m[3][0] = dot(X, T);
	m[3][1] = dot(Y, T);
	m[3][2] = dot(Z, T);
	m[3][3] = 1;
}

void SimpleCamera::GetLookAtMatrixLH(float44 m)
{
	float3 X, Y, Z = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
	float3 T = {-eye[0], -eye[1], -eye[2]};

	cross(up, Z, X);
	cross(Z, X, Y);

	normalize(X);
	normalize(Y);
	normalize(Z);

	m[0][0] = X[0];
	m[0][1] = Y[0];
	m[0][2] = Z[0];
	m[0][3] = 0.0f;

	m[1][0] = X[1];
	m[1][1] = Y[1];
	m[1][2] = Z[1];
	m[1][3] = 0;

	m[2][0] = X[2];
	m[2][1] = Y[2];
	m[2][2] = Z[2];
	m[2][3] = 0;

	m[3][0] = dot(X, T);
	m[3][1] = dot(Y, T);
	m[3][2] = dot(Z, T);
	m[3][3] = 1;
}

void SimpleCamera::GetLookAtMatrix(DirectX::XMMATRIX &mtx)
{
	float44 m;
	GetLookAtMatrix(m);
	Dx12Util::Transpose(mtx, m);
}

void SimpleCamera::GetLookAtMatrixLH(DirectX::XMMATRIX &mtx)
{
	float44 m;
	GetLookAtMatrixLH(m);
	Dx12Util::Transpose(mtx, m);
}

void SimpleCamera::GetProjectionMatrixLH(UINT width, UINT height, float44 mtx) const
{
	SimpleCamera::GetProjectionMatrixLH(fovy, static_cast<float>(width) / static_cast<float>(height), znear, zfar, mtx);
}

void SimpleCamera::GetProjectionMatrixLH(UINT width, UINT height, DirectX::XMMATRIX &mtx) const
{
#ifdef REVERSED_Z
	//DirectX::XMMATRIX m;
	//m = DirectX::XMMatrixPerspectiveFovLH(DEGtoRAD(fovy), static_cast<float>(width) / static_cast<float>(height), zfar, znear);
	//mtx = DirectX::XMMatrixTranspose(m);
	float44 m;
	GetProjectionMatrixLH(width, height, m);
	Dx12Util::Transpose(mtx, m);
#else
	DirectX::XMMATRIX m;
	m = DirectX::XMMatrixPerspectiveFovLH(DEGtoRAD(fovy), static_cast<float>(width) / static_cast<float>(height), znear, zfar);
	mtx = DirectX::XMMatrixTranspose(m);
#endif
}

void SimpleCamera::GetProjectionMatrixLH(float fovy_deg, float aspect, float znear, float zfar, float44 m)
{
	assert(aspect > 0.01f);

	float n, f;

	if(zfar > znear) {
		n = znear;
		f = zfar;
	} else {
		f = znear;
		n = zfar;
	}

	float fov = tanf(DEGtoRAD(fovy_deg) * 0.5f);

	m[0][0] = 1.0f / (aspect * tanf(fov));
	m[0][1] = 0.0f;
	m[0][2] = 0.0f;
	m[0][3] = 0.0f;

	m[1][0] = 0.0f;
	m[1][1] = 1.0f / tanf(fov);
	m[1][2] = 0.0f;
	m[1][3] = 0.0f;

#ifdef REVERSED_Z
	// 以下の２つは最終的に得られる Z 値が同じ範囲内で同じ分布する為どちらでも良い
# if 0
	// ndc_z = (-n / (f - n) * z + 1) / z
	m[2][0] = 0;
	m[2][1] = 0;
	m[2][2] = -n / (f - n);
	m[2][3] = 1;

	m[3][0] = 0;
	m[3][1] = 0;
	m[3][2] = 1;
	m[3][3] = 0;
# else
	// 一般式: 係数 * z + バイアス
	// ndc_z = (n / (n - f)) * z + (-(n * f) / (n - f))
	m[2][0] = 0;
	m[2][1] = 0;
	m[2][2] = -n / (f - n);
	m[2][3] = 1;

	m[3][0] = 0;
	m[3][1] = 0;
	m[3][2] = -(n * f) / (n - f);
	m[3][3] = 0;
# endif
#else
	m[2][0] = 0;
	m[2][1] = 0;
	m[2][2] = f / (f - n);
	m[2][3] = 1.0f;

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = -(n * f) / (f - n);
	m[3][3] = 0.0f;
#endif
}

void SimpleCamera::GetFrustumMatrix(float l, float r, float b, float t, float n, float f, float44 m)
{
	assert(fabsf(r - l) > 0.0001f);
	assert(fabsf(t - b) > 0.0001f);
	assert(fabsf(f - n) > 0.0001f);
	/*
	M[0]=(2*n)/(r-l); M[4]=0;           M[8]=(r+l)/(r-l);   M[12]=0;
	M[1]=0;           M[5]=(2*n)/(t-b); M[9]=(t+b)/(t-b);   M[13]=0;
	M[2]=0;           M[6]=0;           M[10]=-(f+n)/(f-n); M[14]=-(2*f*n)/(f-n);
	M[3]=0;           M[7]=0;           M[11]=-1;           M[15]=0;
	*/
	m[0][0] = (2.0f*n)/(r-l);
	m[0][1] = 0.0f;
	m[0][2] = 0.0f;
	m[0][3] = 0.0f;

	m[1][0] = 0.0f;
	m[1][1] = (2.0f*n)/(t-b);
	m[1][2] = 0.0f;
	m[1][3] = 0.0f;

	m[2][0] = (r+l)/(r-l);
	m[2][1] = (t+b)/(t-b);
	m[2][2] = -(f+n)/(f-n);
	m[2][3] = -1.0f;

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = -(2.0f*f*n)/(f-n);
	m[3][3] = 0.0f;

#if	defined(_DEBUG)
	for(int i = 0; i < 16; i++) {
		if(_isnanf(((float *)m)[i])) DebugBreak();
	}
#endif
}

void SimpleCamera::GetFrustumMatrixLH(float l, float r, float b, float t, float n, float f, float44 m)
{
	assert(fabsf(r - l) > 0.0001f);
	assert(fabsf(t - b) > 0.0001f);
	assert(fabsf(f - n) > 0.0001f);

	//float fov = 2.0f * atanf((fabsf(t - b) * 0.5f) / n);
	float fov = 2*atanf((fabsf(t - b) * 0.5f) / n);
	float aspect = (r - l) / (t - b);

	m[0][0] = 1.0f / (aspect * tanf(fov/2));
	m[0][1] = 0.0f;
	m[0][2] = 0.0f;
	m[0][3] = 0.0f;

	m[1][0] = 0.0f;
	m[1][1] = 1.0f / tanf(fov/2);
	m[1][2] = 0.0f;
	m[1][3] = 0.0f;

	m[2][0] = 0;
	m[2][1] = 0;
	m[2][2] = f / (f - n);
	m[2][3] = 1.0f;

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = -(n * f) / (f - n);
	m[3][3] = 0.0f;

#if	defined(_DEBUG)
	for(int i = 0; i < 16; i++) {
		if(_isnanf(((float *)m)[i])) DebugBreak();
	}
#endif
}

void SimpleCamera::GetFrustumMatrixLHReversedZ(float l, float r, float b, float t, float n, float f, float44 m)
{
	assert(fabsf(r - l) > 0.0001f);
	assert(fabsf(t - b) > 0.0001f);
	assert(fabsf(f - n) > 0.0001f);

	//float fov = 2.0f * atanf((fabsf(t - b) * 0.5f) / n);
	float fov = atanf(fabsf(t) / n);
	float aspect = (r - l) / (t - b);

	m[0][0] = 1.0f / (aspect * tanf(fov));
	m[0][1] = 0.0f;
	m[0][2] = 0.0f;
	m[0][3] = 0.0f;

	m[1][0] = 0.0f;
	m[1][1] = 1.0f / tanf(fov);
	m[1][2] = 0.0f;
	m[1][3] = 0.0f;

	m[2][0] = 0;
	m[2][1] = 0;
	m[2][2] = -n / (f - n);
	m[2][3] = 1.0f;

	m[3][0] = 0.0f;
	m[3][1] = 0.0f;
	m[3][2] = 1.0f;
	m[3][3] = 0.0f;

#if	defined(_DEBUG)
	for(int i = 0; i < 16; i++) {
		if(_isnanf(((float *)m)[i])) DebugBreak();
	}
#endif
}
