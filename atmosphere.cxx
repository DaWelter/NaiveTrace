#include "atmosphere.hxx"
#include "ray.hxx"
#include "sampler.hxx"
#include "phasefunctions.hxx"

namespace Atmosphere
{

Simple::Simple(const Double3& _planet_center, double _planet_radius, int _priority)
  : Medium(_priority),
    geometry{_planet_center, _planet_radius},
    constituents{}
{

}


namespace TrackingDetail
{
inline bool RussianRouletteSurvival(double &weight, int iteration, Sampler &sampler)
{
  assert (weight > -0.1); // Should be non-negative, really.
  if (weight <= 0.)
    return false;
  if (iteration < 5)
    return true;
  double prob_survival = std::min(weight, 1.);
  if (sampler.Uniform01() < prob_survival)
  {
    weight /= prob_survival;
    return true;
  }
  else
    return false;
}
}


Medium::InteractionSample Simple::SampleInteractionPoint(const RaySegment &segment, Sampler &sampler, const PathContext &context) const
{
  // Select a wavelength
  assert(!context.beta.isZero());
  Spectral lambda_selection_prob = context.beta.abs();
  lambda_selection_prob /= lambda_selection_prob.sum();
  int component = TowerSampling<static_size<Spectral>()>(
        lambda_selection_prob.data(),
        sampler.Uniform01());
  // Initialize weight
  Medium::InteractionSample smpl{
    0.,
    Spectral{0.}
  };
  smpl.weight[component] = 1./lambda_selection_prob[component];
  // The lowest point gives the largest collision coefficients along the path.
  auto lowest_point = geometry.ComputeLowestPointAlong(segment);
  // Delta/Woodcock Tracking
  // First compute the majorante
  double sigma_s, sigma_a, sigma_n;
  constituents.ComputeCollisionCoefficients(
        geometry.ComputeAltitude(lowest_point), component, sigma_s, sigma_a);
  double sigma_t_majorant = sigma_a + sigma_s;
  double inv_sigma_t_majorant = 1./sigma_t_majorant;
  // Then start tracking.
  // - No russian roulette because the probability to arrive
  // beyond the end of the segment must be identical to the transmissivity.
  // - Need to limit the number of iterations to protect against evil edge
  // cases where a particles escapes outside of the scene boundaries but
  // due to incorrect collisions the path tracer thinks that we are in a medium.
  constexpr int emergency_abort_max_num_iterations = 100;
  int iteration = 0;
  while (++iteration)
  {
    if (iteration > emergency_abort_max_num_iterations)
    {
      smpl.weight[component] = 0.;
      return smpl;
    }

    smpl.t -=std::log(sampler.Uniform01()) * inv_sigma_t_majorant;
    if (smpl.t > segment.length)
    {
      break;
    }
    else
    {
      const Double3 pos = segment.ray.PointAt(smpl.t);
      constituents.ComputeCollisionCoefficients(
            geometry.ComputeAltitude(pos), component, sigma_s, sigma_a);
      sigma_n = sigma_t_majorant - sigma_s - sigma_a;
      assert(sigma_n >= -1.e-3); // By definition of the majorante
      double r = sampler.Uniform01();
      if (r < sigma_a * inv_sigma_t_majorant)
      {
        smpl.weight[component] = 0.; // Absorption
        return smpl;
      }
      else if (r < (1. - sigma_n * inv_sigma_t_majorant))
      {
        return smpl; // Scattering
      }
      // Else we have a null collision.
    }
  }
  // At this point the particle escaped beyond the endpoint of the ray segment.
  // The sample weight must equal T(segment.length)/prob(t > segment.length)
  // Fortunately the above version of delta tracking distributes samples according
  // to p(t) = sigma_t*T(t), which ultimately yields a weight of 1.
  return smpl;
}


Spectral Simple::EvaluateTransmission(const RaySegment &segment, Sampler &sampler, const PathContext &context) const
{
  // The lowest point gives the largest collision coefficients along the path.
  auto lowest_point = geometry.ComputeLowestPointAlong(segment);
  double lowest_altitude = geometry.ComputeAltitude(lowest_point);
  Spectral estimate{1.};
  for (int lambda = 0; lambda < static_size<Spectral>(); ++lambda)
  {
    if (context.beta[lambda] == 0.)
    {
      // No point computing something when the path weight of this wavelength is already zero.
      // Which is the case when we fall back to single wavelengh sampling.
      estimate[lambda] = 0.;
    }
    else
    {
      // First compute the majorante
      double sigma_s, sigma_a, sigma_n;
      constituents.ComputeCollisionCoefficients(
            lowest_altitude, lambda, sigma_s, sigma_a);
      double sigma_t_majorant = sigma_a + sigma_s;
      double inv_sigma_t_majorant = 1./sigma_t_majorant;
      // Then start tracking.
      double t = 0.;
      int iteration = 0;
      do
      {
        t -=std::log(sampler.Uniform01()) * inv_sigma_t_majorant;
        if (t > segment.length)
        {
          break;
        }
        else
        {
          const Double3 pos = segment.ray.PointAt(t);
          constituents.ComputeCollisionCoefficients(
                geometry.ComputeAltitude(pos), lambda, sigma_s, sigma_a);
          sigma_n = sigma_t_majorant - sigma_s - sigma_a;
          assert(sigma_n >= -1.e-3); // By definition of the majorante
          estimate[lambda] *= sigma_n * inv_sigma_t_majorant;
        }
      }
      while (TrackingDetail::RussianRouletteSurvival(estimate[lambda], iteration++, sampler));
    } // contribution of this wavelength is nonzero?
  }
  return estimate;
}


void Simple::ComputeProbabilities(const Double3 &pos, Spectral &prob_lambda, Spectral *prob_constituent_given_lambda) const
{
  constexpr int NL = static_size<Spectral>();
  constexpr int NC = SimpleConstituents::NUM_CONSTITUENTS;
  double altitude = geometry.ComputeAltitude(pos);

  double prob_lambda_normalization = 0.;

  constituents.ComputeSigmaS(altitude, prob_constituent_given_lambda);
  for (int lambda = 0; lambda<NL; ++lambda)
  {
    double normalization = 0.;
    for (int c=0; c<NC; ++c)
    {
      normalization += prob_constituent_given_lambda[c][lambda];
    }
    prob_lambda[lambda] = normalization;
    assert(normalization > 0.);
    prob_lambda_normalization += normalization;
    for (int c=0; c<NC; ++c)
    {
      prob_constituent_given_lambda[c][lambda] /= normalization;
    }
  }
  assert(prob_lambda_normalization > 0.);
  for (int lambda = 0; lambda<NL; ++lambda)
  {
    prob_lambda[lambda] /= prob_lambda_normalization;
  }
}


Medium::PhaseSample Simple::SamplePhaseFunction(const Double3 &incident_dir, const Double3 &pos, Sampler &sampler) const
{
  constexpr int NL = static_size<Spectral>();
  constexpr int NC = SimpleConstituents::NUM_CONSTITUENTS;

  Spectral prob_lambda;
  Spectral prob_constituent_given_lambda[NC];
  ComputeProbabilities(pos, prob_lambda, prob_constituent_given_lambda);

  int lambda = TowerSampling<NL>(prob_lambda.data(), sampler.Uniform01());
  double contiguous_probs[NC] = {
    prob_constituent_given_lambda[0][lambda],
    prob_constituent_given_lambda[1][lambda]
  };
  int constituent = TowerSampling<NC>(contiguous_probs, sampler.Uniform01());

  double pf_pdf[SimpleConstituents::NUM_CONSTITUENTS];
  Medium::PhaseSample smpl =
      constituent==SimpleConstituents::MOLECULES ?
        constituents.phasefunction_rayleigh.SampleDirection(incident_dir, pos, sampler) :
        constituents.phasefunction_hg.SampleDirection(incident_dir, pos, sampler);
  pf_pdf[constituent] = smpl.pdf;
  constituent==SimpleConstituents::MOLECULES ?
    constituents.phasefunction_hg.Evaluate(incident_dir, pos, smpl.dir, &pf_pdf[SimpleConstituents::AEROSOLES]) :
    constituents.phasefunction_rayleigh.Evaluate(incident_dir, pos, smpl.dir, &pf_pdf[SimpleConstituents::MOLECULES]);
  smpl.value *= prob_constituent_given_lambda[constituent];
  smpl.pdf = 0.;
  for (int c = 0; c<NC; ++c)
  {
    for (int lambda = 0; lambda<NL; ++lambda)
    {
      smpl.pdf += pf_pdf[c]*prob_lambda[lambda]*prob_constituent_given_lambda[c][lambda];
    }
  }
  return smpl;
}


Spectral Simple::EvaluatePhaseFunction(const Double3 &incident_dir, const Double3 &pos, const Double3 &out_direction, double *pdf) const
{
  constexpr int NL = static_size<Spectral>();
  constexpr int NC = SimpleConstituents::NUM_CONSTITUENTS;

  Spectral prob_lambda;
  Spectral prob_constituent_given_lambda[NC];
  ComputeProbabilities(pos, prob_lambda, prob_constituent_given_lambda);

  if (pdf)
    *pdf = 0.;
  Spectral result{0.};
  double pf_pdf[NC];
  Spectral pf_val[NC];
  pf_val[SimpleConstituents::AEROSOLES] = constituents.phasefunction_hg.Evaluate(incident_dir, pos, out_direction, &pf_pdf[SimpleConstituents::AEROSOLES]);
  pf_val[SimpleConstituents::MOLECULES] = constituents.phasefunction_rayleigh.Evaluate(incident_dir, pos, out_direction, &pf_pdf[SimpleConstituents::MOLECULES]);
  for (int c = 0; c<NC; ++c)
  {
    if (pdf) for (int lambda = 0; lambda<NL; ++lambda)
    {
      *pdf += pf_pdf[c]*prob_lambda[lambda]*prob_constituent_given_lambda[c][lambda];
    }
    result += prob_constituent_given_lambda[c]*pf_val[c];
  }
  return result;
}



SimpleConstituents::SimpleConstituents()
  : phasefunction_hg(0.76),
    lower_altitude_cutoff(0.)
{
  inv_scale_height[MOLECULES] = 1./8.; // km
  inv_scale_height[AEROSOLES] = 1./1.2;  // km
  at_sealevel[MOLECULES].sigma_a = Spectral{0};
  at_sealevel[MOLECULES].sigma_s = 1.e-3 * Spectral{5.8, 13.5, 33.1};
  at_sealevel[AEROSOLES].sigma_a = 1.e-3 * Spectral{2.22};
  at_sealevel[AEROSOLES].sigma_s = 1.e-3 * Spectral{20.};
  for (auto inv_h : inv_scale_height)
    lower_altitude_cutoff = std::max(-1./inv_h, lower_altitude_cutoff);
}

}