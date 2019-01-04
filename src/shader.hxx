#ifndef SHADER_HXX
#define SHADER_HXX

#include <memory>

#include "shader_util.hxx"


struct TagScatterSample {};
using ScatterSample = Sample<Double3, Spectral3, TagScatterSample>;
  

// Included here because it uses ScatterSample.
#include"phasefunctions.hxx"


class Shader
{
  int flags;
public:
  Shader(int _flags = 0) : flags(_flags) {}
  virtual ~Shader() {}
  virtual ScatterSample SampleBSDF(const Double3 &incident_dir, const SurfaceInteraction &surface_hit, Sampler& sampler, const PathContext &context) const = 0;
  virtual Spectral3 EvaluateBSDF(const Double3 &incident_dir, const SurfaceInteraction &surface_hit, const Double3 &out_direction, const PathContext &context, double *pdf) const = 0;
  virtual double Pdf(const Double3 &incident_dir, const SurfaceInteraction &surface_hit, const Double3 &out_direction, const PathContext &context) const;
};


class DiffuseShader : public Shader
{
  SpectralN kr_d; // between zero and 1/Pi.
  std::unique_ptr<Texture> diffuse_texture; // TODO: Share textures among shaders?
public:
  DiffuseShader(const SpectralN &reflectance, std::unique_ptr<Texture> _diffuse_texture);
  ScatterSample SampleBSDF(const Double3 &incident_dir, const SurfaceInteraction &surface_hit, Sampler& sampler, const PathContext &context) const override;
  Spectral3 EvaluateBSDF(const Double3 &incident_dir, const SurfaceInteraction& surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const override;
};



class SpecularReflectiveShader : public Shader
{
  SpectralN kr_s;
public:
  SpecularReflectiveShader(const SpectralN &reflectance);
  ScatterSample SampleBSDF(const Double3 &incident_dir, const SurfaceInteraction &surface_hit, Sampler& sampler, const PathContext &context) const override;
  Spectral3 EvaluateBSDF(const Double3 &incident_dir, const SurfaceInteraction& surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const override;
};


class MicrofacetShader : public Shader
{
  SpectralN kr_s;
  double alpha_max;
  std::unique_ptr<Texture> glossy_exponent_texture;
public:
  MicrofacetShader(
    const SpectralN &_glossy_reflectance,
    double _glossy_exponent,
    std::unique_ptr<Texture> _glossy_exponent_texture
  );
  ScatterSample SampleBSDF(const Double3 &reverse_incident_dir, const SurfaceInteraction &surface_hit, Sampler& sampler, const PathContext &context) const override;
  Spectral3 EvaluateBSDF(const Double3 &reverse_incident_dir, const SurfaceInteraction& surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const override;
};


class SpecularTransmissiveDielectricShader : public Shader
{
  double ior_ratio; // Inside ior / Outside ior
public:
  SpecularTransmissiveDielectricShader(double _ior_ratio);
  ScatterSample SampleBSDF(const Double3 &reverse_incident_dir, const SurfaceInteraction &surface_hit, Sampler& sampler, const PathContext &context) const override;
  Spectral3 EvaluateBSDF(const Double3 &reverse_incident_dir, const SurfaceInteraction& surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const override;
};


// Purely refracting shader. Unphysical but useful for testing.
class SpecularPureRefractiveShader : public Shader
{
  double ior_ratio;
public:
  SpecularPureRefractiveShader(double _ior_ratio);
  ScatterSample SampleBSDF(const Double3 &reverse_incident_dir, const SurfaceInteraction &surface_hit, Sampler& sampler, const PathContext &context) const override;
  Spectral3 EvaluateBSDF(const Double3 &reverse_incident_dir, const SurfaceInteraction& surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const override;    
};


class SpecularDenseDielectricShader : public Shader
{
  // Certainly, the compiler is going to de-virtualize calls to these members?!
  DiffuseShader diffuse_part;
  double specular_reflectivity;
public:
  SpecularDenseDielectricShader(
    const double _specular_reflectivity,
    const SpectralN &_diffuse_reflectivity,
    std::unique_ptr<Texture> _diffuse_texture);
  ScatterSample SampleBSDF(const Double3 &reverse_incident_dir, const SurfaceInteraction &surface_hit, Sampler& sampler, const PathContext &context) const override;
  Spectral3 EvaluateBSDF(const Double3 &reverse_incident_dir, const SurfaceInteraction& surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const override;    
};


class InvisibleShader : public Shader
{
public:
  InvisibleShader() {}
  ScatterSample SampleBSDF(const Double3 &incident_dir, const SurfaceInteraction &surface_hit, Sampler& sampler, const PathContext &context) const override;
  Spectral3 EvaluateBSDF(const Double3 &incident_dir, const SurfaceInteraction& surface_hit, const Double3& out_direction, const PathContext &context, double *pdf) const override;
};



class Medium
{
public:
  struct InteractionSample
  {
    double t;
    // Following PBRT pg 893, the returned weight is either
    // beta_surf = T(t_intersect)/p_surf if the sampled t lies beyond the end of the ray, i.e. t > t_intersect, or
    // beta_med = sigma_s(t) T(t) / p(t) 
    Spectral3 weight;
  };
  using PhaseSample = ScatterSample;
  
  const int priority;
  Medium(int _priority) : priority(_priority) {}
  virtual ~Medium() {}
  virtual InteractionSample SampleInteractionPoint(const RaySegment &segment, Sampler &sampler, const PathContext &context) const = 0;
  virtual Spectral3 EvaluateTransmission(const RaySegment &segment, Sampler &sampler, const PathContext &context) const = 0;
  virtual VolumePdfCoefficients ComputeVolumePdfCoefficients(const RaySegment &segment, const PathContext &context) const = 0; // Can be approximate. Deterministic.
  virtual PhaseSample SamplePhaseFunction(const Double3 &incident_dir, const Double3 &pos, Sampler &sampler, const PathContext &context) const = 0;
  virtual Spectral3 EvaluatePhaseFunction(const Double3 &indcident_dir, const Double3 &pos, const Double3 &out_direction, const PathContext &context, double *pdf) const = 0;
};



class VacuumMedium : public Medium
{
public:
  VacuumMedium() : Medium(-1) {}
  virtual InteractionSample SampleInteractionPoint(const RaySegment &segment, Sampler &sampler, const PathContext &context) const override;
  virtual Spectral3 EvaluateTransmission(const RaySegment &segment, Sampler &sampler, const PathContext &context) const override;
  virtual VolumePdfCoefficients ComputeVolumePdfCoefficients(const RaySegment &segment, const PathContext &context) const override;
  virtual PhaseSample SamplePhaseFunction(const Double3 &incident_dir, const Double3 &pos, Sampler &sampler, const PathContext &context) const override;
  virtual Spectral3 EvaluatePhaseFunction(const Double3 &indcident_dir, const Double3 &pos, const Double3 &out_direction, const PathContext &context, double *pdf) const override;
};


class HomogeneousMedium : public Medium
{
  SpectralN sigma_s, sigma_a, sigma_ext;
  Spectral3 EvaluateTransmission(double x, const Spectral3 &sigma_ext) const;
public:
  std::unique_ptr<PhaseFunctions::PhaseFunction> phasefunction; // filled by parser
public:
  HomogeneousMedium(const SpectralN &_sigma_s, const SpectralN &_sigma_a, int _priority); 
  virtual InteractionSample SampleInteractionPoint(const RaySegment &segment, Sampler &sampler, const PathContext &context) const override;
  virtual Spectral3 EvaluateTransmission(const RaySegment &segment, Sampler &sampler, const PathContext &context) const override;
  virtual VolumePdfCoefficients ComputeVolumePdfCoefficients(const RaySegment &segment, const PathContext &context) const override;
  virtual PhaseSample SamplePhaseFunction(const Double3 &incident_dir, const Double3 &pos, Sampler &sampler, const PathContext &context) const override;
  virtual Spectral3 EvaluatePhaseFunction(const Double3 &indcident_dir, const Double3 &pos, const Double3 &out_direction, const PathContext &context, double *pdf) const override;
};


class MonochromaticHomogeneousMedium : public Medium
{
  double sigma_s, sigma_a, sigma_ext;
public:
  std::unique_ptr<PhaseFunctions::PhaseFunction> phasefunction;
public:
  MonochromaticHomogeneousMedium(double _sigma_s, double _sigma_a, int _priority); 
  virtual InteractionSample SampleInteractionPoint(const RaySegment &segment, Sampler &sampler, const PathContext &context) const override;
  virtual Spectral3 EvaluateTransmission(const RaySegment &segment, Sampler &sampler, const PathContext &context) const override;
  virtual VolumePdfCoefficients ComputeVolumePdfCoefficients(const RaySegment &segment, const PathContext &context) const override;
  virtual PhaseSample SamplePhaseFunction(const Double3 &incident_dir, const Double3 &pos, Sampler &sampler, const PathContext &context) const override;
  virtual Spectral3 EvaluatePhaseFunction(const Double3 &indcident_dir, const Double3 &pos, const Double3 &out_direction, const PathContext &context, double *pdf) const override;
};


#endif
