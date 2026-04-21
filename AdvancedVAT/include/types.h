#pragma once

constexpr float PIf = 3.14159265358979323846f;	// C++20 ˆÈ~‚Í std::numbers::pi
constexpr float PIfHalf = (PIf / 2.0f);

typedef	float	float2[2];
typedef	float	float3[3];
typedef	float	float4[4];
typedef	float	float44[4][4];

#define	DEGtoRAD(V)	(V * 0.0174532925199f)
#define	RADtoDEG(V)	(V * 57.2957795131f)

#ifndef SAFE_RELEASE
#define	SAFE_RELEASE(OBJ)	\
	if(OBJ) {				\
		OBJ->Release();		\
		OBJ = nullptr;		\
	}
#endif
