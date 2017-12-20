#include "scene.hxx"
#include "util.hxx"
#include "shader_util.hxx"

#include <fstream>

// WARNING: THIS IS NOT THREAD SAFE. DON'T WRITE TO THE SAME FILE FROM MULTIPLE THREADS!
class PathLogger
{
  std::ofstream file;
  int max_num_paths;
  int num_paths_written;
  int total_path_index;
  void PreventLogFromGrowingTooMuch();
public:
  enum SegmentType : int {
    EYE_SEGMENT = 'e',
    LIGHT_PATH = 'l',
    EYE_LIGHT_CONNECTION = 'c',
  };
  enum ScatterType : int {
    SCATTER_VOLUME = 'v',
    SCATTER_SURFACE = 's'
  };
  PathLogger();
  void AddSegment(const Double3 &x1, const Double3 &x2, const Spectral3 &beta_at_end_before_scatter, SegmentType type);
  void AddScatterEvent(const Double3 &pos, const Double3 &out_dir, const Spectral3 &beta_after, ScatterType type);
  void NewTrace(const Spectral3 &beta_init);
};


PathLogger::PathLogger()
  : file{"paths.log"},// {"paths2.log"}},
    max_num_paths{100},
    num_paths_written{0},
    total_path_index{0}
{
  file.precision(std::numeric_limits<double>::digits10);
}


void PathLogger::PreventLogFromGrowingTooMuch()
{
  if (num_paths_written > max_num_paths)
  {
    //files[0].swap(files[1]);
    file.seekp(std::ios_base::beg);
    num_paths_written = 0;
  }
}


void PathLogger::AddSegment(const Double3 &x1, const Double3 &x2, const Spectral3 &beta_at_end_before_scatter, SegmentType type)
{
  const auto &b = beta_at_end_before_scatter;
  file << static_cast<char>(type) << ", "
       << x1[0] << ", " << x1[1] << ", " << x1[2] << ", "
       << x2[0] << ", " << x2[1] << ", " << x2[2] << ", "
       <<  b[0] << ", " <<  b[1] << ", " <<  b[2] << "\n";
  file.flush();
}

void PathLogger::AddScatterEvent(const Double3 &pos, const Double3 &out_dir, const Spectral3 &beta_after, ScatterType type)
{
  const auto &b = beta_after;
  file << static_cast<char>(type) << ", "
       <<     pos[0] << ", " <<     pos[1] << ", " <<     pos[2] << ", "
       << out_dir[0] << ", " << out_dir[1] << ", " << out_dir[2] << ", "
       <<       b[0] << ", " <<       b[1] << ", " <<       b[2] << "\n";
  file.flush();
}


void PathLogger::NewTrace(const Spectral3 &beta_init)
{
  ++total_path_index;
  ++num_paths_written;
  PreventLogFromGrowingTooMuch();
  const auto &b = beta_init;
  file << "n, " << total_path_index << ", " << b[0] << ", " <<  b[1] << ", " <<  b[2] << "\n";
  file.flush();
}


#ifndef NDEBUG
#  define PATH_LOGGING(x)
#else
#  define PATH_LOGGING(x)
#endif


/* This thing tracks overlapping media volumes. Since it is a bit complicated
 * to physically correctly handle mixtures of media that would occur
 * in overlapping volumes, I take a simpler approach. This hands over the
 * medium with the highest associated priority. In case multiple volumes with the
 * same medium material overlap it will be as if there was the union of all of
 * those volumes.
 */
class MediumTracker
{
  static constexpr int MAX_INTERSECTING_MEDIA = 4;
  const Medium* current;
  std::array<const Medium*, MAX_INTERSECTING_MEDIA> media;
  const Scene &scene;
  void enterVolume(const Medium *medium);
  void leaveVolume(const Medium *medium);
  const Medium* findMediumOfHighestPriority() const;
  bool remove(const Medium *medium);
  bool insert(const Medium *medium);
public:
  explicit MediumTracker(const Scene &_scene);
  MediumTracker(const MediumTracker &_other) = default;
  void initializePosition(const Double3 &pos, HitVector &hits);
  void goingThroughSurface(const Double3 &dir_of_travel, const RaySurfaceIntersection &intersection);
  const Medium& getCurrentMedium() const;
};


MediumTracker::MediumTracker(const Scene& _scene)
  : scene{_scene}, current{nullptr},
    media{} // Note: Ctor zero-initializes the media array.
{
}


const Medium& MediumTracker::getCurrentMedium() const
{
  assert(current);
  return *current;
}


void MediumTracker::initializePosition(const Double3& pos, HitVector &hits)
{
  std::fill(media.begin(), media.end(), nullptr);
  current = &scene.GetEmptySpaceMedium();
  {
    const Box bb = scene.GetBoundingBox();
    // The InBox check is important. Otherwise I would not know how long to make the ray.
    if (bb.InBox(pos))
    {
      double distance_to_go = 2. * (bb.max-bb.min).maxCoeff(); // Times to due to the diagonal.
      Double3 start = pos;
      start[0] += distance_to_go;
      RaySegment seg{{start, {-1., 0., 0.}}, distance_to_go};
      hits.clear();
      scene.IntersectAll(seg.ray, seg.length, hits);
      for (const auto &hit : hits)
      {
        RaySurfaceIntersection intersection(hit, seg);
        goingThroughSurface(seg.ray.dir, intersection);        
      }
    }
  }
}


void MediumTracker::goingThroughSurface(const Double3 &dir_of_travel, const RaySurfaceIntersection& intersection)
{
  if (Dot(dir_of_travel, intersection.volume_normal) < 0)
    enterVolume(intersection.primitive().medium);
  else
    leaveVolume(intersection.primitive().medium);
}


const Medium* MediumTracker::findMediumOfHighestPriority() const
{
  const Medium* medium_max_prio = &scene.GetEmptySpaceMedium();
  for (int i = 0; i < media.size(); ++i)
  {
    medium_max_prio = (media[i] &&  medium_max_prio->priority < media[i]->priority) ?
                        media[i] : medium_max_prio;
  }
  return medium_max_prio;
}


bool MediumTracker::remove(const Medium *medium)
{
  bool is_found = false;
  for (int i = 0; (i < media.size()) && !is_found; ++i)
  {
    is_found = media[i] == medium;
    media[i] = is_found ? nullptr : media[i];
  }
  return is_found;
}


bool MediumTracker::insert(const Medium *medium)
{
  bool is_place_empty = false;
  for (int i = 0; (i < media.size()) && !is_place_empty; ++i)
  {
    is_place_empty = media[i]==nullptr;
    media[i] = is_place_empty ? medium : media[i];
  }
  return is_place_empty;
}


void MediumTracker::enterVolume(const Medium* medium)
{
  // Set one of the array entries that is currently nullptr to the new medium pointer.
  // But only if there is room left. Otherwise the new medium is ignored.
  bool was_inserted = insert(medium);
  current = (was_inserted && medium->priority > current->priority) ? medium : current;
}


void MediumTracker::leaveVolume(const Medium* medium)
{
  // If the medium is in the media stack, remove it.
  // And also make the medium of highest prio the current one.
  remove(medium);
  if (medium == current)
  {
    current = findMediumOfHighestPriority();
  }
}


class Spectral3ImageBuffer
{
  std::vector<int> count;
  std::vector<Spectral3, boost::alignment::aligned_allocator<Spectral3, 128> >  accumulator;
  int xres, yres;
public:
  Spectral3ImageBuffer(int _xres, int _yres)
    : xres(_xres), yres(_yres)
  {
    int sz = _xres * _yres;
    assert(sz > 0);
    count.resize(sz, 0);
    accumulator.resize(sz, Spectral3{0.});
  }
  
  void Insert(int pixel_index, const Spectral3 &value)
  {
    assert (pixel_index >= 0 && pixel_index < count.size() && pixel_index < accumulator.size());
    ++count[pixel_index];
    accumulator[pixel_index] += value; 
  }
  
  void ToImage(Image &dest, int ystart, int yend) const
  {
    assert (ystart >= 0 && yend>= ystart && yend <= dest.height());
    for (int y=ystart; y<yend; ++y)
    for (int x=0; x<xres; ++x)
    {
      int pixel_index = xres * y + x;
      Spectral3 average = accumulator[pixel_index]/count[pixel_index];
      Image::uchar rgb[3];
      bool isfinite = average.isFinite().all();
      //bool iszero = (accumulator[pixel_index]==0.).all();
      average = average.max(0.).min(1.);
      if (isfinite)
      {
        for (int i=0; i<3; ++i)
          rgb[i] = Color::LinearToSRGB(average[i])*255.999;
        dest.set_pixel(x, dest.height() - 1 - y, rgb[0], rgb[1], rgb[2]);
      }
    }
  }
};


class BaseAlgo
{
protected:
  const Scene &scene;
  Sampler sampler;
  MediumTracker medium_tracker_root;
  HitVector hits;
  PATH_LOGGING(
      PathLogger path_logger;)
public:
  BaseAlgo(const Scene &_scene)
    : scene{_scene},
      medium_tracker_root{_scene}
    {}
    
  std::tuple<const Light*, double> PickLightUniform()
  {
    const auto &light = scene.GetLight(sampler.UniformInt(0, scene.GetNumLights()-1));
    double pmf_of_light = 1./scene.GetNumLights();
    return std::make_tuple(&light, pmf_of_light);
  }
  
  virtual Spectral3 MakePrettyPixel(int pixel_index) = 0;
};



class NormalVisualizer : public BaseAlgo
{
public:
  NormalVisualizer(const Scene &_scene) : BaseAlgo(_scene) 
  {
  }
  
  Spectral3 MakePrettyPixel(int pixel_index) override
  {
    auto cam_sample = TakeRaySample(
      scene.GetCamera(), pixel_index, sampler,
      RadianceOrImportance::LightPathContext(Color::LambdaIdxClosestToRGBPrimaries())
    );
    
    RaySegment seg{cam_sample.ray_out, LargeNumber};
    HitId hit = scene.Intersect(seg.ray, seg.length);
    if (hit)
    {
      RaySurfaceIntersection intersection{hit, seg};
      Spectral3 col = (intersection.normal.array() * 0.5 + 0.5).cast<Color::Scalar>();
      return col;
    }
    else
    {
      return Spectral3{0.};
    }
  };
};


using  AlgorithmParameters = RenderingParameters;


class PathTracing : public BaseAlgo
{
  int max_ray_depth;
  double sufficiently_long_distance_to_go_outside_the_scene_bounds;
  
  LambdaSelectionFactory lambda_selection_factory;
  
  
public:
  PathTracing(const Scene &_scene, const AlgorithmParameters &algo_params) 
  : BaseAlgo(_scene), max_ray_depth(algo_params.max_ray_depth) 
  {
    Box bb = _scene.GetBoundingBox();
    sufficiently_long_distance_to_go_outside_the_scene_bounds = 10.*(bb.max - bb.min).maxCoeff();

  }
  
  
  bool RouletteSurvival(Spectral3 &beta, int level)
  {
    static constexpr int MIN_LEVEL = 3;
    static constexpr double LOW_CONTRIBUTION = 0.5;
    if (level >= max_ray_depth || beta.isZero())
      return false;
    if (level < MIN_LEVEL)
      return true;
    double p_survive = std::min(0.9, beta.maxCoeff() / LOW_CONTRIBUTION);
    if (sampler.Uniform01() > p_survive)
      return false;
    beta *= 1./p_survive;
    return true;
  }
  
  
  RaySegment MakeSegmentToLight(const Double3 &pos_to_be_lit, const RadianceOrImportance::Sample &light_sample, const RaySurfaceIntersection *intersection)
  {
    RaySegment seg;
    if (!light_sample.is_direction)
    {
      seg = RaySegment::FromTo(pos_to_be_lit, light_sample.pos);
    }
    else
    {
      // TODO: Make the distance calculation work for the case where the entire space is filled with a scattering medium.
      // In this case an interaction point could be generated far outside the bounds of the scene geometry.
      // I have to derive an appropriate distance. Or just set the distance to infinite because I cannot know
      // the density distribution of the medium.
      seg = RaySegment{{pos_to_be_lit, -light_sample.pos},
        sufficiently_long_distance_to_go_outside_the_scene_bounds};
    }
    if (intersection) seg.ray.org += AntiSelfIntersectionOffset(*intersection, RAY_EPSILON, seg.ray.dir);
    seg.length -= 2.*RAY_EPSILON;
    return seg;
  }
  
  
  Spectral3 TransmittanceEstimate(RaySegment seg, HitId last_hit, MediumTracker medium_tracker, const PathContext &context)
  {
    // TODO: Russian roulette.
    Spectral3 result{1.};
    auto SegmentContribution = [&seg, &medium_tracker, &context, this](double t1, double t2) -> Spectral3
    {
      RaySegment subseg{{seg.ray.PointAt(t1), seg.ray.dir}, t2-t1};
      return medium_tracker.getCurrentMedium().EvaluateTransmission(subseg, sampler, context);
    };

    hits.clear();
    scene.IntersectAll(seg.ray, seg.length, hits);
    double t = 0;
    for (const auto &hit : hits)
    {
      RaySurfaceIntersection intersection{hit, seg};
      result *= intersection.shader().EvaluateBSDF(-seg.ray.dir, intersection, seg.ray.dir, context, nullptr);
      if (result.isZero())
        return result;
      result *= SegmentContribution(t, hit.t);
      medium_tracker.goingThroughSurface(seg.ray.dir, intersection);
      t = hit.t;
    }
    result *= SegmentContribution(t, seg.length);
    return result;
  }
  
  
  Spectral3 LightConnection(const Double3 &pos, const Double3 &incident_dir, const RaySurfaceIntersection *intersection, const MediumTracker &medium_tracker_parent, const PathContext &context)
  {
    if (scene.GetNumLights() <= 0)
      return Spectral3{0.};

    const Light* light; double pmf_of_light; std::tie(light, pmf_of_light) = PickLightUniform();

    RadianceOrImportance::Sample light_sample = light->TakePositionSample(
      sampler, 
      RadianceOrImportance::LightPathContext(context.lambda_idx));
    RaySegment segment_to_light = MakeSegmentToLight(pos, light_sample, intersection);

    double d_factor = 1.;
    Spectral3 scatter_factor;
    if (intersection)
    {
      d_factor = std::max(0., Dot(intersection->shading_normal, segment_to_light.ray.dir));
      const auto &shader = intersection->shader();
      scatter_factor = shader.EvaluateBSDF(-incident_dir, *intersection, segment_to_light.ray.dir, context, nullptr);
    }
    else
    {
      const auto &medium = medium_tracker_parent.getCurrentMedium();
      scatter_factor = medium.EvaluatePhaseFunction(-incident_dir, pos, segment_to_light.ray.dir, context, nullptr);
    }

    if (d_factor <= 0.)
      return Spectral3{0.};

    auto transmittance = TransmittanceEstimate(segment_to_light, (intersection ? intersection->hitid : HitId()), medium_tracker_parent, context);

    auto sample_value = (transmittance * light_sample.measurement_contribution * scatter_factor)
                        * d_factor / (light_sample.pdf * (light_sample.is_direction ? 1. : Sqr(segment_to_light.length)) * pmf_of_light);

    PATH_LOGGING(
    if (!sample_value.isZero())
      path_logger.AddSegment(pos, segment_to_light.EndPoint(), sample_value, PathLogger::EYE_LIGHT_CONNECTION);
    )

    return sample_value;
  }
  
  Spectral3 EvaluateEnvironmentalRadianceField(const Double3 &viewing_dir, const RadianceOrImportance::LightPathContext &context)
  {
    Spectral3 environmental_radiance{0.};
    for (int i=0; i<scene.GetNumLights(); ++i)
    {
      const auto &light = scene.GetLight(i);
      if (light.is_environmental_radiance_distribution) // What an aweful hack. Should there not be an env map class or similar?
      {
        environmental_radiance += light.EvaluatePositionComponent(
          -viewing_dir,
          context,
          nullptr);
      }
    }
    return environmental_radiance;
  }
  
  RGB MakePrettyPixel(int pixel_index) override
  {
    auto lambda_selection = lambda_selection_factory.WithWeights(sampler);
    PathContext context{lambda_selection.first};
    auto cam_sample = TakeRaySample(
      scene.GetCamera(), pixel_index, sampler, 
      RadianceOrImportance::LightPathContext{lambda_selection.first});

    if (cam_sample.measurement_contribution.isZero())
      return RGB::Zero();
    
    MediumTracker medium_tracker = this->medium_tracker_root;
    medium_tracker.initializePosition(cam_sample.ray_out.org, hits);
    
    context.beta *= cam_sample.measurement_contribution / cam_sample.pdf;
    PATH_LOGGING(
      path_logger.NewTrace(context.beta);)

    Spectral3 path_sample_value{0.};
    RaySegment segment{cam_sample.ray_out, LargeNumber};
    int number_of_interactions = 0;

    bool gogogo = true;
    while (gogogo)
    {
      auto hit = scene.Intersect(segment.ray, segment.length);

      const Medium& medium = medium_tracker.getCurrentMedium();
      auto medium_smpl = medium.SampleInteractionPoint(segment, sampler, context);

      context.beta *= medium_smpl.weight;

      if (medium_smpl.t < segment.length)
      {
        Double3 interaction_location = segment.ray.PointAt(medium_smpl.t);
        PATH_LOGGING(path_logger.AddSegment(segment.ray.org, interaction_location, context.beta, PathLogger::EYE_SEGMENT);)

        ++number_of_interactions;
        
        path_sample_value += context.beta *
          LightConnection(interaction_location, segment.ray.dir, nullptr, medium_tracker, context);

        gogogo = RouletteSurvival(context.beta, number_of_interactions);
        if (gogogo)
        {
          auto scatter_smpl = medium.SamplePhaseFunction(-segment.ray.dir, interaction_location, sampler, context);
          context.beta *= scatter_smpl.value / scatter_smpl.pdf;

          PATH_LOGGING(path_logger.AddScatterEvent(segment.ray.org, scatter_smpl.dir, context.beta, PathLogger::SCATTER_VOLUME);)

          segment.ray.org = interaction_location;
          segment.ray.dir = scatter_smpl.dir;
          segment.length  = LargeNumber;
        }
      }
      else if (hit)
      {
        RaySurfaceIntersection intersection{hit, segment};
        PATH_LOGGING(path_logger.AddSegment(segment.ray.org, intersection.pos, context.beta, PathLogger::EYE_SEGMENT);)

        number_of_interactions = intersection.shader().IsPassthrough() ? number_of_interactions : number_of_interactions+1;
        
        if (!intersection.shader().IsReflectionSpecular())
        {
          auto lc = LightConnection(intersection.pos, segment.ray.dir, &intersection, medium_tracker, context);
          path_sample_value += context.beta * lc;
        }

        gogogo = RouletteSurvival(context.beta, number_of_interactions);
        if (gogogo)
        {
          auto surface_sample  = intersection.shader().SampleBSDF(-segment.ray.dir, intersection, sampler, context);
          gogogo = !surface_sample.scatter_function.isZero();
          if (gogogo)
          {
            // By definition, intersection.normal points to where the intersection ray is comming from.
            // Thus we can determine if the sampled direction goes through the surface by looking
            // if the direction goes in the opposite direction of the normal.
            double d_factor = 1.;
            if (Dot(surface_sample.dir, intersection.normal) < 0.)
            {
              medium_tracker.goingThroughSurface(surface_sample.dir, intersection);
            }
            else
            {
              d_factor = std::max(0., Dot(surface_sample.dir, intersection.shading_normal));
            }
            context.beta *= d_factor / surface_sample.pdf * surface_sample.scatter_function;
            PATH_LOGGING(path_logger.AddScatterEvent(intersection.pos, surface_sample.dir, context.beta, PathLogger::SCATTER_SURFACE);)

            segment.ray.org = intersection.pos+AntiSelfIntersectionOffset(intersection, RAY_EPSILON, surface_sample.dir);
            segment.ray.dir = surface_sample.dir;
            segment.length  = LargeNumber;
          }
        }
      }
      else
      {
        gogogo = false;
      }
      assert(context.beta.allFinite());
    }

    if (number_of_interactions == 0)
    {
      // I should actually do this whenever there was no deterministic light connection.
      // And that would be of course here, if the primary ray hit nothing, or if the last interaction
      // was perfectly specular.
      path_sample_value += 1./cam_sample.pdf * cam_sample.measurement_contribution * EvaluateEnvironmentalRadianceField(
        segment.ray.dir,
        RadianceOrImportance::LightPathContext(lambda_selection.first));
    }
    
    return Color::SpectralSelectionToRGB(lambda_selection.second*path_sample_value, lambda_selection.first);
  }
};


#if 0
// TODO: Turn this abomination into a polymorphic type with custom allocator??
//       Use factory to hide allocation in some given buffer (stack or heap).
//       Something like this https://stackoverflow.com/questions/13340664/polymorphism-without-new
//       
struct PathNode
{
  using DirectionalSample = RadianceOrImportance::DirectionalSample;

  enum PathNodeType : char 
  {
    VOLUME,
    NODE1,
    SURFACE,
    NODE1_DIRECTIONAL,
    ESCAPED,
  };
  
  PathNodeType type;
  char index;
  
  // TODO: Use proper variant type. But I don't want boost yet. Don't have c++17 either which has a variant type.
  struct Surface
  {
    RaySurfaceIntersection intersection;
    Double3 inbound_dir;
  };
  struct One
  {
    const RadianceOrImportance::EmitterSensor *emitter;
    Double3 pos_or_dir_to;
    //One() : emitter{nullptr} {}
  };
  struct Volume
  {
    Double3 pos;
    Double3 inbound_dir;
  };
  static auto constexpr BUFFER_SIZE = std::max({sizeof(Surface), sizeof(One), sizeof(Volume)});
  char buffer[BUFFER_SIZE];
  Surface& surface() 
  { 
    assert(type==SURFACE); 
    return *reinterpret_cast<Surface*>(buffer); 
  }
  Volume& volume()
  { 
    assert(type==VOLUME); 
    return *reinterpret_cast<Volume*>(buffer); 
  }
  One& one() 
  { 
    assert(type==NODE1 || type == NODE1_DIRECTIONAL); 
    return *reinterpret_cast<One*>(buffer); 
  }
  const Surface& surface() const { return const_cast<PathNode*>(this)->surface(); }
  const Volume& volume() const { return const_cast<PathNode*>(this)->volume(); }
  const One& one() const { return const_cast<PathNode*>(this)->one(); }
  
  PathNode(PathNodeType _type, char _index) : type{_type}, index{_index} {}
  PathNode(const PathNode &);
  PathNode& operator=(const PathNode &);
//   PathNode(PathNode &&);
//   PathNode& operator=(const PathNode &&);
  DirectionalSample SampleDirection(Sampler &sampler) const;
  Spectral3 EvaluateDirection(const Double3 &outbound_dir) const;
  Double3 Position() const;
  Double3 DirectionToEmitter() const;
  Double3 InboundDir() const;
  HitId hitId() const;

  static PathNode MakeTypeOne(
    const RadianceOrImportance::EmitterSensor &emitter, 
    const RadianceOrImportance::Sample &sample);
  static PathNode MakeSurface(
        char index, const Double3& inbound_dir, const RaySurfaceIntersection& intersection);
  static PathNode MakeEscaped(char index);
};


Double3 PathNode::Position() const
{
  const auto &node = *this;
  assert (node.type != NODE1_DIRECTIONAL);
  return node.type == NODE1 ? 
    node.one().pos_or_dir_to : 
      (node.type == VOLUME ? 
        node.volume().pos : node.surface().intersection.pos);
}


Double3 PathNode::DirectionToEmitter() const
{
  const auto &node = *this;
  assert (node.type == NODE1_DIRECTIONAL);
  return node.one().pos_or_dir_to;
}


Double3 PathNode::InboundDir() const
{
  const auto &node = *this;
  assert (node.type == SURFACE || node.type == VOLUME);
  return node.type == SURFACE ? 
    node.surface().inbound_dir : 
    node.volume().inbound_dir;
}


HitId PathNode::hitId() const
{
  return (type == SURFACE) ? surface().intersection.hitid : HitId{};
}


PathNode::PathNode(const PathNode& other) :
  type{other.type},
  index{other.index}
{
  switch(type)
  {
    case NODE1:
    case NODE1_DIRECTIONAL:
      new (buffer) One(other.one());
      break;
    case SURFACE:
      new (buffer) Surface(other.surface());
      break;
    case VOLUME:
      new (buffer) Volume(other.volume());
      break;
  }
}


PathNode& PathNode::operator=(const PathNode& other)
{
  this->~PathNode();
  new (this) PathNode(other);
  return *this;
}


Spectral3 PathNode::EvaluateDirection(const Double3& outbound_dir) const
{
  const auto &node = *this;
  if (node.type ==NODE1 || node.type == NODE1_DIRECTIONAL)
  {
    return node.one().emitter->EvaluateDirectionComponent(node.one().pos_or_dir_to, outbound_dir, nullptr);
  }
  else if (node.type == SURFACE)
  {
    return node.surface().intersection.shader().EvaluateBSDF(-node.surface().inbound_dir, node.surface().intersection, outbound_dir, nullptr);
  }
  assert(false && "Not implemented");
}


PathNode::DirectionalSample PathNode::SampleDirection(Sampler &sampler) const
{
  const auto &node = *this;
  if (node.type == NODE1 || node.type == NODE1_DIRECTIONAL)
  {
    return node.one().emitter->TakeDirectionSampleFrom(node.one().pos_or_dir_to, sampler);
  }
  else if (node.type == SURFACE)
  {
    auto sample = node.surface().intersection.shader().SampleBSDF(-node.surface().inbound_dir, node.surface().intersection, sampler);
    double d_factor = Dot(node.surface().intersection.normal, sample.dir);
    return RadianceOrImportance::DirectionalSample{
      {node.surface().intersection.pos, sample.dir}, 
      sample.pdf, 
      d_factor * sample.scatter_function};
  }
  assert(false && "Not implemented");
}


PathNode PathNode::MakeSurface(char index, const Double3& inbound_dir, const RaySurfaceIntersection& intersection)
{
  PathNode result{SURFACE, index};
  new (result.buffer) Surface{intersection, inbound_dir};
  return result;
}


PathNode PathNode::MakeTypeOne(const RadianceOrImportance::EmitterSensor& emitter, const RadianceOrImportance::Sample& sample)
{
  PathNode result{sample.is_direction ? NODE1_DIRECTIONAL : NODE1, 1};
  new (result.buffer) One{&emitter, sample.pos};
  return result;
}


PathNode PathNode::MakeEscaped(char index)
{
  return PathNode{ESCAPED, index};
}




class Bdpt : public BaseAlgo
{
  static constexpr int MAX_SUBPATH_LENGH = 5;
  static constexpr int MAX_PATH_LENGTH = MAX_SUBPATH_LENGH*2 + 2; 
  
  struct BdptHistory
  {
    Spectral3 measurement_contribution[MAX_PATH_LENGTH];
    double  pdf_product_up_to_node[MAX_PATH_LENGTH];
    BdptHistory()
    {
      pdf_product_up_to_node[0] = 1.;
      measurement_contribution[0] = Spectral3{1.};
      for (int i=1; i<MAX_PATH_LENGTH; ++i)
      {
        pdf_product_up_to_node[i] = NaN;
        measurement_contribution[i] = Spectral3{NaN};
      }
    }
    void Copy(int src, int dst)
    {
      measurement_contribution[dst] = measurement_contribution[src];
      pdf_product_up_to_node[dst] = pdf_product_up_to_node[src];
    }
    void MultiplyNodeValues(int idx, double pdf_factor, const Spectral3 &contrib_factor)
    {
      measurement_contribution[idx] *= contrib_factor;
      pdf_product_up_to_node[idx] *= pdf_factor;
    }
  };
  
public:  
  Bdpt(const Scene &_scene) : BaseAlgo(_scene)
  {
  }
  

  Spectral3 MakePrettyPixel() override
  {
    Spectral3 ret{0.};
    ret += BiDirectionPathTraceOfLength(2, 1);
    ret += BiDirectionPathTraceOfLength(2, 2);
    ret += BiDirectionPathTraceOfLength(2, 3);
    ret += BiDirectionPathTraceOfLength(3, 1);
    //ret += BiDirectionPathTraceOfLength(4, 1);
    return ret;
  }
  
  
  Spectral3 BiDirectionPathTraceOfLength(int eye_path_length, int light_path_length)
  {
    BdptHistory history_from_eye;
    BdptHistory history_from_light;

    if (eye_path_length <= 0)
      return Spectral3(0.);
    if (light_path_length <= 0)
      return Spectral3(0.);
    
    auto eye_node = TracePath(scene.GetCamera(), eye_path_length, history_from_eye);
    if (eye_node.type == PathNode::ESCAPED) return Spectral3{0};
    
    const Light* the_light; double pmf_of_light; 
    std::tie(the_light, pmf_of_light) = PickLightUniform();
    auto light_node = TracePath(*the_light, light_path_length, history_from_light);
    if (light_node.type == PathNode::ESCAPED) return Spectral3{0};
    
    Spectral3 measurement_contribution = 
      GeometryTermAndScattering(light_node, eye_node) *
      history_from_eye.measurement_contribution[eye_node.index] *
      history_from_light.measurement_contribution[light_node.index];
    double  path_pdf = history_from_eye.pdf_product_up_to_node[eye_node.index] *
                       history_from_light.pdf_product_up_to_node[light_node.index];
    Spectral3 path_sample_value = measurement_contribution / path_pdf;
    return path_sample_value;
  };
    
  
  PathNode TracePath(const RadianceOrImportance::EmitterSensor &emitter, int length, BdptHistory &history)
  {
      PathNode lastnode = InitNodeOne(
        emitter,
        history);
      while(lastnode.index < length && lastnode.type != PathNode::ESCAPED)
      {
        MakeNextNode(
          lastnode,
          history);
      }
      return lastnode;
  }
  
  
  PathNode InitNodeOne(
    const RadianceOrImportance::EmitterSensor &emitter,
    BdptHistory &history)
  {
    RadianceOrImportance::Sample sample = emitter.TakePositionSample(sampler);
    history.MultiplyNodeValues(0, 
                               sample.pdf, 
                               sample.measurement_contribution);
    history.Copy(0, 1);
    // Return value optimization???
    return PathNode::MakeTypeOne(emitter, sample);
  }
  
  
  void MakeNextNode(
    PathNode &node, // updated
    BdptHistory &history)
  {
    auto dir_sample = node.SampleDirection(sampler);
    history.MultiplyNodeValues(
      node.index,
      dir_sample.pdf,
      dir_sample.measurement_contribution);
    history.Copy(node.index, node.index+1); // It's the same value since we have not sampled at the node we are going to add now yet.
    auto segment = RaySegment{dir_sample.ray_out, LargeNumber};
    auto last_hit = node.hitId();
    HitId hit = scene.Intersect(segment.ray, segment.length, last_hit);
    if (hit)
    {
      auto intersection = RaySurfaceIntersection{hit, segment};
      node = PathNode::MakeSurface(node.index+1, dir_sample.ray_out.dir, intersection);
    }
    else
    {
      node = PathNode::MakeEscaped(node.index+1);
    }
  }


  double DFactor(const PathNode &node, const Double3 &outgoing_dir)
  {
    assert(node.type == PathNode::SURFACE);
    return std::max(0., Dot(node.surface().intersection.normal, outgoing_dir));
  }
  
  
  Spectral3 GeometryTermAndScattering(
    const PathNode &node1,
    const PathNode &node2
  )
  {
    // We sampled for each of the nodes a random direction,
    // reflecting light or importance arriving from an infinitely 
    // distance source. Only if both direction coincided would there
    // be a contribution (unlikely). Such paths are better handled
    // by s=0,t=2 or s=2,t=0 paths.
    if (node1.type == PathNode::NODE1_DIRECTIONAL && 
        node2.type == PathNode::NODE1_DIRECTIONAL)
      return Spectral3{0.};
    
    RaySegment segment;
    HitId hits_ignored[2];
    if (node1.type == PathNode::NODE1_DIRECTIONAL)
    {
      segment = RaySegment{{node2.Position(), node1.DirectionToEmitter()}, LargeNumber};
      hits_ignored[0] = node2.hitId();
    }
    else if (node2.type == PathNode::NODE1_DIRECTIONAL)
    {
      segment = RaySegment{{node1.Position(), node2.DirectionToEmitter()}, LargeNumber};
      hits_ignored[0] = node1.hitId();
    }
    else
    {
      segment = RaySegment::FromTo(node1.Position(), node2.Position());
      hits_ignored[0] = node1.hitId();
      hits_ignored[1] = node2.hitId();
    }
    if (scene.Occluded(segment.ray, segment.length, hits_ignored[0], hits_ignored[1]))
      return Spectral3{0.};
    bool isAreaMeasure = segment.length<LargeNumber; // i.e. nodes are not directional.
    double geom_term = isAreaMeasure ? Sqr(1./segment.length) : 1.;
    if (node1.type == PathNode::SURFACE)
      geom_term *= DFactor(node1, segment.ray.dir);
    if (node2.type == PathNode::SURFACE)
      geom_term *= DFactor(node2, -segment.ray.dir);    

    Spectral3 scatter1 = node1.EvaluateDirection(segment.ray.dir);
    Spectral3 scatter2 = node2.EvaluateDirection(-segment.ray.dir);
    return geom_term * scatter1 * scatter2;
  }
};
#endif
