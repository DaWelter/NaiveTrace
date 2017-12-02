#include "shader.hxx"
#include "ray.hxx"
#include "scene.hxx"
#include "shader_util.hxx"

namespace
{
inline Spectral MaybeMultiplyTextureLookup(const Spectral &color, const Texture *tex, const RaySurfaceIntersection &surface_hit)
{
  Spectral ret{color};
  if (tex)
  {
    Double3 uv = surface_hit.primitive().GetUV(surface_hit.hitid);
    ret *= tex->GetTexel(uv[0], uv[1]).array();
  }
  return ret;
}
}



DiffuseShader::DiffuseShader(const Spectral &_reflectance, std::unique_ptr<Texture> _diffuse_texture)
  : Shader(IS_REFLECTIVE),
    kr_d(_reflectance),
    diffuse_texture(std::move(_diffuse_texture))
{
  // Wtf? kr_d is the (constant) Lambertian BRDF. Energy conservation
  // demands Int|_Omega kr_d cos(theta) dw <= 1. Working out the math
  // I obtain kr_d <= 1/Pi. 
  // But well, reflectance, also named Bihemispherical reflectance
  // [TotalCompendium.pdf,pg.31] goes up to one. Therefore I divide by Pi. 
  kr_d *= 1./Pi;
}


Spectral DiffuseShader::EvaluateBSDF(const Double3 &incident_dir, const RaySurfaceIntersection &surface_hit, const Double3& out_direction, double *pdf) const
{
  double n_dot_out = Dot(surface_hit.shading_normal, out_direction);
  if (pdf)
    *pdf = n_dot_out>0. ? n_dot_out/Pi : 0.;
  if (n_dot_out > 0.)
  {
    return MaybeMultiplyTextureLookup(kr_d, diffuse_texture.get(), surface_hit);
  }
  else
    return Spectral{0.};
}


BSDFSample DiffuseShader::SampleBSDF(const Double3 &incident_dir, const RaySurfaceIntersection &surface_hit, Sampler& sampler) const
{
  auto m = OrthogonalSystemZAligned(surface_hit.shading_normal);
  Double3 v = SampleTrafo::ToCosHemisphere(sampler.UniformUnitSquare());
  double pdf = v[2]/Pi;
  Double3 out_direction = m * v;
  Spectral value = Dot(out_direction, surface_hit.normal)>0 ? kr_d : Spectral{0.};
  return BSDFSample{out_direction, value, pdf};
}



SpecularReflectiveShader::SpecularReflectiveShader(const Spectral& reflectance)
  : Shader(REFLECTION_IS_SPECULAR|IS_REFLECTIVE),
    kr_s(reflectance)
{
}


Spectral SpecularReflectiveShader::EvaluateBSDF(const Double3& incident_dir, const RaySurfaceIntersection& surface_hit, const Double3& out_direction, double* pdf) const
{
  if (pdf)
    *pdf = 0.;
  return Spectral{0.};
}


BSDFSample SpecularReflectiveShader::SampleBSDF(const Double3& incident_dir, const RaySurfaceIntersection& surface_hit, Sampler& sampler) const
{
  Double3 r = Reflected(incident_dir, surface_hit.shading_normal);
  double cos_rn = Dot(surface_hit.normal, r);
  if (cos_rn < 0.)
    return BSDFSample{r, Spectral{0.}, 1.};
  else
  {
    double cos_rsdn = Dot(surface_hit.shading_normal, r);
    return BSDFSample{r, kr_s/cos_rsdn, 1.};
  }
}



namespace MicrofacetDetail
{
double G1(double cos_v_m, double cos_v_n, double alpha)
{
  // From Walter et al. 2007 "Microfacet Models for Refraction"
  // Eq. 27 which pertains to the Beckman facet distribution.
  // "... Instead we will use the Smith shadowing-masking approximation [Smi67]."
  if (cos_v_m * cos_v_n < 0.) return 0.;
  double a = cos_v_n/(alpha * std::sqrt(1 - cos_v_n * cos_v_n));
  if (a >= 1.6)
    return 1.;
  else
    return (3.535*a + 2.181*a*a)/(1.+2.276*a+2.577*a*a);
}
}


MicrofacetShader::MicrofacetShader(
  const Spectral &_glossy_reflectance, std::unique_ptr<Texture> _glossy_texture,
  double _glossy_exponent)
  : Shader(0),
    kr_s(_glossy_reflectance), 
    alpha(_glossy_exponent),
    glossy_texture(std::move(_glossy_texture))
{
}


Spectral MicrofacetShader::EvaluateBSDF(const Double3 &reverse_incident_dir, const RaySurfaceIntersection &surface_hit, const Double3& out_direction, double *pdf) const
{
  double n_dot_out = Dot(surface_hit.normal, out_direction);
  double ns_dot_out = Dot(surface_hit.shading_normal, out_direction);
  double ns_dot_in  = Dot(surface_hit.shading_normal, reverse_incident_dir);
  Double3 half_angle_vector = Normalized(reverse_incident_dir + out_direction);
  double ns_dot_wh = Dot(surface_hit.shading_normal, half_angle_vector);
  double wh_dot_out = Dot(out_direction, half_angle_vector);
  double wh_dot_in  = Dot(reverse_incident_dir, half_angle_vector);

  double microfacet_distribution_val;
  { // Beckman Distrib. This formula is using the normalized distribution D(cs) 
    // such that Int_omega D(cs) cs dw = 1, using the differential solid angle dw, 
    // integrated over the hemisphere.
    double cs = ns_dot_wh;
    double t1 = (cs*cs-1.)/(cs*cs*alpha*alpha);
    double t2 = alpha*alpha*cs*cs*cs*cs*Pi;
    microfacet_distribution_val = std::exp(t1)/t2;
  }
  
  if (pdf)
  {
    double half_angle_distribution_val = microfacet_distribution_val*std::abs(ns_dot_wh);
    double out_distribution_val = half_angle_distribution_val*0.25/wh_dot_out; // From density of h_r to density of out direction.
    *pdf = out_distribution_val;
  }
  
  if (ns_dot_wh <= 0. || n_dot_out <= 0. || wh_dot_out <= 0. || wh_dot_in <= 0.)
    return Spectral{0.};
  
  double geometry_term;
  {
#if 1
    // Cook & Torrance model. Seems to work good enough although Walter et al. has concerns about it's realism.
    // Ref: Cook and Torrance (1982) "A reflectance model for computer graphics"
    double t1 = 2.*ns_dot_wh*ns_dot_out / wh_dot_out;
    double t2 = 2.*ns_dot_wh*ns_dot_in / wh_dot_out;
    geometry_term = std::min(1., std::min(t1, t2));
#else
    // Overkill?
    geometry_term = MicrofacetDetail::G1(wh_dot_in, ns_dot_in, alpha)*
                    MicrofacetDetail::G1(wh_dot_out, ns_dot_out, alpha);
#endif
  }
  double fresnel_term;
  {
#if 0
    Way too little reflection as these formulae are for dielectrics which reflect mostly at glancing angles.
    Without a diffuse background this is not good. Anyway, I want to simulate metals with this shader.
    double eta_i = 1.0;
    double eta_t = 0.9;
    double eta = eta_t/eta_i;
    double c = wh_dot_in;
    double t5 = eta*eta - 1. + c*c;
    if (t5 >= 0.)
    {
      double g = std::sqrt(t5);
      double t1 = (g-c)*(g-c)/((g+c)*g+c);
      double t2 = c*(g+c)-1.;
      double t3 = c*(g-c)+1.;
      double t4 = (t2/t3)*(t2/t3);
      fresnel_term = 0.5*t1*(1. + t4);
    }
    else
      fresnel_term = 1.;
#else
    // Schlicks approximation. Parameters for metal.
    // Ref: Jacco Bikkers Lecture.
    double kspecular = 0.92; // Alu.
    fresnel_term = kspecular + (1-kspecular)*std::pow(1-wh_dot_in, 5.);
#endif
  }
  double microfacet_val = fresnel_term*geometry_term*microfacet_distribution_val*0.25/ns_dot_in/ns_dot_out;

  auto kr_s_local = MaybeMultiplyTextureLookup(kr_s, glossy_texture.get(), surface_hit);  

  return microfacet_val * kr_s_local;
}


BSDFSample MicrofacetShader::SampleBSDF(const Double3 &reverse_incident_dir, const RaySurfaceIntersection &surface_hit, Sampler& sampler) const
{
  auto m = OrthogonalSystemZAligned(surface_hit.shading_normal);
  Double3 h_r_local = SampleTrafo::ToBeckmanHemisphere(sampler.UniformUnitSquare(), alpha);
  Double3 h_r = m*h_r_local;
  // The following is the inversion of the half-vector formula. It is like reflection except for the abs. But the abs is needed.
  Double3 out_direction = 2.*std::abs(Dot(reverse_incident_dir, h_r))*h_r - reverse_incident_dir;
  BSDFSample smpl; 
  smpl.dir = out_direction;
  smpl.scatter_function = this->EvaluateBSDF(reverse_incident_dir, surface_hit, out_direction, &smpl.pdf);
  return smpl;
}




Spectral InvisibleShader::EvaluateBSDF(const Double3 &incident_dir, const RaySurfaceIntersection &surface_hit, const Double3& out_direction, double *pdf) const
{
  constexpr double tol = Epsilon;
  double u = LengthSqr(incident_dir + out_direction);
  u = u<tol ? 1. : 0.;
  if (pdf)
    *pdf = u;
  return Spectral{u};
}


BSDFSample InvisibleShader::SampleBSDF(const Double3 &incident_dir, const RaySurfaceIntersection &surface_hit, Sampler& sampler) const
{
  return BSDFSample{-incident_dir, Spectral{1.}, 1.};
}





Spectral VacuumMedium::EvaluatePhaseFunction(const Double3& indcident_dir, const Double3& pos, const Double3& out_direction, const PathContext &context, double* pdf) const
{
  if (pdf)
    *pdf = 1.;
  return Spectral{0.}; // Because it is a delta function.
}


Medium::InteractionSample VacuumMedium::SampleInteractionPoint(const RaySegment& segment, Sampler& sampler, const PathContext &context) const
{
  return Medium::InteractionSample{
      LargeNumber,
      Spectral{1.}
    };
}


PhaseFunctions::Sample VacuumMedium::SamplePhaseFunction(const Double3& incident_dir, const Double3& pos, Sampler& sampler, const PathContext &context) const
{
  return PhaseFunctions::Sample{
    -incident_dir,
    Spectral{1.},
    1.
  };
}


Spectral VacuumMedium::EvaluateTransmission(const RaySegment& segment, Sampler &sampler, const PathContext &context) const
{
  return Spectral{1.};
}




HomogeneousMedium::HomogeneousMedium(const Spectral& _sigma_s, const Spectral& _sigma_a, int priority)
  : Medium(priority), sigma_s{_sigma_s}, sigma_a{_sigma_a}, sigma_ext{_sigma_s + _sigma_a},
    phasefunction{new PhaseFunctions::Uniform()}
{
}


Spectral HomogeneousMedium::EvaluatePhaseFunction(const Double3& incident_dir, const Double3& pos, const Double3& out_direction, const PathContext &context, double* pdf) const
{
  return phasefunction->Evaluate(incident_dir, out_direction, pdf);
}


PhaseFunctions::Sample HomogeneousMedium::SamplePhaseFunction(const Double3& incident_dir, const Double3& pos, Sampler& sampler, const PathContext &context) const
{
  return phasefunction->SampleDirection(incident_dir, sampler);
}


Medium::InteractionSample HomogeneousMedium::SampleInteractionPoint(const RaySegment& segment, Sampler& sampler, const PathContext &context) const
{
  assert(!context.beta.isZero());
  Medium::InteractionSample smpl{
    0.,
    Spectral{1.}
  };
  double sigma_t_majorant = sigma_ext.maxCoeff();
  double inv_sigma_t_majorant = 1./sigma_t_majorant;
  constexpr int emergency_abort_max_num_iterations = 100;
  int iteration = 0;
  while (++iteration < emergency_abort_max_num_iterations)
  {
    smpl.t -= std::log(sampler.Uniform01()) * inv_sigma_t_majorant;
    if (smpl.t > segment.length)
    {
      return smpl;
    }
    else
    {
      Spectral sigma_n = sigma_t_majorant - sigma_ext;
      assert(sigma_n.minCoeff() >= -1.e-3); // By definition of the majorante
      double prob_t, prob_n;
      TrackingDetail::ComputeProbabilitiesHistoryScheme(smpl.weight, {sigma_s, sigma_n}, {prob_t, prob_n});
      double r = sampler.Uniform01();
      if (r < prob_t) // Scattering/Absorption
      {
        smpl.weight *= inv_sigma_t_majorant / prob_t * sigma_s;
        return smpl;
      }
      else // Null collision
      {
        smpl.weight *= inv_sigma_t_majorant / prob_n * sigma_n;
      }
    }
  }
  assert (false);
  return smpl;
}


Spectral HomogeneousMedium::EvaluateTransmission(const RaySegment& segment, Sampler &sampler, const PathContext &context) const
{
  return (-sigma_ext * segment.length).exp();
}





MonochromaticHomogeneousMedium::MonochromaticHomogeneousMedium(double _sigma_s, double _sigma_a, int priority)
  : Medium(priority), sigma_s{_sigma_s}, sigma_a{_sigma_a}, sigma_ext{_sigma_s + _sigma_a},
    phasefunction{new PhaseFunctions::Uniform()}
{
}


Spectral MonochromaticHomogeneousMedium::EvaluatePhaseFunction(const Double3& indcident_dir, const Double3& pos, const Double3& out_direction, const PathContext &context, double* pdf) const
{
  return phasefunction->Evaluate(indcident_dir, out_direction, pdf);
}


Medium::PhaseSample MonochromaticHomogeneousMedium::SamplePhaseFunction(const Double3& incident_dir, const Double3& pos, Sampler& sampler, const PathContext &context) const
{
  return phasefunction->SampleDirection(incident_dir, sampler);
}


Medium::InteractionSample MonochromaticHomogeneousMedium::SampleInteractionPoint(const RaySegment& segment, Sampler& sampler, const PathContext &context) const
{
  Medium::InteractionSample smpl;
  smpl.t = - std::log(1-sampler.Uniform01()) / sigma_ext;
  smpl.t = smpl.t < LargeNumber ? smpl.t : LargeNumber;
  smpl.weight = (smpl.t >= segment.length) ? 
    1.0
    :
    (sigma_s / sigma_ext);
  return smpl;
}


Spectral MonochromaticHomogeneousMedium::EvaluateTransmission(const RaySegment& segment, Sampler &sampler, const PathContext &context) const
{
  return Spectral{std::exp(-sigma_ext * segment.length)};
}
