#ifndef RAY_HXX
#define RAY_HXX

#include <limits>
#include <vector>
#include "vec3f.hxx"

class Primitive;
class Shader;
class Medium;

static constexpr double RAY_EPSILON = 1.e-6; //1.e-10;


struct Ray
{
  Ray() {}
  Ray(Double3 _org, Double3 _dir) : org(_org), dir(_dir) {}
  Double3 org; // origin
  Double3 dir; // direction
  
  auto PointAt(double t) const -> decltype(org + t * dir)
  {
    return org + t * dir;
  }
};


struct RaySegment
{
  Ray ray;
  double length;
  
  RaySegment() : length(NaN) {}
  RaySegment(const Ray &_ray, double _length) : ray(_ray), length(_length) {}
  static RaySegment FromTo(const Double3 &src, const Double3 &dest);
  
  auto EndPoint() const -> decltype(ray.PointAt(length))
  { 
    // Want to return some expression template construct. Not an actual Double3. To facilitate optimization.
    return ray.PointAt(length);
  }
  
  inline void ShortenBothEndsBy(double epsilon)
  {
    ray.org += epsilon*ray.dir;
    length -= 2.*epsilon;
  }
  
  RaySegment Reversed() const
  {
    return {{ray.org + length * ray.dir, -ray.dir}, length };
  }
};


inline std::ostream &operator<<(std::ostream &o,const Ray &ray)
{ o << "Ray[" << ray.org << "+t*" << ray.dir << "]"; return o; }


struct HitId
{
  const Primitive *primitive = { nullptr }; // primitive that was hit
  Double3 barry;
  
  operator bool() const
  {
    return primitive != nullptr;
  }
};


struct InteractionPoint
{
  Double3 pos;
};


struct SurfaceInteraction : public InteractionPoint
{
  HitId hitid;
  Double3 geometry_normal;
  Double3 smooth_normal;
  
  const Primitive& primitive() const;
  const Shader& shader() const;
  
  SurfaceInteraction(const HitId &hitid);
  SurfaceInteraction() = default;
};


struct RaySurfaceIntersection : public SurfaceInteraction
{
  Double3 normal;        // Used for shading and tracing. Oriented toward the incomming ray.
  Double3 shading_normal;
  
  RaySurfaceIntersection(const HitId &hitid, const RaySegment &_incident_segment);
  RaySurfaceIntersection() = default;
};


struct VolumeInteraction : public InteractionPoint
{
  const Medium *_medium = nullptr;
  VolumeInteraction() = default;
  VolumeInteraction(const Double3 &_position, const Medium &_medium)
    : InteractionPoint{_position}, _medium{&_medium}
    {}
    
  const Medium& medium() const { return *_medium; }
};



inline Double3 AntiSelfIntersectionOffset(const Double3 &normal, double eps, const Double3 &exitant_dir)
{
  return eps * (Dot(exitant_dir, normal) > 0. ? 1. : -1.) * normal;
}


inline Double3 AntiSelfIntersectionOffset(const RaySurfaceIntersection &intersection, double eps, const Double3 &exitant_dir)
{
  return AntiSelfIntersectionOffset(intersection.normal, eps, exitant_dir);
}


struct HitRecord : public HitId
{
  HitRecord(const HitId &hit, double _t) :
    HitId(hit), t(_t) {}
  HitRecord() : t(LargeNumber) {}
  double t;
};
using HitVector = std::vector<HitRecord>;


#endif
