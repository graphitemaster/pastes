#ifndef M_BBOX_HDR
#define M_BBOX_HDR
#ifdef __SSE2__
#   include <xmmintrin.h>
#endif

#include "m_vec3.h"

namespace m {

#ifdef __SSE2__
struct alignas(16) ray {
    __m128 origin;
    __m128 direction;
    __m128 invert;
#else
    vec3 origin;
    vec3 direction;
    vec3 invert;
#endif

    ray(const vec3 &origin, const vec3 &direction);

    vec3 where(float distance) const;
};

#ifdef __SSE2__
inline ray::ray(const vec3 &origin, const vec3 &direction)
    : origin(_mm_set_ps(0.0f, origin.z, origin.y, origin.x))
    , direction(_mm_set_ps(0.0f, direction.z, direction.y, direction.x))
    , invert(_mm_div_ps(_mm_set_ps(0.0f, 1.0f, 1.0f, 1.0f), this->direction))
{
}

inline vec3 ray::where(float distance) const {
    const __m128 scale = _mm_mul_ps(direction, _mm_set_ps(0.0f, distance, distance, distance));
    const union {
        __m128 add;
        float w[4]; // w, z, y, x
    } add = { _mm_add_ps(origin, scale) };
    return vec3(add.w[3], add.w[2], add.w[1]);
}
#else
inline ray::ray(const vec3 &origin, const vec3 &direction)
    : origin(origin)
    , direction(direction)
    , invert(vec3(1.0f, 1.0f, 1.0f).cdiv(direction))
{
}

inline vec3 ray::where(float distance) const {
    return origin + direction * distance;
}
#endif

#ifdef __SSE2__
struct alignas(16) bbox {
    __m128 min;
    __m128 max;
    __m128 extent;
#else
struct bbox {
    vec3 min;
    vec3 max;
    vec3 extent;
#endif

    bbox() = default;
    bbox(const vec3 &min, const vec3 &max);
    bbox(const vec3 &point);

    bool intersect(const ray &r, float &tnear, float &tfar) const;
};

#ifdef __SSE2__
inline bbox::bbox(const vec3 &min, const vec3 &max)
    : min(_mm_set_ps(0.0f, min.z, min.y, min.x))
    , max(_mm_set_ps(0.0f, max.z, max.y, max.x))
    , extent(_mm_sub_ps(this->max, this->min))
{
}

inline bbox::bbox(const vec3 &point)
    : min(_mm_set_ps(0.0f, point.z, point.y, point.x))
    , max(_mm_set_ps(0.0f, point.z, point.y, point.x))
{
}

static const float kInf = -logf(0.0f);

alignas(16) static const float kInfP[4] = { kInf, kInf, kInf, kInf };
alignas(16) static const float kInfM[4] = { -kInf, -kInf, -kInf, -kInf };

static const __m128 kPlusInf = _mm_load_ps((const float *const)kInfP);
static const __m128 kMinusInf = _mm_load_ps((const float *const)kInfM);
    
inline bool bbox::intersect(const ray &r, float &tnear, float &tfar) const {
    const __m128 boxMin = min;
    const __m128 boxMax = max;
    const __m128 position = r.origin;
    const __m128 invert = r.invert;

    // Save a divide instruction
    const __m128 l1 = _mm_mul_ps(_mm_sub_ps(boxMin, position), invert);
    const __m128 l2 = _mm_mul_ps(_mm_sub_ps(boxMax, position), invert);

    // Filter out NaNs when inverse is +/- inf and (min - position) is 0.
    // inf * 0 = NaN.
    const __m128 filterl1a = _mm_min_ps(l1, kPlusInf);
    const __m128 filterl2a = _mm_min_ps(l2, kPlusInf);
    const __m128 filterl1b = _mm_max_ps(l1, kMinusInf);
    const __m128 filterl2b = _mm_max_ps(l2, kMinusInf);

    __m128 lmax = _mm_max_ps(filterl1a, filterl2a);
    __m128 lmin = _mm_min_ps(filterl1b, filterl2b);

    // unfold back while hiding latency of shufps
    const __m128 lmax0 = _mm_shuffle_ps(lmax, lmax, 0x39); // a,b,c,d -> b,c,d,a
    const __m128 lmin0 = _mm_shuffle_ps(lmin, lmin, 0x39); // a,b,c,d -> b,c,d,a
    lmax = _mm_min_ss(lmax, lmax0);
    lmin = _mm_max_ss(lmin, lmin0);

    const __m128 lmax1 = _mm_movehl_ps(lmax, lmax); // low{a,b,c,d}|high{e,f,g,h} = {c,d,g,h}
    const __m128 lmin1 = _mm_movehl_ps(lmin, lmin); // low{a,b,c,d}|high{e,f,g,h} = {c,d,g,h}
    lmax = _mm_min_ss(lmax, lmax1);
    lmin = _mm_max_ss(lmin, lmin1);

    const bool hit = _mm_comige_ss(lmax, _mm_setzero_ps()) & _mm_comige_ss(lmax, lmin);

    _mm_store_ss((float *const)&tnear, lmin);
    _mm_store_ss((float *const)&tfar, lmax);

    return hit;
}
#else
inline bbox::bbox(const vec3 &min, const vec3 &max)
    : min(min)
    , max(max)
    , extent(max - min)
{
}

inline bbox::bbox(const vec3 &point)
    : min(point)
    , max(point)
{
}

inline bool bbox::intersect(const ray &r, float &tnear, float &tfar) const {
    const vec3 c0 = r.inverse * (min - r.origin);
    const vec3 c1 = r.inverse * (max - r.origin);
    const vec3 boxMin = u::min(c1, c0);
    const vec3 boxMax = u::max(c1, c0);

    tnear = u::max(u::max(boxMin.x, boxMin.y), boxMin.z);
    tfar = u::max(u::max(boxMax.x, boxMax.y), boxMax.z);

    return !(tnear > tfar) && tfar > 0;
}
#endif

}

#endif
