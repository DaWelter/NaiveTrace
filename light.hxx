#ifndef LIGHT_HXX
#define LIGHT_HXX

#include "vec3f.hxx"
#include "ray.hxx"
#include "sampler.hxx"
#include "radianceorimportance.hxx"



class Light : public RadianceOrImportance::EmitterSensor
{
public:
  using Sample = RadianceOrImportance::Sample;
  using DirectionalSample = RadianceOrImportance::DirectionalSample;
};

// class DirectionalLight : public Light
// {
// 	Double3 dir;
// 	Double3 col;
// public:
// 	DirectionalLight(const Double3 &col,const Double3 &dir) 
// 		: dir(dir) , col(col)
// 	{}
// 
//   LightSample TakePositionSampleTo(const Double3 &org) const override
//   {
//     LightSample s;
//     // We evaluate Le(x0->dir) = col * delta[dir * (org-x0)] by importance sampling, where
//     // x0 is on the light source, delta is the dirac delta distribution.
//     // Taking the position as below importance samples the delta distribution, i.e. we take
//     // the sample deterministically.
//     s.pos_on_light = org - LargeNumber * dir;
//     s.radiance = col;
//     // This should be the delta function. However it cannot be represented by a regular float number.
//     // Fortunately the calling code should only ever use the quotient of radiance and pdf. Thus
//     // the deltas cancel out and I can set this to one.
//     s.pdf_of_pos = 1.;
//     s.is_extremely_far_away = true;
//   }
//   
//   Light::DirectionalSample TakeDirectionalSample() const override
//   {
//     // I need a random position where the ray with direction this->dir has a chance to hit the world. 
//     // TODO: implement
//   }
// };



class PointLight : public Light
{
	Spectral col; // Total power distributed uniformely over the unit sphere.
	Double3 pos;
public:
	PointLight(const Double3 &col,const Double3 &pos)
		: col(col),pos(pos)
	{}

  Sample TakePositionSample(Sampler &sampler) const override
  {
    Sample s {
      pos,
      1.,
      col,
      false };
    return s;
  }
  
  DirectionalSample TakeDirectionSampleFrom(const Double3 &pos, Sampler &sampler) const override
  {
    constexpr double one_over_unit_sphere_surface_area = 1./(4.*Pi);
    DirectionalSample s{
      { pos, SampleTrafo::ToUniformSphere(sampler.UniformUnitSquare()) },
      one_over_unit_sphere_surface_area,
      Spectral{1.}
    };
    return s;
  }
  
  Spectral EvaluatePositionComponent(const Double3 &pos, double *pdf) const override
  {
    assert (Length(pos - this->pos) <= Epsilon);
    if (pdf) *pdf = 1.;
    return col;
  }
  
  Spectral EvaluateDirectionComponent(const Double3 &pos, const Double3 &dir_out, double *pdf) const override
  {
    constexpr double one_over_unit_sphere_surface_area = 1./(4.*Pi);
    if (pdf) *pdf = one_over_unit_sphere_surface_area;
    return Spectral{1.};
  }
};


// class SpotLight : public Light
// {
// 	Double3 pos,dir,col;
// 	double min,max;
// public:
// 	SpotLight(	const Double3 &_col,
// 				const Double3 &_pos,
// 				const Double3 &_dir,
// 				double _min,
// 				double _max ) 
// 				: pos(_pos),dir(_dir),col(_col),min(_min),max(_max) 
// 	{
// 		Normalize(dir);
// 		min *= Pi/180.;
// 		max *= Pi/180.;
// 	}
// 
// 	virtual bool Illuminate(Ray &ray,Double3 &intensity)
// 	{
// 		Double3 d = ray.org-pos;
// 		double l = Length(d);
// 		d		= d/l;
// 		ray.dir = -d;
// 		ray.t   = l-Epsilon;
// 
// 		double alpha = acos(Dot(d,dir));
// 		if(alpha>max) return false;
// 		double weight = (alpha-min)/(max-min);
// 		Clip(weight,0.,1.);
// 		weight = 1.-weight;
// 
// 		double c1,c2,c3;
// 		c1 = 1.;
// 		c2 = 0.5;
// 		c3 = 0.;
// 		double attenuation = 1./(c1+l*c2+l*l*c3);
// 
// 		intensity = col * weight * attenuation;
// 		return true;
// 	}
// };

#endif