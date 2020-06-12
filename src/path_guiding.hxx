#pragma once

#include "util.hxx"
#include "vec3f.hxx"
#include "spectral.hxx"
#include "box.hxx"
#include "span.hxx"
#include "distribution_mixture_models.hxx"
#include "path_guiding_tree.hxx"
#include "json_fwd.hxx"

//#include <random>
#include <fstream>
#include <tuple>

//#include <boost/pool/object_pool.hpp>
#include <boost/filesystem/path.hpp>

#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>
#include <tbb/atomic.h>
#include <tbb/task_group.h>
#include <tbb/task_arena.h>

struct SurfaceInteraction;
struct RenderingParameters;

#define WRITE_DEBUG_OUT 
//#define PATH_GUIDING_WRITE_SAMPLES

#if (defined PATH_GUIDING_WRITE_SAMPLES & !defined NDEBUG & defined HAVE_JSON)
#   define PATH_GUIDING_WRITE_SAMPLES_ACTUALLY_ENABLED 1
#endif



namespace guiding
{

inline static constexpr int CACHE_LINE_SIZE = 64;
boost::filesystem::path GetDebugFilePrefix();

using Color::Spectral3f;


struct IncidentRadiance
{
    Double3 pos;
    Float3 reverse_incident_dir;
    float weight;
};


using LeafStatistics = Accumulators::OnlineCovariance<double, 3, int64_t>;

// Should be quite lightweight.
static_assert(sizeof(tbb::spin_mutex) <= 16);

// About 1100 Bytes.
struct CellData
{
    CellData() = default;

    CellData(const CellData &) = delete;
    CellData& operator=(const CellData &) = delete;
    
    CellData(CellData &&) = default;
    CellData& operator=(CellData &&) = default;

    alignas (CACHE_LINE_SIZE) struct CurrentEstimate {
      // Normalized to the total incident flux. So radiance_distribution(w) * incident_flux_density is the actual radiance from direction w.
      vmf_fitting::VonMisesFischerMixture<> radiance_distribution;
      Box cell_bbox{};
      Eigen::Matrix3d points_cov_frame{Eigen::zero}; // U*sqrt(Lambda), where U is composed of Eigenvectors, and Lambda composed of Eigenvalues.
      Eigen::Vector3d points_mean{Eigen::zero};
      Eigen::Vector3d points_stddev{Eigen::zero};
      double incident_flux_density{0.};
      double incident_flux_confidence_bounds{0.};
    } current_estimate;
    
    alignas (CACHE_LINE_SIZE) struct Learned { 
      vmf_fitting::VonMisesFischerMixture<> radiance_distribution;
      vmf_fitting::incremental::Data<> fitdata;
      LeafStatistics leaf_stats;
      OnlineVariance::Accumulator<double, int64_t> incident_flux_density_accum;
    } learned;
    
    long last_num_samples = 0;
    long max_num_samples = 0;
    int index = -1;
};


inline double FittedRadiance(const CellData::CurrentEstimate &estimate, const Double3 &dir)
{
  return vmf_fitting::Pdf(estimate.radiance_distribution, dir.cast<float>()) * estimate.incident_flux_density;
}

inline std::pair<double,double> FittedRadianceWithErr(const CellData::CurrentEstimate &estimate, const Double3 &dir)
{
  double pdf = vmf_fitting::Pdf(estimate.radiance_distribution, dir.cast<float>());
  return {pdf*estimate.incident_flux_density, pdf*estimate.incident_flux_confidence_bounds };
}


#ifdef PATH_GUIDING_WRITE_SAMPLES_ACTUALLY_ENABLED
class CellDebug
{
public:
  CellDebug() = default;
  ~CellDebug();

  CellDebug(const CellDebug &) = delete;
  CellDebug& operator=(const CellDebug &) = delete;

  CellDebug(CellDebug &&) = delete;
  CellDebug& operator=(CellDebug &&) = delete;

  void Open(std::string filename_);
  void Write(const Double3 &pos, const Float3 &dir, float weight);
  void Close();
  const std::string_view GetFilename() const { return filename; }

  vmf_fitting::incremental::Params<> params{};
private:
  std::ofstream file;
  std::string filename;
};
#endif


class CellIterator : kdtree::LeafIterator
{
    Span<const CellData> celldata;
  public:
    CellIterator(const kdtree::Tree &tree_, Span<const CellData> celldata_, const Ray &ray_, double tnear_init, double tfar_init)  noexcept
      : kdtree::LeafIterator{tree_, ray_, tnear_init, tfar_init}, celldata{celldata_}
    {}

    const CellData::CurrentEstimate& operator*() const noexcept
    {
      return celldata[kdtree::LeafIterator::Payload()].current_estimate;
    }

    using kdtree::LeafIterator::Interval;
    using kdtree::LeafIterator::operator bool;
    using kdtree::LeafIterator::operator++;
};



class PathGuiding
{
    public:
        using RadianceEstimate = CellData::CurrentEstimate;

        struct ThreadLocal 
        {
            ToyVector<IncidentRadiance> samples;
        };

        PathGuiding(const Box &region, double cellwidth, const RenderingParameters &params, tbb::task_arena &the_task_arena, const char* name);

        void BeginRound(Span<ThreadLocal*> thread_locals);

        void AddSample(
          ThreadLocal& tl, const Double3 &pos, 
          Sampler &sampler, const Double3 &reverse_incident_dir, 
          const Spectral3 &radiance);

        
        const RadianceEstimate& FindRadianceEstimate(const Double3 &p) const;

        void FinalizeRound(Span<ThreadLocal*> thread_locals);

        void PrepareAdaptedStructures();

        CellIterator MakeCellIterator(const Ray &ray, double tnear_init, double tfar_init) const
        {
          return CellIterator{recording_tree, AsSpan(cell_data), ray, tnear_init, tfar_init};
        }

    private:
        void WriteDebugData();
        void AdaptIncremental();
        void AdaptInitial(Span<ThreadLocal*> thread_locals);
        void FitTheSamples(Span<ThreadLocal*> thread_locals);
        ToyVector<int> ComputeCellIndices(Span<const IncidentRadiance> samples) const;
        ToyVector<ToyVector<IncidentRadiance>> SortSamplesIntoCells(Span<const int> cell_indices, Span<const IncidentRadiance> samples) const;
        void GenerateStochasticFilteredSamplesInplace(Span<int> cell_indices, Span<IncidentRadiance> samples) const;

        static IncidentRadiance ComputeStochasticFilterPosition(const IncidentRadiance & rec, const CellData &cd, Sampler &sampler);
        
        void FitTheSamples(CellData &cell, Span<IncidentRadiance> buffer, bool is_original=true) const;
        
        void Enqueue(int cell_idx, ToyVector<IncidentRadiance> &sample_buffer);
        CellData& LookupCellData(const Double3 &p);

        Box region;
        kdtree::Tree recording_tree;
        ToyVector<CellData, AlignedAllocator<CellData, CACHE_LINE_SIZE>> cell_data;
        std::string name;
#ifdef PATH_GUIDING_WRITE_SAMPLES_ACTUALLY_ENABLED
        std::unique_ptr<CellDebug[]> cell_data_debug;
#endif
        int param_num_initial_samples;
        int param_em_every;
        double param_prior_strength;
        int64_t previous_max_samples_per_cell = 0;
        int64_t previous_total_samples = 0;

        tbb::task_arena *the_task_arena;
        tbb::task_group the_task_group;

        int round = 0;
};


template<class Iter1_, class Iter2_>
class CombinedIntervalsIterator
{
  using Iter1 = Iter1_; //guiding::kdtree::LeafIterator;
  using Iter2 = Iter2_; //SegmentIterator;
  Iter1 leaf_iter;
  Iter2 boundary_iter;
  double tnear, tfar;
  double li_tnear, li_tfar;
  double bi_tnear, bi_tfar;

public: 
  CombinedIntervalsIterator(Iter1 leaf_iter, Iter2 boundary_iter)
    : leaf_iter{leaf_iter}, boundary_iter{boundary_iter}
  {
    std::tie(li_tnear, li_tfar) = leaf_iter.Interval();
    std::tie(bi_tnear, bi_tfar) = boundary_iter.Interval();
    tnear = std::max(li_tnear, bi_tnear);
    tfar = std::min(li_tfar, bi_tfar);    
  }

  operator bool() const noexcept
  {
    return leaf_iter && boundary_iter;
  }

  void operator++()
  {
    // If the interval ends of the two iterators coincide, then here an interval of length zero 
    // will be produced, at the place of the boundary.
    tnear = tfar;
    if (li_tfar <= bi_tfar)
    {
      ++leaf_iter;
      if (leaf_iter)
      {
        std::tie(li_tnear, li_tfar) = leaf_iter.Interval();
      }
    }
    else
    {
      ++boundary_iter;
      if (boundary_iter)
      {
        std::tie(bi_tnear, bi_tfar) = boundary_iter.Interval();
      }
    }
    tfar = std::min(li_tfar, bi_tfar);
  }

  auto Interval() const noexcept
  {
    return std::make_pair(tnear, tfar);
  }

  decltype(*leaf_iter) DereferenceFirst() const
  {
    return *leaf_iter;
  }

  decltype(*boundary_iter) DereferenceSecond() const
  {
    return *boundary_iter;
  }
};


} // namespace guiding