#include "gtest/gtest.h"

#include <rapidjson/document.h>

#include "cubature_wrapper.hxx"
#include "cubemap.hxx"
#include "tests_stats.hxx"

#include "normaldistributionfunction.hxx"
#include "util.hxx"
#include "sampler.hxx"
#include "shader_util.hxx"

namespace {
static const Double3 VERTICAL = Double3{0,0,1};
static const Double3 EXACT_45DEG    = Normalized(Double3{0,1,1});
static const Double3 ALMOST_45DEG    = Normalized(Double3{0,1,1.1});
static const Double3 MUCH_DEFLECTED = Normalized(Double3{0,10,1});
static const Double3 BELOW          = Normalized(Double3{0,5,-1});
}


template<class Func>
auto IntegralOverCubemap(Func func, const CubeMap &cubemap, double absError, double relError, int max_eval = 0) //, std::vector<decltype(func(Double2{}))> *errors = nullptr)
{
  using R = decltype(func(Double2{}));
  std::vector<R> result;  result.reserve(cubemap.TotalNumBins());
  for (int idx=0; idx<cubemap.TotalNumBins(); ++idx)
  {
    auto [side, i, j] = cubemap.IndexToCell(idx);
    // Integrating this gives me the probability of a sample falling
    // into the current bin. The integration is carried out on a square in
    // R^2, hence the scale factor J from the Jacobian of the variable transform.
    auto probabilityDensityTimesJ = [side = side,&cubemap,&func](const Double2 x) -> double
    {
      Double3 omega = cubemap.UVToOmega(side, x);
      R val = func(omega)*cubemap.UVtoJ(x);
      return val;
    };
    auto [start, end] = cubemap.CellToUVBounds(i, j);
    //R err = NaN;
    const R prob = Integral2D(probabilityDensityTimesJ, start, end, absError, relError, max_eval, nullptr); //, errors ? &err : nullptr);
    result.emplace_back(prob);
    //if (errors)
    //  errors->emplace_back(err);
  }
  return result;
}


template<class Func>
std::vector<int> SampleOverCubemap(Func generator, const CubeMap &cubemap, int count)
{
  std::vector<int> result(cubemap.TotalNumBins(), 0);
  for (int k=0; k<count; ++k)
  {
    const Double3 w = generator();
    const auto [side, i, j] = cubemap.OmegaToCell(w);
    const int idx = cubemap.CellToIndex(side, i, j);
    result[idx]++;
  }
  return result;
}

template<class Func, class T>
auto Map(const std::vector<T> &v, Func &&f)
{
  std::vector<decltype(f(v[0]))> ret; ret.reserve(v.size());
  for (const auto &x : v)
    ret.emplace_back(f(x));
  return ret;
}



#ifdef HAVE_JSON
namespace rj
{
using namespace rapidjson;

using Alloc = rapidjson::Document::AllocatorType; // For some reason I must supply an "allocator" almost everywhere. Why? Who knows?!

rapidjson::Value Array3ToJSON(const Eigen::Array<double, 3, 1> &v, Alloc &alloc);
void Write(rj::Document &doc, const std::string &filename);

rj::Value CubemapToJSON(const CubeMap &cubemap, const int Nbins, std::vector<double> bin_values, rj::Alloc& alloc)
{
  rj::Value json_side;
  json_side.SetArray();
  for (int side = 0; side < 6; ++side)
  {
    rj::Value json_i(rj::kArrayType);
    for (int i = 0; i < Nbins; ++i)
    {
      rj::Value json_j(rj::kArrayType);
      for (int j = 0; j < Nbins; ++j)
      {
        rj::Value json_bin(rj::kObjectType);
        int idx = cubemap.CellToIndex(side, i, j);
        auto uv_bounds = cubemap.CellToUVBounds(i, j);
        json_bin.AddMember("value", bin_values[idx], alloc);
        json_bin.AddMember("pos", rj::Array3ToJSON(cubemap.UVToOmega(side, (std::get<0>(uv_bounds) + std::get<1>(uv_bounds))*0.5).array(), alloc), alloc);
        json_j.PushBack(json_bin, alloc);
      }
      json_i.PushBack(json_j, alloc);
    }
    json_side.PushBack(json_i, alloc);
  }
  return json_side;
}


}

#endif




TEST(NDF, Beckmann)
{
  using NDF = BeckmanDistribution;
  CubeMap cubemap(8);
  const double alpha = 0.05;
  Sampler sampler;
  
  NDF ndf{alpha};

  auto density_func = [&](const Double3 &w)
  {
    // Argument is w.dot.n, where n is aligned with the z-axis.
    // So it simplifies to w.z. The second term comes from the fact
    // that we test NDF.|w.n|, which is a proper probability density.
    // Just NDF is not.
    return ndf.EvalByHalfVector(w[2])*std::abs(w[2]);
  };
  
  auto sample_gen = [&]() 
  {
    return ndf.SampleHalfVector(sampler.UniformUnitSquare());
  };
  
  std::vector<double> probs = IntegralOverCubemap(density_func, cubemap, 1.e-3, 1.e-2, 100000);
  std::vector<int> sample_counts = SampleOverCubemap(sample_gen, cubemap, 1000);
  
//   for (int i=0; i<cubemap.TotalNumBins(); ++i)
//   {
//     std::cout << strconcat(i,": p=", probs[i], ", cnt=", sample_counts[i]) << std::endl;
//   }
  double chi_sqr_probability = ChiSquaredProbability(&sample_counts[0], &probs[0], probs.size());
  EXPECT_GE(chi_sqr_probability, 0.05);
}


using NDF_Params = std::tuple<double, Double3>;

class NDFTest : public testing::TestWithParam<NDF_Params>
{
public:
  using NDF = BeckmanDistribution;
  Sampler sampler;
};


TEST_P(NDFTest, Outdirection)
{
  /* Test the mapping from microfacet normal distribution to outgoing direction.
   * The incident ray is reflected specularly at the microfacet normal.
   * Thus the exitant ray is a random variable with some distribution on the unit sphere.
   * So here, I test the consistency of sampling and density of the existant direction.
   */
  
  const auto [alpha, wi] = this->GetParam();
  CubeMap cubemap(8); 
  NDF ndf{alpha};

  auto density_func = [wi=wi, &ndf](const Double3 &wo)
  {
    Double3 wh = Normalized(wo + wi);
    // The abs(...) in the argument to the NDF is required for
    // when wi lies below the ground plane. If it was not there,
    // the NDF would return 0, which is wrong!. Computation of the
    // half-vector from wi and wo, can produce half-vectors which also lie below
    // the ground plane. But the plain NDF is not defined there.
    // With the abs(...) we have the correct distribution for wo!
    double ndf_val = ndf.EvalByHalfVector(std::abs(wh[2]))*std::abs(wh[2]);
    return HalfVectorPdfToReflectedPdf(ndf_val, Dot(wh, wi));
  };
  
  auto sample_gen = [wi = wi, this, &ndf]()
  {
    Double3 wh = ndf.SampleHalfVector(sampler.UniformUnitSquare());
    return Reflected(wi,wh);
  };
  
  std::vector<double> probs = IntegralOverCubemap(density_func, cubemap, 1.e-3, 1.e-2, 100000);
  std::vector<int> sample_counts = SampleOverCubemap(sample_gen, cubemap, 1000);
  double chi_sqr_probability = ChiSquaredProbability(&sample_counts[0], &probs[0], probs.size());
  EXPECT_GE(chi_sqr_probability, 0.05);
  // Integral of the density over all solid angles should be normalized to 1.
  double total_prob = std::accumulate(probs.begin(), probs.end(), 0.);
  EXPECT_NEAR(total_prob, 1., 1.e-2);
}


TEST_P(NDFTest, VNDFSampling)
{
  const auto [alpha, wi] = this->GetParam();
  CubeMap cubemap(8); 
  NDF ndf{alpha};
  
  auto density_func = [wi=wi, &ndf](const Double3 &wh)
  {
    double ndf_val = ndf.EvalByHalfVector(wh[2]);
    return VisibleNdfVCavity::Pdf(ndf_val, wh, wi);
  };
  
  auto sample_gen = [wi = wi, this, &ndf]() 
  {
    // The method by Heitz et. al (2014), Algorithm 3, for VCavity model.
    Double3 wh = ndf.SampleHalfVector(sampler.UniformUnitSquare());
    VisibleNdfVCavity::Sample(wh, wi, sampler.Uniform01());
    return wh;
  };
  
  std::vector<double> probs = IntegralOverCubemap(density_func, cubemap, 1.e-3, 1.e-2, 100000);
  std::vector<int> sample_counts = SampleOverCubemap(sample_gen, cubemap, 1000);
  double chi_sqr_probability = ChiSquaredProbability(&sample_counts[0], &probs[0], probs.size());
  EXPECT_GE(chi_sqr_probability, 0.05);
  double total_prob = std::accumulate(probs.begin(), probs.end(), 0.);
  EXPECT_NEAR(total_prob, 1., 1.e-2);
}


TEST_P(NDFTest, VNDFSamplingOutDirection)
{
  const auto [alpha, wi] = this->GetParam();
  CubeMap cubemap(8); 
  NDF ndf{alpha};
  
  auto density_func = [wi=wi, &ndf](const Double3 &wo)
  {
    Double3 wh = Normalized(wo + wi);
    if (wh[2] < 0)
      wh = -wh;
    double ndf_val = ndf.EvalByHalfVector(wh[2]);
    double vndf = VisibleNdfVCavity::Pdf(ndf_val, wh, wi);
    return HalfVectorPdfToReflectedPdf(vndf, Dot(wh, wi));
  };
  
  auto sample_gen = [wi = wi, this, &ndf]() 
  {
    // The method by Heitz et. al (2014), Algorithm 3, for VCavity model.
    Double3 wh = ndf.SampleHalfVector(sampler.UniformUnitSquare());
    VisibleNdfVCavity::Sample(wh, wi, sampler.Uniform01());
    return Reflected(wi,wh);
  };
  
  std::vector<double> probs = IntegralOverCubemap(density_func, cubemap, 1.e-3, 1.e-2, 100000);
  std::vector<int> sample_counts = SampleOverCubemap(sample_gen, cubemap, 1000);
  double chi_sqr_probability = ChiSquaredProbability(&sample_counts[0], &probs[0], probs.size());
  EXPECT_GE(chi_sqr_probability, 0.05);
  double total_prob = std::accumulate(probs.begin(), probs.end(), 0.);
  EXPECT_NEAR(total_prob, 1., 1.e-2);
}



TEST(NDFTest, VNDFViz)
{
  using NDF = BeckmanDistribution;
  CubeMap cubemap(32); 
  NDF ndf{0.08};
  const Double3 wi = MUCH_DEFLECTED; //Normalized(Double3{10, 0, -1});
  Sampler sampler;
  
  auto density_func = [&](const Double3 &wo)
  {
    Double3 wh = Normalized(wo + wi);
    if (wh[2] < 0)
      wh = -wh;
    double ndf_val = ndf.EvalByHalfVector(wh[2]);
    double vndf = VisibleNdfVCavity::Pdf(ndf_val, wh, wi);
    return HalfVectorPdfToExitantPdf(vndf, Dot(wh, wi));
    
    //return HalfVectorPdfToExitantPdf(ndf_val*wh[2], Dot(wh, wi));
  };
  
  auto sample_gen = [&]() 
  {
    // The method by Heitz et. al (2014), Algorithm 3, for VCavity model.
    Double3 wh = ndf.SampleHalfVector(sampler.UniformUnitSquare());
    VisibleNdfVCavity::Sample(wh, wi, sampler.Uniform01());
    return HalfVectorToExitant(wh, wi);
  };
  
  std::vector<double> probs = IntegralOverCubemap(density_func, cubemap, 1.e-3, 1.e-2, 100000);
  std::vector<int> sample_counts = SampleOverCubemap(sample_gen, cubemap, 100000);
  std::vector<double> double_counts;
  for (auto x : sample_counts) double_counts.push_back((double)x);
  
  rj::Document doc;
  auto &alloc = doc.GetAllocator();
  doc.SetObject();
  
  auto node1 = rj::CubemapToJSON(cubemap, 32, probs, alloc);
  doc.AddMember("probs", node1, alloc);
  auto node2 = rj::CubemapToJSON(cubemap, 32, double_counts, alloc);
  doc.AddMember("counts", node2, alloc);
  rj::Write(doc, "/tmp/cubemap.json");
}



namespace NDFTestNS
{

INSTANTIATE_TEST_CASE_P(NDFTestNS, NDFTest, ::testing::Values(
  NDF_Params(0.05, VERTICAL),
  NDF_Params(0.08, MUCH_DEFLECTED), // Needs more roughness. Probably because numerical error of integration of the probability density.
  NDF_Params(0.05, BELOW),
  NDF_Params(0.5, VERTICAL),
  NDF_Params(0.5, MUCH_DEFLECTED),
  NDF_Params(0.5, BELOW)
));


}
