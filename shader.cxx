#include "shader.hxx"
#include "ray.hxx"
#include "scene.hxx"
#include "shader_util.hxx"

namespace ShadingInternal
{

using Real = double;
using Real2 = Vec<Real,2>;
using Real3 = Vec<Real,3>;
constexpr Real operator"" _r(long double d) { return Real(d); }
constexpr Real operator"" _r(unsigned long long int d) { return Real(d); }
static constexpr Real Pi_r = Real(::Pi);

struct BackmanDistribution
{
  Real alpha; // aka. roughness
  
  // This formula is using the normalized distribution D(cs) 
  // such that Int_omega D(cs) cs dw = 1, using the differential solid angle dw, 
  // integrated over the hemisphere.
  Real EvalByHalfVector(Real ns_dot_wh)
  {
    const Real cs = ns_dot_wh;
    const Real t1 = (cs*cs-1._r)/(cs*cs*alpha*alpha);
    const Real t2 = alpha*alpha*cs*cs*cs*cs*Pi_r;
    return std::exp(t1)/t2;
  }
  
  /* Samples the Beckman microfacet distribution D(m) times |m . n|. 
  * The surface normal n is assumed to be aligned with the z-axis.
  * Returns the half-angle vector m. 
  * Ref: Walter et al. (2007) "Microfacet Models for Refraction through Rough Surfaces" Eq. 28, 29. */
  Real3 SampleHalfVector(Real2 r)
  {
    const Real t1 = -alpha*alpha*std::log(r[0]);
    const Real t = 1_r/(t1+1_r);
    const Real z = std::sqrt(t);
    const Real rho = std::sqrt(1_r-t);
    const Real omega = 2_r*Pi_r*r[1];
    const Real sn = std::sin(omega);
    const Real cs = std::cos(omega);
    return Real3{cs*rho, sn*rho, z};
  }
};


Spectral3 MaybeMultiplyTextureLookup(const Spectral3 &color, const Texture *tex, const RaySurfaceIntersection &surface_hit, const Index3 &lambda_idx)
{
  Spectral3 ret{color};
  if (tex)
  {
    RGB col = tex->GetPixel(UvToPixel(*tex, surface_hit.tex_coord));
    ret *= Color::RGBToSpectralSelection(col, lambda_idx); // TODO: optimize, I don't have to compute the full spectrum.
  }
  return ret;
}


double MaybeMultiplyTextureLookup(double _value, const Texture *tex, const RaySurfaceIntersection &surface_hit)
{
  if (tex)
  {
    RGB col = tex->GetPixel(UvToPixel(*tex, surface_hit.tex_coord));
    _value *= (value(col[0])+value(col[1])+value(col[2]))/3_r;
  }
  return _value;
}


template<class T>
inline T SchlicksApproximation(const T &kspecular, Real n_dot_dir)
{
  // Ref: Siggraph 2012 Course. "Background: Physics and Math of Shading (Naty Hoffman)"
  //      http://blog.selfshadow.com/publications/s2012-shading-course/hoffman/s2012_pbs_physics_math_notes.pdf
  return kspecular + (1_r-kspecular)*std::pow(1_r-n_dot_dir, 5_r);
}


template<class T>
inline T AverageOfProjectedSchlicksApproximationOverHemisphere(const T &kspecular)
{
  // Computes I= 1/Pi * Int_HalfSphere F_schlick(w)*cos(theta) dw
  // The average albedo of ideal specular reflection.
  return kspecular + (1_r-kspecular)*2_r/42_r;
  // The 42 here is no joke. It comes out of Wolfram Alpha when ordered to compute:
  // integrate (1-cos(x))^5*cos(x)*sin(x) from 0 to pi/2
  // The factor two comes from the integration over the azimuthal angle.
}


Real HalfVectorPdfToExitantPdf(Real pdf_wh, Real wh_dot_in)
{
    Real out_direction_pdf = pdf_wh*0.25/(wh_dot_in+Epsilon); // From density of h_r to density of out direction.
    return out_direction_pdf;
}


Real3 HalfVectorToExitant(const Real3 &h_r, const Real3 &reverse_incident_dir)
{
  double hr_dot_in = Dot(reverse_incident_dir, h_r);
  // See walter 2007, eq. 38. // Should I use the "reflection" formula with the abs in it instead?
  // However, for that case I haven't found the correct pdf transform for the outgoing direction yet ...
//   if (hr_dot_in < 0.)
//   {
//     hr_dot_in = -hr_dot_in;
//     h_r = -h_r;
//   }
  Double3 out_direction = 2.*hr_dot_in*h_r - reverse_incident_dir;
//   if (hr_dot_in < 0.) // Only needed when using the "reflection" variant with std::abs in it.
//     out_direction = Normalized(out_direction);
  return out_direction;
}

}

using namespace ShadingInternal;


double Shader::Pdf(const Double3& incident_dir, const RaySurfaceIntersection& surface_hit, const Double3& out_direction, const PathContext& context) const
{
  RaySurfaceIntersection intersect{surface_hit};
  // Puke ... TODO: Abolish requirement of normal alignment with incident dir.
  if (Dot(incident_dir, intersect.normal) < 0.)
  {
    intersect.normal = -intersect.normal;
    intersect.shading_normal = -intersect.shading_normal;
  }
  // TODO: This should be implemented in each shader so that only the pdf is computed (?)!
  double pdf;
  this->EvaluateBSDF(incident_dir, intersect, out_direction, context, &pdf);
  return pdf;
}


DiffuseShader::DiffuseShader(const SpectralN &_reflectance, std::unique_ptr<Texture> _diffuse_texture)
  : Shader(),
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


Spectral3 DiffuseShader::EvaluateBSDF(const Double3 &incident_dir, const RaySurfaceIntersection &surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const
{
  Spectral3 ret{0.};
  assert (Dot(surface_hit.normal, incident_dir)>=0); // Because normal is aligned such that this conditions should be true.
  double n_dot_out = Dot(surface_hit.normal, out_direction);
  if (n_dot_out > 0.) // In/Out on same side of geometric surface?
  {
    Spectral3 kr_d_taken = Take(kr_d, context.lambda_idx);
    ret = MaybeMultiplyTextureLookup(kr_d_taken, diffuse_texture.get(), surface_hit, context.lambda_idx);
  }
  if (pdf)
  {
    *pdf = n_dot_out>0. ? n_dot_out/Pi : 0.;
  }
  return ret;
}


ScatterSample DiffuseShader::SampleBSDF(const Double3 &incident_dir, const RaySurfaceIntersection &surface_hit, Sampler& sampler, const PathContext &context) const
{
  auto m = OrthogonalSystemZAligned(surface_hit.normal);
  Double3 v = SampleTrafo::ToCosHemisphere(sampler.UniformUnitSquare());
  double pdf = v[2]/Pi;
  Double3 out_direction = m * v;
  Spectral3 value = Take(kr_d, context.lambda_idx);
  value = MaybeMultiplyTextureLookup(value, diffuse_texture.get(), surface_hit, context.lambda_idx);
  return ScatterSample{out_direction, value, pdf};
}



SpecularReflectiveShader::SpecularReflectiveShader(const SpectralN& reflectance)
  : Shader(),
    kr_s(reflectance)
{
}


Spectral3 SpecularReflectiveShader::EvaluateBSDF(const Double3& incident_dir, const RaySurfaceIntersection& surface_hit, const Double3& out_direction, const PathContext &context, double* pdf) const
{
  if (pdf)
    *pdf = 0.;
  return Spectral3{0.};
}


ScatterSample SpecularReflectiveShader::SampleBSDF(const Double3& incident_dir, const RaySurfaceIntersection& surface_hit, Sampler& sampler, const PathContext &context) const
{
  Double3 r = Reflected(incident_dir, surface_hit.shading_normal);
  double cos_rn = Dot(surface_hit.normal, r);
  
  auto NominalSample = [&]() -> ScatterSample
  {
    double cos_rsdn = Dot(surface_hit.shading_normal, r);
    auto kr_s_taken = Take(kr_s, context.lambda_idx);
    return ScatterSample{r, kr_s_taken/cos_rsdn, 1.};
  };
  
  ScatterSample smpl = (cos_rn < 0.) ?
    ScatterSample{r, Spectral3{0.}, 1.} :
    NominalSample();
  SetPmfFlag(smpl);
  return smpl;
}


inline bool OnSameSide(const Double3 &reverse_incident_dir, const RaySurfaceIntersection &surface_hit, const Double3 &other_dir)
{
  assert(Dot(reverse_incident_dir, surface_hit.normal) >= 0.);
  return Dot(other_dir, surface_hit.normal) >= 0.;
}


inline double FresnelReflectivity(
  double cs_i,  // cos theta of incident direction
  double cs_t,  // cos theta of refracted(!) direction. Must be >0.
  double eta_i_over_t
)
{
  // https://en.wikipedia.org/wiki/Fresnel_equations
  // I divide both nominator and denominator by eta_2
  assert (cs_i >= 0.); 
  assert (cs_t >= 0.); // Take std::abs(Dot(n,r), or flip normal.
  assert (eta_i_over_t > 0.);
  double rs_nom = eta_i_over_t * cs_i - cs_t;
  double rs_den = eta_i_over_t * cs_i + cs_t;
  double rp_nom = eta_i_over_t * cs_t - cs_i;
  double rp_den = eta_i_over_t * cs_t + cs_i;
  return 0.5*(Sqr(rs_nom/rs_den) + Sqr(rp_nom/rp_den));
}


SpecularTransmissiveDielectricShader::SpecularTransmissiveDielectricShader(double _ior_ratio) 
  : Shader{}, ior_ratio{_ior_ratio}
{
  
}


ScatterSample SpecularTransmissiveDielectricShader::SampleBSDF(const Double3 &reverse_incident_dir, const RaySurfaceIntersection &surface_hit, Sampler& sampler, const PathContext &context) const
{
  double shn_dot_i = std::abs(Dot(surface_hit.shading_normal, reverse_incident_dir));
  bool entering = Dot(surface_hit.geometry_normal, reverse_incident_dir) > 0.;
  double eta_i_over_t = entering ? 1./ior_ratio  : ior_ratio; // eta_i refers to ior on the side of the incomming random walk! 
  
  /* Citing Veach (pg. 147): "Specular BSDF’s contain Dirac distribu-
  tions, which means that the only allowable operation is sampling: there must be an explicit
  procedure that generates a sample direction and a weight. When the specular BSDF is not
  symmetric, the direction and/or weight computation for the adjoint is different, and thus
  there must be two different sampling procedures, or an explicit flag that specifies whether
  the direct or adjoint BSDF is being sampled."! */
  
  auto OneOverCosThetaLight = [&](double cos_theta_i, double cos_theta_o)
  {
    // Want to divide out the cos theta term with the incident direction of light!
    // return 1./(context.transport==RADIANCE ? cos_theta_o : cos_theta_i);
    
    // Probably the following is correct. It is what PBRT does. Also if I follow Veach with
    // his derivation on p.g. 171, but with dsigma instead of dsigma_perpendicular, I get this.
    // It also makes my tests not fail!
    return 1./cos_theta_o; 
  };
  
  double radiance_weight = (context.transport==RADIANCE) ? Sqr(eta_i_over_t) : 1.;
  
  double fresnel_reflectivity = 1.;
  boost::optional<Double3> wt = Refracted(reverse_incident_dir, surface_hit.shading_normal, eta_i_over_t);
  if (wt)
  {
    double abs_shn_dot_r = std::abs(Dot(*wt, surface_hit.shading_normal));
    fresnel_reflectivity = FresnelReflectivity(shn_dot_i,abs_shn_dot_r, eta_i_over_t);
  }
  assert (fresnel_reflectivity >= -0.00001 && fresnel_reflectivity <= 1.000001);
  
  const double pr_sample_reflect = fresnel_reflectivity;
  bool do_sample_reflection = sampler.Uniform01() < pr_sample_reflect;
  assert (do_sample_reflection || (bool)(wt));
  
  ScatterSample smpl;
  if (do_sample_reflection)
  {
    Double3 wr = Reflected(reverse_incident_dir, surface_hit.shading_normal);
    smpl.coordinates = wr;
    smpl.pdf_or_pmf = Pdf::MakeFromDelta(pr_sample_reflect);
    if (OnSameSide(reverse_incident_dir, surface_hit, wr))
    {
      double tmp = OneOverCosThetaLight(shn_dot_i, Dot(wr, surface_hit.shading_normal));
      smpl.value = Spectral3{fresnel_reflectivity*tmp};
    }
    else
      smpl.value = Spectral3{0.};
  }
  else // Transmission
  {
    smpl.coordinates = *wt;
    smpl.pdf_or_pmf = Pdf::MakeFromDelta(1.-pr_sample_reflect);
    if (OnSameSide(reverse_incident_dir, surface_hit, *wt))
      smpl.value = Spectral3{0.}; // Should be on other side of geometric surface, but we are not!
    else
    {
      double tmp = OneOverCosThetaLight(shn_dot_i, -Dot(*wt, surface_hit.shading_normal));
      smpl.value = Spectral3{(1.-fresnel_reflectivity)*tmp*radiance_weight};
    }
  }
  return smpl;
}


Spectral3 SpecularTransmissiveDielectricShader::EvaluateBSDF(const Double3 &reverse_incident_dir, const RaySurfaceIntersection& surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const
{
  if (pdf)
    *pdf = 0.;
  return Spectral3{0.};
}



SpecularPureRefractiveShader::SpecularPureRefractiveShader(double _ior_ratio) : Shader{}, ior_ratio{_ior_ratio}
{

}


ScatterSample SpecularPureRefractiveShader::SampleBSDF(const Double3& reverse_incident_dir, const RaySurfaceIntersection& surface_hit, Sampler& sampler, const PathContext& context) const
{
  ScatterSample smpl;
  double shn_dot_i = std::abs(Dot(surface_hit.shading_normal, reverse_incident_dir));
  bool entering = Dot(surface_hit.geometry_normal, reverse_incident_dir) > 0.;
  double eta_i_over_t = entering ? 1./ior_ratio  : ior_ratio; // eta_i refers to ior on the side of the incomming random walk! 
    
  double radiance_weight = (context.transport==RADIANCE) ? Sqr(eta_i_over_t) : 1.;
  
  boost::optional<Double3> wt = Refracted(reverse_incident_dir, surface_hit.shading_normal, eta_i_over_t);
  
  if (!wt) // Total reflection. Neglected!
  {
    smpl.value = {0.};
    smpl.pdf_or_pmf = Pdf::MakeFromDelta(1.);
    smpl.coordinates = reverse_incident_dir;
    return smpl;
  }
  smpl.coordinates = *wt;
  smpl.pdf_or_pmf = Pdf::MakeFromDelta(1.);
  if (OnSameSide(reverse_incident_dir, surface_hit, *wt))
    smpl.value = Spectral3{0.}; // Should be on other side of geometric surface, but we are not!
  else
  {
    smpl.value = Spectral3{-1./Dot(*wt, surface_hit.shading_normal)*radiance_weight};
  }
  return smpl;
}


Spectral3 SpecularPureRefractiveShader::EvaluateBSDF(const Double3& reverse_incident_dir, const RaySurfaceIntersection& surface_hit, const Double3& out_direction, const PathContext& context, double* pdf) const
{
  if (pdf)
    *pdf = 0.;
  return Spectral3{0.};
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
  const SpectralN &_glossy_reflectance,
  double _glossy_exponent,
  std::unique_ptr<Texture> _glossy_exponent_texture)
  : Shader(0),
    kr_s(_glossy_reflectance), 
    alpha_max(_glossy_exponent),
    glossy_exponent_texture(std::move(_glossy_exponent_texture))
{
}



Spectral3 MicrofacetShader::EvaluateBSDF(const Double3 &reverse_incident_dir, const RaySurfaceIntersection &surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const
{
  double n_dot_out = Dot(surface_hit.normal, out_direction);
  double ns_dot_out = std::abs(Dot(surface_hit.shading_normal, out_direction));
  double ns_dot_in  = std::abs(Dot(surface_hit.shading_normal, reverse_incident_dir));
  Double3 half_angle_vector = Normalized(reverse_incident_dir + out_direction);
  // Note: half_angle_vector is NaN whenever I evalute straight through transmission.

  double ns_dot_wh = Dot(surface_hit.shading_normal, half_angle_vector);
  double wh_dot_out = std::abs(Dot(out_direction, half_angle_vector));
  double wh_dot_in  = std::abs(Dot(reverse_incident_dir, half_angle_vector)); // Do I need this? It is the same as wh_dot_out, no?
  //assert(wh_dot_out >= 0.);
  //assert(wh_dot_in >= 0.);
  
  double microfacet_alpha = MaybeMultiplyTextureLookup(alpha_max, glossy_exponent_texture.get(), surface_hit);
  double microfacet_distribution_val = BackmanDistribution{microfacet_alpha}.EvalByHalfVector(ns_dot_wh);
  
  if (pdf)
  {    
    double half_angle_distribution_val = microfacet_distribution_val*std::abs(ns_dot_wh);
    double out_distribution_val = HalfVectorPdfToExitantPdf(half_angle_distribution_val, wh_dot_in);
    *pdf = out_distribution_val;
  }
 
  if (n_dot_out <= 0.) // Not on same side of geometric surface?
  {
    return Spectral3{0.};
  }

  // Half vector is on positive side of shading surface, else zero contribution.
  // This is essential when shading normals are involved, since then, indeed the
  // half vector can point below the surface. It can also happen by the random
  // sampling process of the outgoing direction.
  microfacet_distribution_val *= Heaviside(ns_dot_wh);
  
  double geometry_term;
  {
#if 1
    // Cook & Torrance model. Seems to work good enough although Walter et al. has concerns about it's realism.
    // Ref: Cook and Torrance (1982) "A reflectance model for computer graphics"
    double t1 = 2.*ns_dot_wh*ns_dot_out / (wh_dot_out + Epsilon);
    double t2 = 2.*ns_dot_wh*ns_dot_in / (wh_dot_out + Epsilon);
    geometry_term = std::min(1., std::min(t1, t2));
#else
    // Overkill?
    geometry_term = MicrofacetDetail::G1(wh_dot_in, ns_dot_in, alpha)*
                    MicrofacetDetail::G1(wh_dot_out, ns_dot_out, alpha);
#endif
  }
 
  
  Spectral3 kr_s_taken = Take(kr_s, context.lambda_idx);
  Spectral3 fresnel_term = SchlicksApproximation(kr_s_taken, wh_dot_in);
  assert (fresnel_term.allFinite());
  double monochromatic_terms = geometry_term*microfacet_distribution_val*0.25/(ns_dot_in*ns_dot_out+Epsilon);
  assert(std::isfinite(monochromatic_terms));
  return monochromatic_terms*fresnel_term;
}



ScatterSample MicrofacetShader::SampleBSDF(const Double3 &reverse_incident_dir, const RaySurfaceIntersection &surface_hit, Sampler& sampler, const PathContext &context) const
{
  auto m = OrthogonalSystemZAligned(surface_hit.shading_normal);
  double alpha = MaybeMultiplyTextureLookup(alpha_max, glossy_exponent_texture.get(), surface_hit);
  Double3 h_r_local = BackmanDistribution{alpha}.SampleHalfVector(sampler.UniformUnitSquare());
  Double3 h_r = m*h_r_local;
  Double3 out_direction = HalfVectorToExitant(h_r, reverse_incident_dir);
  ScatterSample smpl; 
  smpl.coordinates = out_direction;
  ASSERT_NORMALIZED(out_direction);
  double pdf = NaN;
  smpl.value = this->EvaluateBSDF(reverse_incident_dir, surface_hit, out_direction, context, &pdf);
  smpl.pdf_or_pmf = pdf;
  return smpl;
}




Spectral3 InvisibleShader::EvaluateBSDF(const Double3 &incident_dir, const RaySurfaceIntersection &surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const
{
  constexpr double tol = Epsilon;
  double u = LengthSqr(incident_dir + out_direction);
  u = u<tol ? 1. : 0.;
  if (pdf)
    *pdf = u;
  return Spectral3{u};
}


ScatterSample InvisibleShader::SampleBSDF(const Double3 &incident_dir, const RaySurfaceIntersection &surface_hit, Sampler& sampler, const PathContext &context) const
{
  return ScatterSample{-incident_dir, Spectral3{1.}, 1.};
}






SpecularDenseDielectricShader::SpecularDenseDielectricShader(const double _specular_reflectivity, const SpectralN& _diffuse_reflectivity, std::unique_ptr<Texture> _diffuse_texture): 
  Shader(), diffuse_part{_diffuse_reflectivity, std::move(_diffuse_texture)}, specular_reflectivity{_specular_reflectivity}
{
  
}


namespace SmoothAndDenseDielectricDetail
{
// Symmetry demands that f(w1,w2)=f(w2,w1). Therefore, following, Klemen & Kalos, I use the factors 
// (1-R(w1))*(1-R(w2)) where R(w) is the reflective albedo of the specular part given and w the incidence direction. 
// Ref: Klemen & Kalos (2001) "A Microfacet Based Coupled Specular-Matte BRDF Model with Importance Sampling", 
  
double DiffuseAttenuationFactor(double albedo1, double albedo2, double average_albedo) 
{
  assert(0 <= average_albedo && average_albedo <= 1.);
  assert(0 <= albedo1 && albedo1 <= 1.);
  assert(0 <= albedo2 && albedo2 <= 1.);
  double normalization = 1./(1.-average_albedo);
  // Another factor 1/Pi comes from the normalization built into the Diffuse shader class.
  return (1.-albedo1)*(1.-albedo2)*normalization;
}
}


Spectral3 SpecularDenseDielectricShader::EvaluateBSDF(const Double3& reverse_incident_dir, const RaySurfaceIntersection& surface_hit, const Double3& out_direction, const PathContext& context, double* pdf) const
{ 
  double cos_out_n = Dot(surface_hit.normal, out_direction);
  if (cos_out_n > 0.)
  {
    double cos_shn_exitant = std::max(0., Dot(surface_hit.shading_normal, out_direction));
    double cos_shn_incident = std::max(0., Dot(surface_hit.shading_normal, reverse_incident_dir));
    
    double reflected_fraction = SchlicksApproximation(specular_reflectivity, cos_shn_incident);
    double other_reflection_term = SchlicksApproximation(specular_reflectivity, cos_shn_exitant);  
    double average_albedo = AverageOfProjectedSchlicksApproximationOverHemisphere<double>(specular_reflectivity);
   
    Spectral3 brdf_value = diffuse_part.EvaluateBSDF(reverse_incident_dir, surface_hit, out_direction, context, pdf); 
    brdf_value *= SmoothAndDenseDielectricDetail::DiffuseAttenuationFactor(
      reflected_fraction, other_reflection_term, average_albedo);
    
    if (pdf)
      *pdf *= (1.-reflected_fraction);
    return brdf_value;
  }
  else
  {
    if (pdf)
      *pdf = 0.;
    return Spectral3{0.};
  }
}



ScatterSample SpecularDenseDielectricShader::SampleBSDF(const Double3& reverse_incident_dir, const RaySurfaceIntersection& surface_hit, Sampler& sampler, const PathContext& context) const
{
  double cos_shn_incident = std::max(0., Dot(surface_hit.shading_normal, reverse_incident_dir));
  double reflected_fraction = SchlicksApproximation(specular_reflectivity, cos_shn_incident);
  assert(reflected_fraction >= 0. && reflected_fraction <= 1.);
  double decision_var = sampler.Uniform01();
  ScatterSample smpl;
  if (decision_var < reflected_fraction)
  {
    Double3 refl_dir = Reflected(reverse_incident_dir, surface_hit.shading_normal);
    double cos_rn = Dot(surface_hit.normal, refl_dir);
    if (cos_rn >= 0.)
    {
      smpl = ScatterSample{refl_dir, Spectral3{reflected_fraction/(cos_shn_incident+Epsilon)}, reflected_fraction};
    }
    else
    {
      smpl = ScatterSample{refl_dir, Spectral3{0.}, reflected_fraction};
    }
    SetPmfFlag(smpl);
  }
  else
  {
    smpl = diffuse_part.SampleBSDF(reverse_incident_dir, surface_hit, sampler, context);
    double cos_n_exitant = std::max(0., Dot(surface_hit.shading_normal, smpl.coordinates));
    double other_reflection_term = SchlicksApproximation(specular_reflectivity, cos_n_exitant);
    double average_albedo = AverageOfProjectedSchlicksApproximationOverHemisphere<double>(specular_reflectivity);
    smpl.value *= SmoothAndDenseDielectricDetail::DiffuseAttenuationFactor(
      reflected_fraction, other_reflection_term, average_albedo);
    smpl.pdf_or_pmf *= (1.-reflected_fraction);
  }
  return smpl;
}



/*************************************
 * Media
 ***********************************/


Spectral3 VacuumMedium::EvaluatePhaseFunction(const Double3& indcident_dir, const Double3& pos, const Double3& out_direction, const PathContext &context, double* pdf) const
{
  if (pdf)
    *pdf = 1.;
  return Spectral3{0.}; // Because it is a delta function.
}


Medium::InteractionSample VacuumMedium::SampleInteractionPoint(const RaySegment& segment, Sampler& sampler, const PathContext &context) const
{
  return Medium::InteractionSample{
      LargeNumber,
      Spectral3{1.}
    };
}


ScatterSample VacuumMedium::SamplePhaseFunction(const Double3& incident_dir, const Double3& pos, Sampler& sampler, const PathContext &context) const
{
  return ScatterSample{
    -incident_dir,
    Spectral3{1.},
    1.
  };
}


Spectral3 VacuumMedium::EvaluateTransmission(const RaySegment& segment, Sampler &sampler, const PathContext &context) const
{
  return Spectral3{1.};
}


VolumePdfCoefficients VacuumMedium::ComputeVolumePdfCoefficients(const RaySegment &segment, const PathContext &context) const
{
  return VolumePdfCoefficients{
    0.,
    0.,
    1.
  };
}



HomogeneousMedium::HomogeneousMedium(const SpectralN& _sigma_s, const SpectralN& _sigma_a, int priority)
  : Medium(priority), sigma_s{_sigma_s}, sigma_a{_sigma_a}, sigma_ext{sigma_s + sigma_a},
    phasefunction{new PhaseFunctions::Uniform()}
{
}


Spectral3 HomogeneousMedium::EvaluatePhaseFunction(const Double3& incident_dir, const Double3& pos, const Double3& out_direction, const PathContext &context, double* pdf) const
{
  return phasefunction->Evaluate(incident_dir, out_direction, pdf);
}


ScatterSample HomogeneousMedium::SamplePhaseFunction(const Double3& incident_dir, const Double3& pos, Sampler& sampler, const PathContext &context) const
{
  return phasefunction->SampleDirection(incident_dir, sampler);
}


Medium::InteractionSample HomogeneousMedium::SampleInteractionPoint(const RaySegment& segment, Sampler& sampler, const PathContext &context) const
{
  Medium::InteractionSample smpl{
    0.,
    Spectral3{1.}
  };
  // Shadow the member var by the new var taking only the current lambdas.
  Spectral3 sigma_ext = Take(this->sigma_ext, context.lambda_idx);
  Spectral3 sigma_s   = Take(this->sigma_s,   context.lambda_idx);
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
      Spectral3 sigma_n = sigma_t_majorant - sigma_ext;
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


inline Spectral3 HomogeneousMedium::EvaluateTransmission(double x, const Spectral3& sigma_ext) const
{
  return (-sigma_ext * x).exp();
}


Spectral3 HomogeneousMedium::EvaluateTransmission(const RaySegment& segment, Sampler &sampler, const PathContext &context) const
{
  Spectral3 sigma_ext = Take(this->sigma_ext, context.lambda_idx);
  return EvaluateTransmission(segment.length, sigma_ext);
}


VolumePdfCoefficients HomogeneousMedium::ComputeVolumePdfCoefficients(const RaySegment &segment, const PathContext &context) const
{
  // We take the mean over the densities that would be appropriate for single-lambda sampling. So this is only approximate.
  // With Peter Kutz's spectral tracking method the actual pdf is not accessible in closed form.
   Spectral3 sigma_ext = Take(this->sigma_ext, context.lambda_idx);
   double tr = EvaluateTransmission(segment.length, sigma_ext).mean();
   double e  = sigma_ext.mean();
   return VolumePdfCoefficients{
     e*tr,
     e*tr,
     tr,
   }; // Forward and backward is the same in homogeneous media.
}





MonochromaticHomogeneousMedium::MonochromaticHomogeneousMedium(double _sigma_s, double _sigma_a, int priority)
  : Medium(priority), sigma_s{_sigma_s}, sigma_a{_sigma_a}, sigma_ext{_sigma_s + _sigma_a},
    phasefunction{new PhaseFunctions::Uniform()}
{
}


Spectral3 MonochromaticHomogeneousMedium::EvaluatePhaseFunction(const Double3& indcident_dir, const Double3& pos, const Double3& out_direction, const PathContext &context, double* pdf) const
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


VolumePdfCoefficients MonochromaticHomogeneousMedium::ComputeVolumePdfCoefficients(const RaySegment &segment, const PathContext &context) const
{
  double tr = std::exp(-sigma_ext*segment.length);
  return VolumePdfCoefficients{
    sigma_ext*tr,
    sigma_ext*tr,
    tr,
  }; // Forward and backward is the same in homogeneous media.
}

Spectral3 MonochromaticHomogeneousMedium::EvaluateTransmission(const RaySegment& segment, Sampler &sampler, const PathContext &context) const
{
  return Spectral3{std::exp(-sigma_ext * segment.length)};
}
