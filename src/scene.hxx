#pragma once
#include "embreeaccelerator.hxx"
#include "util.hxx"
#include "types.hxx"
#include "box.hxx"
#include "primitive.hxx"
#include "ray.hxx"

#include <memory>
#include <boost/functional/hash.hpp>
#include <mpark/variant.hpp>
#include <optional>

namespace boost { namespace filesystem {
  class path;
}};

namespace RadianceOrImportance
{
class TotalEnvironmentalRadianceField;
};


struct RenderingParameters
{
  int pixel_x = {-1};
  int pixel_y = {-1};
  int width = {-1};
  int height = {-1};
  int num_threads = { -1 };
  int max_ray_depth = 25;
  int max_samples_per_pixel = {-1};
  std::string pt_sample_mode = {};
  std::string algo_name = {};
  std::vector<std::string> search_paths = { "" };
  double initial_photon_radius = 0.01;
  double guiding_prior_strength = 50.;
  int guiding_em_every = 200;
  int guiding_tree_subdivision_factor = 100;
  int guiding_max_spp = 512;
  bool linear_output = false;
  bool qmc = false;
};


struct Material
{
  Shader* shader = {nullptr};
  Medium* medium = {nullptr};  // Within the geometry. I.e. on the other side of where the surface normal points.
  RadianceOrImportance::AreaEmitter *emitter = {nullptr};
  Medium* outer_medium = { nullptr };
  
  struct Hash
  {
    inline std::size_t operator()(const Material &key) const
    {
      std::size_t h = boost::hash_value((key.shader));
      boost::hash_combine(h, boost::hash_value((key.medium)));
      boost::hash_combine(h, boost::hash_value((void*)key.emitter));
      boost::hash_combine(h, boost::hash_value((void*)key.outer_medium));
      return h;
    }
  };
  
  friend inline bool operator==(const Material &a, const Material &b)
  {
    return (a.shader == b.shader) &&
          (a.medium == b.medium) &&
          (a.emitter == b.emitter) && 
          (a.outer_medium == b.outer_medium);
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
  Double3 normal;    // Geometry normal, oriented toward the incomming ray, if result of ray-surface intersection.
  Double3 shading_normal; // Same for smooth normal.
  Float2 tex_coord;
  Float3 pos_bounds{ 0. }; // Bounds within which the true hitpoint (computed without roundoff errors) lies. See PBRT chapt 3.

  SurfaceInteraction(const HitId &hitid, const RaySegment &incident_segment);
  SurfaceInteraction(const HitId &hitid);
  SurfaceInteraction() = default;
  
  SurfaceInteraction(const SurfaceInteraction &) = default;
  SurfaceInteraction(SurfaceInteraction&&) = default;
  SurfaceInteraction& operator=(const SurfaceInteraction &) = default;
  SurfaceInteraction& operator=(SurfaceInteraction &&) = default;

  void SetOrientedNormals(const Double3 &incident);
};


struct VolumeInteraction : public InteractionPoint
{
  const Medium *_medium = nullptr;
  Spectral3 radiance;
  Spectral3 sigma_s; // Scattering coefficient. Use in evaluate functions and scatter sampling. Kernel defined as phase function times sigma_s. 
  VolumeInteraction() = default;
  VolumeInteraction(const Double3 &_position, const Medium &_medium, const Spectral3 &radiance_, const Spectral3 &sigma_s_)
    : InteractionPoint{ _position }, _medium{ &_medium }, radiance{ radiance_ }, sigma_s{ sigma_s_ }
  {}

  VolumeInteraction(const VolumeInteraction &) = default;
  VolumeInteraction(VolumeInteraction&&) = default;
  VolumeInteraction& operator=(const VolumeInteraction &) = default;
  VolumeInteraction& operator=(VolumeInteraction &&) = default;

  const Medium& medium() const { return *_medium; }
};

using SomeInteraction = mpark::variant<SurfaceInteraction, VolumeInteraction>;
using MaybeSomeInteraction = std::optional<SomeInteraction>;

Double3 AntiSelfIntersectionOffset(const SurfaceInteraction &interaction, const Double3 &exitant_dir);

namespace scenereader
{
struct Scope;
void AddDefaultMaterials(Scope &scope, const Scene &scene);
class YamlSceneReader;
}

class Scene
{
public:
  using index_t = scene_index_t;
  static constexpr MaterialIndex DEFAULT_MATERIAL_INDEX{ 0 };

private:
  friend class NFFParser;
  friend class scenereader::YamlSceneReader;
  friend void scenereader::AddDefaultMaterials(scenereader::Scope &scope, const Scene &scene);
  friend class EmbreeAccelerator;
  friend class PrimitiveIterator;
  using Light = RadianceOrImportance::PointEmitter;
  using EnvironmentalRadianceField = RadianceOrImportance::EnvironmentalRadianceField;
  using AreaLight = RadianceOrImportance::AreaEmitter;

  EmbreeAccelerator embreeaccelerator;
  EmbreeAccelerator embreevolumes;
  std::unique_ptr<Camera> camera;

  ToyVector<std::unique_ptr<Geometry>> geometries;
  ToyVector<Geometry*> emissive_surfaces; // Indicies into geometries array
  ToyVector<Geometry*> surfaces;
  ToyVector<Geometry*> volumes;
  index_t num_area_lights = 0;

  ToyVector<Material> materials;
  Medium* empty_space_medium;
  Shader* invisible_shader;
  Shader* default_shader;
  Shader* black_shader;
  MaterialIndex default_material_index;
  MaterialIndex vacuum_material_index;

  ToyVector<std::unique_ptr<Shader>> shaders;
  ToyVector<std::unique_ptr<Medium>> media;
  ToyVector<std::unique_ptr<EnvironmentalRadianceField>> envlights;
  ToyVector<std::unique_ptr<Light>> lights; // point lights
  ToyVector<std::shared_ptr<Texture>> textures;
  std::unique_ptr<RadianceOrImportance::TotalEnvironmentalRadianceField> envlight;
  Box boundingBox;

public:
  Scene();
  ~Scene();
  void ParseSceneFile(const boost::filesystem::path &filename, RenderingParameters *render_params = nullptr);
  void ParseNFF(const boost::filesystem::path &filename, RenderingParameters *render_params = nullptr);
  void ParseNFFString(const std::string &scenestr, RenderingParameters *render_params = nullptr);
  void ParseNFF(std::istream &is, RenderingParameters *render_params = nullptr);
  void ParseYAML(std::istream &is, RenderingParameters *render_params, const boost::filesystem::path &filename_hint);
  void WriteObj(const boost::filesystem::path &filename) const;

  const Camera& GetCamera() const
  {
    return *camera;
  }

  Camera& GetCamera()
  {
    return *camera;
  }

  bool HasCamera() const
  {
    return camera != nullptr;
  }


  bool HasLights() const;

  bool HasEnvLight() const { return envlights.size() > 0; }

  // TODO: Remove
  const RadianceOrImportance::TotalEnvironmentalRadianceField& GetTotalEnvLight() const
  {
    assert(envlight.get() != nullptr);
    return *envlight;
  }

  index_t GetNumPointLights() const
  {
    return isize(lights);
  }

  const Light& GetPointLight(index_t i) const
  {
    return *lights[i];
  }

  index_t GetNumAreaLights() const;

  index_t GetAreaLightIndex(const PrimRef ref) const; // Return invalid index if has no light.

  PrimRef GetPrimitiveFromAreaLightIndex(index_t light) const;

  //index_t GetNumVolumeLights() const;

  //const Material& GetVolumeLight(index_t i) const;

  const Material& GetMaterialOf(index_t geom_idx, index_t prim_idx) const;

  const Material& GetMaterialOf(const PrimRef ref) const;

  const Medium& GetEmptySpaceMedium() const
  {
    return *this->empty_space_medium;
  }

  const Shader& GetInvisibleShader() const
  {
    return *this->invisible_shader;
  }


  inline index_t GetNumGeometries() const
  {
    return isize(geometries);
  }

  inline const Geometry& GetGeometry(index_t i) const
  {
    assert(i >= 0 && i < GetNumGeometries());
    return *geometries[i];
  }

  inline index_t GetNumMaterials() const
  {
    return (index_t)materials.size();
  }

  inline const Material& GetMaterial(index_t i) const
  {
    return materials[i];
  }

  inline index_t GetNumShaders() const
  {
    return static_cast<index_t>(shaders.size());
  }

  inline Shader& GetShader(index_t i) 
  {
    return *shaders[i];
  }

  bool FirstIntersectionEmbree(const Ray &ray, double tnear, double &ray_length, SurfaceInteraction &intersection) const
  {
    return embreeaccelerator.FirstIntersection(ray, tnear, ray_length, intersection);
  }

  std::optional<SurfaceInteraction> FirstIntersection(const Ray &ray, double tnear, double &tfar) const
  {
    SurfaceInteraction meh; // ... oh well this needs to change ...
    bool hit = embreeaccelerator.FirstIntersection(ray, tnear, tfar, meh);
    return hit ? meh : std::optional<SurfaceInteraction>{};
  }

  Span<BoundaryIntersection> IntersectionsWithVolumes(const Ray &ray, double tnear, double tfar) const
  {
    return embreevolumes.IntersectionsInOrder(ray, tnear, tfar);
  }
  Span<BoundaryIntersection> IntersectionsWithSurfaces(const Ray &ray, double tnear, double tfar) const
  {
    return embreeaccelerator.IntersectionsInOrder(ray, tnear, tfar);
  }

  bool IsOccluded(const Ray &ray, double tnear, double tfar) const
  {
    return embreeaccelerator.IsOccluded(ray, tnear, tfar);
  }

  void BuildAccelStructure();
  
  void PrintInfo() const;
  
  Box GetBoundingBox() const;

  void Append(const Geometry &geo, const Material &mat);

private:
  void UpdateEmissiveIndexOffset();
};
