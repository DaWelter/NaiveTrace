#include "sampler.hxx"

namespace SampleTrafo
{
// Ref: Global Illumination Compendium (2003)
Double3 ToUniformDisc(Double2 r)
{
  double s = std::sqrt(r[1]);
  double omega = 2.*Pi*r[0];
  double x = s*std::cos(omega);
  double y = s*std::sin(omega);
  return Double3{x,y,0};
}

// Ref: Global Illumination Compendium (2003)
Double3 ToUniformSphere(Double2 r)
{
  double z = 1. - 2.*r[1];
  double s = std::sqrt(r[1]*(1.-r[1]));
  double omega = 2.*Pi*r[0];
  double sn = std::sin(omega);
  double cs = std::cos(omega);
  double x = 2.*cs*s;
  double y = 2.*sn*s;
  return Double3{x,y,z};
}


// Modification of the above
Double3 ToUniformSphereSection(Double2 r, double phi0, double z0, double phi1, double z1)
{
//   const double z0 = std::cos(theta0);
//   const double z1 = std::cos(theta1);
  const double z = z0 + r[1]*(z1 - z0);
  double s = std::sqrt(1. - z*z);
  s = std::isnan(s) ? 0. : s;
  double omega = phi0 + (phi1-phi0)*r[0];
  double sn = std::sin(omega);
  double cs = std::cos(omega);
  double x = cs*s;
  double y = sn*s;
  return Double3{x,y,z};
}


// That one is obvious isn't it.
Double3 ToUniformHemisphere(Double2 r)
{
  Double3 v = ToUniformSphere(r);
  v[2] = v[2]>=0. ? v[2] : -v[2];
  return v;
}

// Ref: Global Illumination Compendium (2003)
Double3 ToUniformSphereSection(double cos_opening_angle, Double2 r)
{
  double z = 1.-r[1]*(1.-cos_opening_angle);
  double s = std::sqrt(1. - z*z);
  double omega = 2.*Pi*r[0];
  double sn = std::sin(omega);
  double cs = std::cos(omega);
  double x = cs*s;
  double y = sn*s;
  return Double3{x,y,z};
}

// Ref: Global Illumination Compendium (2003)
Double3 ToCosHemisphere(Double2 r)
{
  double rho = std::sqrt(1.-r[0]);
  double z   = std::sqrt(r[0]);
  double omega = 2.*Pi*r[1];
  double sn = std::sin(omega);
  double cs = std::cos(omega);
  return Double3{cs*rho, sn*rho, z};
}

Double3 ToPhongHemisphere(Double2 r, double alpha)
{
  double t = std::pow(r[0], 1./(alpha+1));
  double rho = std::sqrt(1.-t);
  double z   = std::sqrt(t);
  double omega = 2.*Pi*r[1];
  double sn = std::sin(omega);
  double cs = std::cos(omega);
  return Double3{cs*rho, sn*rho, z};
}


/* Samples the Beckman microfacet distribution D(m) times |m . n|. 
 * The the surface normal n is assumed aligned with the z-axis.
 * Returns the half-angle vector m. 
 * Ref: Walter et al. (2007) "Microfacet Models for Refraction through Rough Surfaces" Eq. 28, 29.
 */
Double3 ToBeckmanHemisphere(Double2 r, double alpha)
{
  double t1 = -alpha*alpha*std::log(r[0]);
  double t = 1./(t1+1.);
  double z = std::sqrt(t);
  double rho = std::sqrt(1.-t);
  double omega = 2.*Pi*r[1];
  double sn = std::sin(omega);
  double cs = std::cos(omega);
  return Double3{cs*rho, sn*rho, z};
}


/* Ref: Total Compendium pg. 12 */
Double3 ToTriangleBarycentricCoords(Double2 r)
{
  double sqrtr0 = std::sqrt(r[0]);
  double alpha = 1. - sqrtr0;
  double beta  = (1.-r[1])*sqrtr0;
  double gamma = r[1]*sqrtr0;
  return Double3{alpha, beta, gamma};
}


Double3 ToUniformSphere3d(const Double3 &rvs)
{
// https://math.stackexchange.com/questions/87230/picking-random-points-in-the-volume-of-sphere-with-uniform-probability
// Apply inversion method to probability of finding a point at distance D in a smaller sphere of radius 'rho'.
// That is, CDF(rho) = P(D < rho) = (rho/sphere_radius)^3.
// Thus, rho = CDF^{-1}(rvs[0]) = ....
  double rho = std::pow(rvs[0], 1./3.);
  Double3 p = ToUniformSphere({rvs[1], rvs[2]});
  return rho*p;
}

}


Sampler::Sampler()
  : random_engine(),
    uniform(0., 1.)
{

}


void Sampler::Seed(std::uint64_t seed)
{
  random_engine.seed(seed);
}


void Sampler::Uniform01(double* dest, int count)
{
  for (int i=0; i<count; ++i)
    dest[i] = uniform(random_engine);
}


int Sampler::UniformInt(int a, int b_inclusive)
{
  return std::uniform_int_distribution<int>(a, b_inclusive)(random_engine);
}

constexpr std::uint64_t Sampler::default_seed;


// Double3 Sampler::UniformSphere()
// {
//   double r[2];
//   Uniform01(r, 2);
//   return TransformToUniformSphere(r[0], r[1]);
// }
// 
// 
// Double3 Sampler::UniformHemisphere()
// {
//   Double3 v = UniformSphere();
//   v[2] = v[2]>=0. ? v[2] : -v[2];
//   return v;
// }
