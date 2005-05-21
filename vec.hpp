/* Copyright (C) 2003-2005 Ahti Heinla
   Licensing conditions are described in the file LICENSE
*/

#include <math.h>

#ifdef _MSC_VER
#pragma warning(disable:4244)
#pragma warning(disable:4245)
#pragma warning(disable:4018)
#pragma warning(disable:4305)
#pragma warning(disable:4355)
#endif

typedef unsigned int        uint;
typedef unsigned short      ushort;
typedef unsigned char       uchar;
typedef signed int          sint;
typedef signed char         schar;

#undef max
#undef min
#define max(a,b)            ((a)>=(b)?(a):(b))
#define min(a,b)            ((a)<=(b)?(a):(b))
#define lenof(t)            (sizeof(t)/sizeof(*t))

template <class T>
struct vec {
	T	x;
	T	y;

	vec<T> exchange_components(void)
		{	T tmp=x;
			x=y;
			y=tmp;
			return *this;
			}
	};

template<class T> struct vec3d {
	T	x;
	T	y;
	T	z;

	static vec3d<T> make(const T x,const T y=0,const T z=0)
		{
			const vec3d<T> dest={x,y,z};
			return dest;
			}

	vec3d<float> tofloat(void) const {
		vec3d<float> dest;
		dest.x=(float)x;
		dest.y=(float)y;
		dest.z=(float)z;
		return dest;
		}

	vec3d<double> todouble(void) const {
		vec3d<double> dest;
		dest.x=(double)x;
		dest.y=(double)y;
		dest.z=(double)z;
		return dest;
		}
	};

#define vec3d_mul_with_const(const_type) template <class T> \
		vec3d<T> operator * (const vec3d<T> &a,const const_type b) { \
		const vec3d<T> dest={(T)(a.x * b),(T)(a.y * b),(T)(a.z * b)}; \
		return dest; }
vec3d_mul_with_const(double)
vec3d_mul_with_const(float)
vec3d_mul_with_const(sint)
vec3d_mul_with_const(uint)
vec3d_mul_with_const(ushort)

template <class T,class U>
void operator *= (vec3d<T> &a,const U value) {
	a.x=(T)(a.x*value);
	a.y=(T)(a.y*value);
	a.z=(T)(a.z*value);
	}

template <class T>
T operator * (const vec3d<T> &a,const vec3d<T> &b) {
	return a.x*b.x + a.y*b.y + a.z*b.z;
	}

template <class T>
vec3d<T> operator % (const vec3d<T> &a,const vec3d<T> &b) {
	vec3d<T> dest;
	dest.x=a.y*b.z - a.z*b.y;	
	dest.y=a.z*b.x - a.x*b.z;	
	dest.z=a.x*b.y - a.y*b.x;	
	return dest;
	}

struct matrix {
	vec3d<float>	x_vec,y_vec,z_vec;	// base vectors

	matrix &inverse(void);	// throws an exception if matrix cannot be inversed
	};
