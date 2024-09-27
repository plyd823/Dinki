#pragma once
#include <corecrt_math.h>
#include "hookmgr.h"

class CVector2f
{
public:
	CVector2f() { x = 0.f; y = 0.f; }
	CVector2f(float fX, float fY)
	{
		x = fX;
		y = fY;
	}
	float x, y;
};

class CVector3f
{
public:
	CVector3f() { x = 0.f; y = 0.f; z = 0.f; }
	CVector3f(float fX, float fY, float fZ)
	{
		x = fX;
		y = fY;
		z = fZ;
	}
	float x, y, z;

	static CVector3f* SpeedLerp(CVector3f& result, CVector3f& from, CVector3f& to, float units, float dt, bool clamp_to_01);

	inline float Magnitude()
	{
		//printf("%f %f %f %f\n", x, y, z, sqrtf((x * x + y * y + z * z)));
		return sqrtf((x * x + y * y + z * z));
	}

	inline float Distance(CVector3f const& to)
	{
		return CVector3f(x - to.x, y - to.y, z - to.z).Magnitude();
	}
};

inline CVector3f operator-(const CVector3f& vecOne, const CVector3f& vecTwo) {
	return CVector3f(vecOne.x - vecTwo.x, vecOne.y - vecTwo.y, vecOne.z - vecTwo.z);
}

inline CVector3f operator+(const CVector3f& vecOne, const CVector3f& vecTwo) {
	return CVector3f(vecOne.x + vecTwo.x, vecOne.y + vecTwo.y, vecOne.z + vecTwo.z);
}

inline CVector3f operator*(const CVector3f& vecOne, const CVector3f& vecTwo) {
	return CVector3f(vecOne.x * vecTwo.x, vecOne.y * vecTwo.y, vecOne.z * vecTwo.z);
}

inline CVector3f operator*(const CVector3f& vec, float multiplier) {
	return CVector3f(vec.x * multiplier, vec.y * multiplier, vec.z * multiplier);
}

inline CVector3f Lerp(const CVector3f& vecOne, const CVector3f& vecTwo, float fProgress) {
	return vecOne * (1.0F - fProgress) + vecTwo * fProgress;
}

inline float DistanceBetweenPoints(const CVector3f& pointOne, const CVector3f& pointTwo) {
	return (pointTwo - pointOne).Magnitude();
}

class CPlane
{
public:
	CVector3f Normal;
	float D;
};

class CAABox
{
	CVector3f m_Min;
	CVector3f m_Max;
};


class CVector4f
{
public:
	CVector4f() { x = 0.f; y = 0.f; z = 0.f; w = 0.f; }
	CVector4f(float fX, float fY, float fZ, float fW)
	{
		x = fX;
		y = fY;
		z = fZ;
		w = fW;
	}

	CVector4f(CVector3f const& vec3)
	{
		x = vec3.x;
		y = vec3.y;
		z = vec3.z;
		w = 1.0f;
	}

	float x, y, z, w;
};



template<typename T>
class matrix
{
public:
	union {
		struct {
			T _11, _12, _13, _14;
			T _21, _22, _23, _24;
			T _31, _32, _33, _34;
			T _41, _42, _43, _44;
		};
		T m[4][4];
	};
public:
	matrix()
	{
		Identity();
	}
	matrix(T m11, T m12, T m13, T m14,
		T m21, T m22, T m23, T m24,
		T m31, T m32, T m33, T m34,
		T m41, T m42, T m43, T m44)
	{
		_11 = m11; _12 = m12; _13 = m13; _14 = m14;
		_21 = m21; _22 = m22; _23 = m23; _24 = m24;
		_31 = m31; _32 = m32; _33 = m33; _34 = m34;
		_41 = m41; _42 = m42; _43 = m43; _44 = m44;
	}
	matrix operator* (const matrix& other)
	{
		matrix result;
		for (int i = 0; i < 4; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				result.m[i][j] = m[i][0] * other.m[0][j] + m[i][1] * other.m[1][j] + m[i][2] * other.m[2][j] + m[i][3] * other.m[3][j];
			}
		}
		return result;
	}

	CVector4f& operator* (CVector4f const& v) {
		CVector4f res;
		return HookMgr::Call<CVector4f&>(0x1409DEA30, this, &res, &v);
	}

	CMETHOD(0x1409EA970, 0x0, matrix&, RotationYawPitchRoll(float yaw, float pitch, float roll), yaw, pitch, roll)

	void Identity()
	{
		_11 = 1.0f;	_12 = 0.0f;	_13 = 0.0f; _14 = 0.0f;
		_21 = 0.0f;	_22 = 1.0f;	_23 = 0.0f; _14 = 0.0f;
		_31 = 0.0f;	_32 = 0.0f;	_33 = 1.0f; _14 = 0.0f;
		_41 = 0.0f;	_42 = 0.0f;	_43 = 0.0f; _14 = 1.0f;
	}
	CVector3f Right() { return CVector3f(_11, _12, _13); }
	CVector3f Forward() { return CVector3f(_21, _22, _23); }
	CVector3f Up() { return CVector3f(_31, _32, _33); }
	CVector3f Position() { return CVector3f(_41, _42, _43); }

	void SetRight(const CVector3f& vec)
	{
		_11 = vec.x;
		_12 = vec.y;
		_13 = vec.z;
		_14 = 0.0f;
	}
	void SetForward(const CVector3f& vec)
	{
		_21 = vec.x;
		_22 = vec.y;
		_23 = vec.z;
		_24 = 0.0f;
	}
	void SetUp(const CVector3f& vec)
	{
		_31 = vec.x;
		_32 = vec.y;
		_33 = vec.z;
		_34 = 0.0f;
	}
	void SetPosition(const CVector3f& vec)
	{
		_41 = vec.x;
		_42 = vec.y;
		_43 = vec.z;
		_44 = 1.0f;
	}


	CMETHODV(0x1409EA850, 0x1429BE740, CVector3f&, ToEulerXYZ())
	CMETHOD(0x1409EA3F0, 0x1429BE740, matrix&, FromEulerXYZ(CVector3f const& v), &v)
	CMETHOD(0x1409EC760, 0x1429C0EE0, void, ToEuler(float& x, float& y, float& z), &x, &y, &z)

	const static matrix identity;
};
template<typename T>
const matrix<T> matrix<T>::identity;
using CMatrix = matrix<float>;
using CMatrix4f = matrix<float>;

class CQuaternion
{
public:
	CQuaternion() { x = 0.f; y = 0.f; z = 0.f; w = 0.f; }
	CQuaternion(float fX, float fY, float fZ, float fW)
	{
		x = fX;
		y = fY;
		z = fZ;
		w = fW;
	}
	//CVector4f v;
	float x, y, z, w;

	CMETHOD(0x1409F4470, 0x1429C91A0, CQuaternion&, FromMatrix4(const CMatrix& mat), &mat);
	CMETHOD(0x1409E1270, 0x1429B49A0, CMatrix&, ToMatrix4(CMatrix& mat), &mat);

	void Set(float _x, float _y, float _z, float _w)
	{
		w = _w;
		x = _x;
		y = _y;
		z = _z;
	}

	CMETHOD(0x1409EF470, 0x1429C3D30, CQuaternion&, FromEulerXYZ(CVector3f const& euler), euler)
	CMETHOD(0x1409EF030, 0x1429C38D0, CQuaternion&, FromEuler(float roll, float pitch, float yaw), roll, pitch, yaw)

	CVector3f operator*(CVector3f const& v) {
		CVector3f result;
		CCALL(0x1409E1BA0, 0x1429B5340, CVector3f&, &result, &v);
		return result;
	}
};