#pragma once

#include "scene.hxx"
#include "shader.hxx"
#include "light.hxx"
#include "util.hxx"


class LightPickerCommon
{
protected:
  const Scene &scene;
  
  inline static constexpr int IDX_PROB_ENV = 0;
  inline static constexpr int IDX_PROB_AREA = 1;
  inline static constexpr int IDX_PROB_POINT = 2;
  inline static constexpr int IDX_PROB_VOLUME = 3;
  inline static constexpr int NUM_LIGHT_TYPES = 4;

  LightPickerCommon(const Scene &scene);
};


using  AlgorithmParameters = RenderingParameters;
namespace ROI = RadianceOrImportance;

/* Selects lights randomly with uniform probability distributions. */
class TrivialLightPicker : public LightPickerCommon
{
  std::array<double, 4> emitter_type_selection_probabilities;
  std::array<double, 4> in_class_probabilities;
public:
  TrivialLightPicker(const Scene &_scene);
  
  // Invokes visitor with the overload pertaining to the selected light type.
  template<class Visitor> 
  void PickLight(Sampler &sampler, Visitor &&visitor)
  {
    int which_kind = TowerSampling<4>(emitter_type_selection_probabilities.data(), sampler.Uniform01());
    switch (which_kind)
    {
      case IDX_PROB_ENV:
      {
        double prob = emitter_type_selection_probabilities[which_kind];
        visitor(scene.GetTotalEnvLight(), prob);
      }
      break;
      case IDX_PROB_POINT:
      {
        const auto n = scene.GetNumPointLights();
        int idx = sampler.UniformInt(0, n-1);
        double prob = emitter_type_selection_probabilities[which_kind]/n;
        visitor(scene.GetPointLight(idx), prob);
      }
      break;
      case IDX_PROB_AREA:
      {
        const auto n = scene.GetNumAreaLights();
        Scene::index_t i = sampler.UniformInt(0, n - 1);
        auto prim_ref = scene.GetPrimitiveFromAreaLightIndex(i);
        double prob = emitter_type_selection_probabilities[which_kind]/n;
        visitor(prim_ref, prob);
      }
      break;
      case IDX_PROB_VOLUME:
      {
        assert(!"not implemented");
        //const int n = isize(volume_light_refs);
        //auto idx = volume_light_refs[sampler.UniformInt(0, n-1)];
        //double prob = emitter_type_selection_probabilities[which_kind]/n;
        //return visitor(*ASSERT_NOT_NULL(scene.GetMaterial(idx).medium), prob);
      }
      break;
    }
  }
  
  double PmfOfLight(const ROI::EnvironmentalRadianceField &) const
  {
    return emitter_type_selection_probabilities[IDX_PROB_ENV];
  }

  double PmfOfLight(const ROI::PointEmitter &) const
  {
    return emitter_type_selection_probabilities[IDX_PROB_POINT] * in_class_probabilities[IDX_PROB_POINT];
  }

  double PmfOfLight(const PrimRef &) const
  {
    return emitter_type_selection_probabilities[IDX_PROB_AREA] * in_class_probabilities[IDX_PROB_AREA];
  }
  
  double PmfOfLight(const Medium &) const
  {
    return emitter_type_selection_probabilities[IDX_PROB_VOLUME] * in_class_probabilities[IDX_PROB_VOLUME];
  }
};

