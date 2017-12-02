#pragma once
#include "vec3f.hxx"
#include "spectral.hxx"
#include "ray.hxx"
#include "sampler.hxx"

namespace RadianceOrImportance
{

struct Sample
{
  // If "is_direction" is true then pos represents a solid angle.
  Double3 pos;
  double pdf;
  Spectral measurement_contribution;
  bool is_direction;
};

struct DirectionalSample
{
  Ray ray_out;
  double pdf;
  Spectral measurement_contribution;
};

class EmitterSensor
{
public:
  virtual ~EmitterSensor() {}
  virtual Sample TakePositionSample(Sampler &sampler) const = 0;
  virtual DirectionalSample TakeDirectionSampleFrom(const Double3 &pos, Sampler &sampler) const = 0;
  virtual Spectral EvaluatePositionComponent(const Double3 &pos, double *pdf) const = 0;
  virtual Spectral EvaluateDirectionComponent(const Double3 &pos, const Double3 &dir_out, double *pdf) const = 0;
};


class EmitterSensorArray
{
public:
  const int num_units;
  struct Response
  {
    Spectral measurement_contribution;
    int unit_index;
    double pdf;
  };
  EmitterSensorArray(int _num_units) : num_units(_num_units) {}
  virtual ~EmitterSensorArray() {}
  virtual Sample TakePositionSample(int unit_index, Sampler &sampler) const = 0;
  virtual DirectionalSample TakeDirectionSampleFrom(int unit_index, const Double3 &pos, Sampler &sampler) const = 0;
  virtual void Evaluate(const Double3 &pos_on_this, const Double3 &dir_out, std::vector<Response> &responses) const = 0;
};


inline DirectionalSample TakeRaySample(const EmitterSensorArray &thing, int unit_index, Sampler &sampler)
{
  auto smpl_pos = thing.TakePositionSample(unit_index, sampler);
  auto smpl_dir = thing.TakeDirectionSampleFrom(unit_index, smpl_pos.pos, sampler);
  smpl_dir.pdf *= smpl_pos.pdf;
  smpl_dir.measurement_contribution *= smpl_pos.measurement_contribution;
  return smpl_dir;
}


}
