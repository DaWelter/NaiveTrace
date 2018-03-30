#include "gtest/gtest.h"
#include <cstdio>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <rapidjson/document.h>
#include <boost/filesystem.hpp>

#include "ray.hxx"
#include "image.hxx"
#include "perspectivecamera.hxx"
#include "infiniteplane.hxx"
#include "sampler.hxx"
#include "scene.hxx"
#include "renderingalgorithms.hxx"
#include "sphere.hxx"
#include "triangle.hxx"
#include "atmosphere.hxx"
#include "util.hxx"


// Throw a one with probability p and zero with probability 1-p.
// Let this be reflected by random variable X \in {0, 1}.
// What is the expectation E[X] and std deviation sqrt(Var[X])?
std::pair<double, double> MeanAndSigmaOfThrowingOneWithPandZeroOtherwise(double p)
{
  // Expected hit indication = 1 * P + 0 * (1-P) = P
  // Variance of single throw = (1-P)^2 * P + (0-P)^2 * (1-P) = P - 2 PP + PPP + PP - PPP = P - PP
  return std::make_pair(p, std::sqrt(p-p*p));
}

// For a sample average <X_i> = 1/N sum_i X_i, 
// <X_i> is a random variable itself. It has some distribution
// around the true expectation E[X]. The standard deviation 
// of this distribution is:
double SigmaOfAverage(int N, double sample_sigma)
{
  return sample_sigma/std::sqrt(N);
}


void CheckNumberOfSamplesInBin(const char *name, int num_smpl_in_bin, int total_num_smpl, double p_of_bin, double number_of_sigmas_threshold=3.)
{
  double mean, sigma;
  std::tie(mean, sigma) = MeanAndSigmaOfThrowingOneWithPandZeroOtherwise(p_of_bin);
  mean *= total_num_smpl;
  sigma = SigmaOfAverage(total_num_smpl, sigma * total_num_smpl);
  if (name)
    std::cout << "Expected in " << name << ": " << mean << "+/-" << sigma << " Actual: " << num_smpl_in_bin << " of " << total_num_smpl << std::endl;
  EXPECT_NEAR(num_smpl_in_bin, mean, sigma*number_of_sigmas_threshold);
}



TEST(TestRaySegment, ExprTemplates)
{
  auto e = RaySegment{}.EndPoint();
  ASSERT_NE(typeid(e), typeid(e.eval()));
  ASSERT_EQ(typeid(e.eval()), typeid(Eigen::Matrix<double, 3, 1>));
}


TEST(TestRaySegment, EndPointNormal)
{
  Double3 p = RaySegment{{Double3(1., 0., 0.), Double3(0., 1., 0.)}, 2.}.EndPoint();
  ASSERT_EQ(p[0], 1.);
  ASSERT_EQ(p[1], 2.);
  ASSERT_EQ(p[2], 0.);
}


TEST(TestRaySegment, Reversed)
{
  RaySegment s{{{1, 2, 3}, {42, 0, 0}}, 5.};
  RaySegment r = s.Reversed();
  ASSERT_NEAR(s.EndPoint()[0], r.ray.org[0], 1.e-8);
  ASSERT_NEAR(r.EndPoint()[0], s.ray.org[0], 1.e-8);
}



TEST(TestMath, TakeFromVectorByIndices)
{
  Eigen::Array<double, 10, 1> m; m << 0, 1, 2, 3, 4, 5, 6, 7, 8, 9;
  Eigen::Array<double, 3, 1> v = Take(m, Index3{3,4,9});
  ASSERT_EQ(v[0], 3);
  ASSERT_EQ(v[1], 4);
  ASSERT_EQ(v[2], 9);
}


TEST(TestMath, OrthogonalSystemZAligned)
{
  Double3 directions[] = {
    { 1., 0., 0. },
    { 0., 1., 0. },
    { 0., 0., 3. },
    { 1., 1., 0. }
  };
  for (auto dir : directions)
  {
    Eigen::Matrix<double, 3, 3> m = OrthogonalSystemZAligned(Normalized(dir));
    std::cout << "dir = " << dir << std::endl;
    std::cout << "m{dir} = " << m << std::endl;
    EXPECT_TRUE(m.isUnitary(1.e-6));
    ASSERT_NEAR(m.determinant(), 1., 1.e-6);
    auto mi = m.inverse().eval();
    auto dir_local = mi * dir;
    EXPECT_NEAR(dir_local[0], 0., 1.e-6);
    EXPECT_NEAR(dir_local[1], 0., 1.e-6);
    EXPECT_NEAR(dir_local[2], Length(dir), 1.e-6);
  }
}


TEST(TestMath, TowerSampling)
{
  Sampler sampler;
  double probs[] = { 0, 1, 5, 1, 0 };
  constexpr int n = sizeof(probs)/sizeof(double);
  double norm = 0.;
  for (auto p : probs)
    norm += p;
  for (auto &p : probs)
    p /= norm;
  int bins[n] = {};
  constexpr int Nsamples = 1000;
  for (int i = 0; i < Nsamples; ++i)
  {
    int bin = TowerSampling<n>(probs, sampler.Uniform01());
    EXPECT_GE(bin, 0);
    EXPECT_LE(bin, n-1);
    bins[bin]++;
  }
  for (int i=0; i<n; ++i)
  {
    CheckNumberOfSamplesInBin(strconcat("Bin[",i,"]").c_str(), bins[i], Nsamples, probs[i]);
  }
}


TEST(Spectral, RGBConversion)
{
  using namespace Color;
  std::array<RGB, 10> trials = {{
    { 0._rgb, 0._rgb, 0._rgb },
    { 1._rgb, 0._rgb, 0._rgb },
    { 0._rgb, 1._rgb, 0._rgb },
    { 0._rgb, 0._rgb, 1._rgb },
    { 1._rgb, 1._rgb, 0._rgb },
    { 1._rgb, 0._rgb, 1._rgb },
    { 0._rgb, 1._rgb, 1._rgb },
    { 1._rgb, 1._rgb, 1._rgb },
    { 1._rgb, 0.5_rgb, 0.3_rgb },
    { 0.5_rgb, 0.8_rgb, 0.9_rgb },
  }};
  for (RGB trial : trials)
  {
    SpectralN s = RGBToSpectrum(trial);
    for (int i=0; i<s.size(); ++i)
      EXPECT_GE(s[i], 0.);
    RGB back = SpectrumToRGB(s);
    EXPECT_NEAR(value(trial[0]), value(back[0]), 1.3e-2);
    EXPECT_NEAR(value(trial[1]), value(back[1]), 1.3e-2);
    EXPECT_NEAR(value(trial[2]), value(back[2]), 1.3e-2);
  }
}


TEST(Spectral, RGBConversionLinearity)
{
  using namespace Color;
  SpectralN spectra[3] = {
    SpectralN::Zero(), SpectralN::Zero(), SpectralN::Zero()};
  spectra[0][0] = 1.;
  spectra[1][2] = 1.;
  spectra[2][NBINS-1] = 1.;
  auto CheckLinear = [](const SpectralN &a, const SpectralN &b)
  {
    RGB rgb_a = SpectrumToRGB(a);
    RGB rgb_b = SpectrumToRGB(b);
    RGB rgb_ab = SpectrumToRGB(a+b);
    EXPECT_LE(value((rgb_ab - rgb_b - rgb_a).abs().maxCoeff()), 1.e-3);
  };
  CheckLinear(spectra[0], spectra[1]);
  CheckLinear(spectra[0], spectra[2]);
  CheckLinear(spectra[1], spectra[2]);
}


TEST(Spectral, RGBConversionSelection)
{
  /* Back and forth transform using RGBToSpectralSelection and SpectralSelectionToRGB.
   * Only part of the spectrum is considered by these function, namely the wavelengths
   * given by the indices. Because of that, the complete reconstruction of the RGB 
   * original requires summation over parts of the spectrum. It is basically an inner
   * product of the spectrum with the color matching function, except that on each iteration 
   * is not one wavelengths but multiple added to the output. */
  
  using namespace Color;
  auto rgb = RGB(0.8_rgb, 0.2_rgb, 0.6_rgb);
  RGB converted_rgb = RGB::Zero();
  for (int lambda=0; lambda<Color::NBINS-2; lambda += static_size<Spectral3>())
  {
    Index3 idx{lambda, lambda+1, lambda+2};
    converted_rgb += SpectralSelectionToRGB(RGBToSpectralSelection(rgb, idx), idx);
  }
  EXPECT_NEAR(value(rgb[0]), value(converted_rgb[0]), 1.e-2);
  EXPECT_NEAR(value(rgb[1]), value(converted_rgb[1]), 1.e-2);
  EXPECT_NEAR(value(rgb[2]), value(converted_rgb[2]), 1.e-2);
}



class TestIntersection : public testing::Test
{
protected:
  RaySurfaceIntersection intersection;
  double distance;
  
  void Intersect(const Primitive &prim, const Ray &ray, bool expect_hit = true)
  {
    RaySegment rs{ray, LargeNumber};
    HitId hit;
    bool bhit = prim.Intersect(rs.ray, rs.length, hit);
    ASSERT_TRUE(bhit == expect_hit);
    if (bhit)
    {
      distance = rs.length;
      intersection = RaySurfaceIntersection{hit, rs};
    }
  }
  
  void CheckPosition(const Double3 &p) const
  {
    for (int i=0; i<3; ++i)
      EXPECT_NEAR(intersection.pos[i], p[i], 1.e-6);
  }
  
  void CheckNormal(const Double3 &n)
  {
    for (int i=0; i<3; ++i)
      EXPECT_NEAR(intersection.normal[i], n[i], 1.e-6);
  }
};


TEST_F(TestIntersection, Sphere)
{
  Sphere s{{0., 0., 2.}, 2.};
  Intersect(s, {{0., 0., -1.},{0., 0., 1.}});
  EXPECT_NEAR(distance, 1., 1.e-6);
  CheckPosition({0., 0., 0.});
  CheckNormal({0.,0.,-1.});
}


//TEST_F(TestIntersection, SphereRepeatedIntersection)
//{
//  Sphere s{{0., 0., 0.}, 6300.};
//  Ray r{{-10000., 0., 0.}, {1., 0., 0.}};
//  std::cout << std::setprecision(std::numeric_limits<double>::digits10) << "one ulp at r=6300 is" << boost::math::float_advance<double>(6300.,1) - 6300. << std::endl;
//  for (int iter=0; iter<100; ++iter)
//  {
//    Intersect(s, r);
//    EXPECT_LE(intersection.pos[0], 0.);
//    EXPECT_NEAR(intersection.pos[1], 0., 1.e-6);
//    EXPECT_NEAR(intersection.pos[2], 0., 1.e-6);
//    r.org[0] = (r.org[0]+6300.)*0.5 - 6300.;
//    std::cout << std::setprecision(std::numeric_limits<double>::digits10) << "start=" << r.org[0] << " xpos=" << intersection.pos[0] << std::endl;
//  }
//}


TEST_F(TestIntersection, Triangle)
{
  /*     x
   *   / |
   *  /  |
   * x---x
   * Depiction of the triangle */
  double q = 0.5;
  Triangle prim{{-q, -q, 0}, {q, -q, 0}, {q, q, 0}};
  Intersect(prim, Ray{{0.1, 0., -1.},{0., 0., 1.}});
  EXPECT_NEAR(distance, 1., 1.e-6);
  CheckPosition({0.1, 0., 0.});
  CheckNormal({0., 0., -1.});
}


TEST_F(TestIntersection, TriangleEdgeCase)
{
  double q = 0.5;
  Triangle prim{{-q, -q, 0}, {q, -q, 0}, {q, q, 0}};
  Intersect(prim, Ray{{0., 0., -1.},{0., 0., 1.}});
  EXPECT_NEAR(distance, 1., 1.e-6);
  CheckPosition({0., 0., 0.});
  CheckNormal({0., 0., -1.});
  // Slightly offset the ray, there should be no intersection.
  Intersect(prim, Ray{{-Epsilon, 0., -1.},{0., 0., 1.}}, false);
  // An adjacent triangle should catch the ray, however.
  // This one covers the top left corner.
  Triangle prim_top_left{{-q, -q, 0}, {-q, q, 0}, {q, q, 0}};
  Intersect(prim_top_left, Ray{{-Epsilon, 0., -1.},{0., 0., 1.}}, true);
}


inline RaySegment MakeSegmentAt(const RaySurfaceIntersection &intersection, const Double3 &ray_dir)
{
 return RaySegment{
   {intersection.pos + AntiSelfIntersectionOffset(intersection, RAY_EPSILON, ray_dir), ray_dir},
   LargeNumber
 };
}


TEST(TestMath, Reflect)
{
  Double3 n{0., 1., 0.};
  auto in = Normalized(Double3{0., 1., 2.});
  Double3 out = Reflected(in, n);
  auto out_expected = Normalized(Double3{0., 1., -2.});
  ASSERT_GE(Dot(out, out_expected), 0.99);
}


TEST(TestMath, SphereIntersectionMadness)
{
  Sampler sampler;
  Double3 sphere_org{0., 0., 2.};
  double sphere_rad = 6300;
  Sphere s{sphere_org, sphere_rad};
  const int N = 100;
  for (int num = 0; num < N; ++num)
  {
    // Sample random position outside the sphere as start point.
    // And position inside the sphere as end point.
    // Intersection is thus guaranteed.
    Double3 org = 
      SampleTrafo::ToUniformSphere(sampler.UniformUnitSquare())
      * sphere_rad * 10. + sphere_org;
    Double3 target = 
      SampleTrafo::ToUniformHemisphere(sampler.UniformUnitSquare())
      * sphere_rad * 0.99 + sphere_org;
    Double3 dir = target - org; 
    Normalize(dir);
    
    // Shoot ray to the inside. Expect intersection at the
    // front side of the sphere.
    RaySegment rs{{org, dir}, LargeNumber};
    HitId hit1;
    bool bhit = s.Intersect(rs.ray, rs.length, hit1);
    ASSERT_TRUE(bhit);
    RaySurfaceIntersection intersect1{hit1, rs};
    ASSERT_LE(Length(org - intersect1.pos), Length(org - sphere_org));
    
    // Put the origina at at the intersection and shoot a ray to the outside
    // in a random direction. Expect no further hit.
    auto m  = OrthogonalSystemZAligned(intersect1.geometry_normal);
    auto new_dir = m * SampleTrafo::ToUniformHemisphere(sampler.UniformUnitSquare());
    rs = MakeSegmentAt(intersect1, new_dir);
    bhit = s.Intersect(rs.ray, rs.length, hit1);
    ASSERT_FALSE(bhit);
    ASSERT_EQ(rs.length, LargeNumber);
  }
}


TEST(TestMath, SphereIntersectionMadness2)
{
  Sampler sampler;
  Double3 sphere_org{0., 0., 2.};
  double sphere_rad = 6300;
  Sphere s{sphere_org, sphere_rad};
  RaySegment rs{{sphere_org, {1., 0., 0.}}, LargeNumber};
  HitId hit, last_hit;
  const int N = 100;
  for (int num = 0; num < N; ++num)
  {
    bool bhit = s.Intersect(rs.ray, rs.length, hit); //, last_hit, HitId());
    ASSERT_TRUE(bhit);
    RaySurfaceIntersection intersect{hit, rs};
    double rho = Length(intersect.pos-sphere_org);
    auto m  = OrthogonalSystemZAligned(intersect.normal);
    auto new_dir = m * SampleTrafo::ToUniformHemisphere(sampler.UniformUnitSquare());
    rs = MakeSegmentAt(intersect, new_dir);
    rho = Length(rs.ray.org-sphere_org);
    EXPECT_LE(rho, sphere_rad);
    last_hit = hit;
  }
}


TEST(TestMath, SphereIntersectionMadness3)
{
  Sampler sampler;
  Scene scene;
  double rad1 = 6300.;
  double rad2 = 6350.;
  scene.AddPrimitive<Sphere>(Double3{0.}, rad1);
  scene.AddPrimitive<Sphere>(Double3{0.}, rad2);
  scene.BuildAccelStructure();
  Double3 start_point = {0.5*(rad1+rad2), 0., 0.};
  double max_dist_from_start = 0.;
  RaySegment rs{{start_point, {1., 0., 0.}}, LargeNumber};
  HitId hit, last_hit;
  const int N = 100;
  for (int num = 0; num < N; ++num)
  {
    hit = scene.MakeIntersectionCalculator().First(rs.ray, rs.length);
    ASSERT_TRUE((bool)hit);
    RaySurfaceIntersection intersect{hit, rs};
    double rho = Length(intersect.pos);
    max_dist_from_start = std::max(max_dist_from_start, Length(intersect.pos-start_point));
    EXPECT_LE(rho-RAY_EPSILON, rad2);
    EXPECT_GE(rho+RAY_EPSILON, rad1);
    auto m  = OrthogonalSystemZAligned(intersect.normal);
    auto new_dir = m * SampleTrafo::ToUniformHemisphere(sampler.UniformUnitSquare());
    rs = MakeSegmentAt(intersect, new_dir);
    last_hit = hit;
  }
  EXPECT_GE(max_dist_from_start, rad1*1.e-3);
}



TEST(Display, Open)
{
  ImageDisplay display;
  Image img(128, 128);
  img.SetColor(255, 0, 0);
  img.DrawLine(0, 0, 128, 128, 5);
  img.DrawLine(0, 128, 128, 0, 5);
  display.show(img);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  img.SetColor(255,255,255);
  img.DrawLine(64, 0, 64, 128);
  img.DrawLine(0, 64, 128, 64);
  display.show(img);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
}



class RandomSamplingFixture : public ::testing::Test
{
protected:
  Sampler sampler;
public:
  RandomSamplingFixture() {}
};


TEST_F(RandomSamplingFixture, Uniform01)
{
  static constexpr int N = 1000;
  double buffer[N];
  sampler.Uniform01(buffer, N);
  double max_elem = *std::max_element(buffer, buffer+N);
  double min_elem = *std::min_element(buffer, buffer+N);
  ASSERT_LE(max_elem, 1.0);
  ASSERT_GE(max_elem, 0.5);  // With pretty high probability ...
  ASSERT_GE(min_elem, 0.0);
  ASSERT_LE(min_elem, 0.5);
}


TEST_F(RandomSamplingFixture, UniformIntInclusive)
{
  int i = sampler.UniformInt(10,10);
  ASSERT_EQ(i, 10);
}


TEST_F(RandomSamplingFixture, UniformIntDistribution)
{
  // Put random throws in bins and do statistics on the number of hits per bin.
  static constexpr int N = 5;
  int counts[N] = { 0, 0, 0, 0, 0 };
  static constexpr int M = N * 1000;
  for (int i = 0; i < M; ++i)
  {
    int k = sampler.UniformInt(0,N-1);
    ASSERT_LE(k, N-1);
    ASSERT_GE(k, 0);
    ++counts[k];
  }
  for (int k = 0; k < N; ++k)
  {
    std::stringstream ss; ss << "'uniform int bin " << k << "'";
    CheckNumberOfSamplesInBin(ss.str().c_str(), counts[k], M, 1./N);
  }
}


TEST_F(RandomSamplingFixture, UniformSphereDistribution)
{
  static constexpr int N = 100;
  for (int i=0; i<N; ++i)
  {
    Double3 v = SampleTrafo::ToUniformSphere(sampler.UniformUnitSquare());
    double len = Length(v);
    ASSERT_NEAR(len, 1.0, 1.e-6);
  }
}


TEST_F(RandomSamplingFixture, UniformHemisphereDistribution)
{
  static constexpr int N = 100;
  for (int i=0; i<N; ++i)
  {
    Double3 v = SampleTrafo::ToUniformHemisphere(sampler.UniformUnitSquare());
    double len = Length(v);
    ASSERT_NEAR(len, 1.0, 1.e-6);
    ASSERT_GE(v[2], 0.);
  }
}


TEST_F(RandomSamplingFixture, CosHemisphereDistribution)
{
  static constexpr int N = 100;
  int n_samples_per_quadrant[2][2] = { {0, 0}, {0, 0} };
  int n_samples_z_test[3] = { 0, 0, 0, };
  const double z_thresholds[3] = { 0.25 * Pi*0.5, 0.5 * Pi*0.5, 0.75 * Pi*0.5 };
  for (int i=0; i<N; ++i)
  {
    auto v = SampleTrafo::ToCosHemisphere(sampler.UniformUnitSquare());
    ASSERT_NEAR(Length(v), 1.0, 1.e-6);
    ASSERT_GE(v[2], 0.);
    ++n_samples_per_quadrant[v[0]<0. ? 0 : 1][v[1]<0. ? 0 : 1];
    auto angle = std::acos(v[2]);
    if (angle<z_thresholds[2]) ++n_samples_z_test[2];
    if (angle<z_thresholds[1]) ++n_samples_z_test[1];
    if (angle<z_thresholds[0]) ++n_samples_z_test[0];
  }
  const char* bin_names[2][2] = {
    { "00", "01" }, { "10", "11" }
  };
  for (int qx = 0; qx <= 1; ++qx)
  for (int qy = 0; qy <= 1; ++qy)
  {
    CheckNumberOfSamplesInBin(bin_names[qx][qy], n_samples_per_quadrant[qx][qy], N, 0.25);
  }
  for (int z_bin = 0; z_bin < 3; ++z_bin)
  {
    std::stringstream ss; ss << "'Theta<" << (z_thresholds[z_bin]*180./Pi) <<"deg'";
    double p = std::pow(std::sin(z_thresholds[z_bin]), 2.);
    CheckNumberOfSamplesInBin(ss.str().c_str(), n_samples_z_test[z_bin], N, p);
  }
}


TEST_F(RandomSamplingFixture, DiscDistribution)
{
  static constexpr int N = 100;
  static constexpr int NUM_REGIONS=5;
  int n_samples[NUM_REGIONS] = {};
  const double center_disc_radius = 0.5;
  const double center_disc_area = Pi*Sqr(center_disc_radius);
  const double outer_quadrant_area = (Pi - center_disc_area) * 0.25;
  const double region_area[NUM_REGIONS] = 
  {
    center_disc_area,
    outer_quadrant_area,
    outer_quadrant_area,
    outer_quadrant_area,
    outer_quadrant_area
  };
  for (int i=0; i<N; ++i)
  {
    Double3 v = SampleTrafo::ToUniformDisc(sampler.UniformUnitSquare());
    ASSERT_EQ(v[2], 0.);
    ASSERT_LE(Length(v), 1.);
    int region = 0;
    if (Length(v)>center_disc_radius)
    {
      if (v[0]>0 && v[1]>0) region=1;
      if (v[0]>0 && v[1]<=0) region=2;
      if (v[0]<=0 && v[1]>0) region=3;
      if (v[0]<=0 && v[1]<=0) region=4;
    }
    ++n_samples[region];
  }
  for (int bin=0; bin<NUM_REGIONS; ++bin)
  {
    CheckNumberOfSamplesInBin("disc_bin", n_samples[bin], N, region_area[bin]/Pi);
  }
}


TEST_F(RandomSamplingFixture, HomogeneousTransmissionSampling)
{
  ImageDisplay display;
  Image img{500, 10};
  img.SetColor(64, 64, 64);
  RGB length_scales{1._rgb, 5._rgb, 10._rgb};
  double cutoff_length = 5.; // Integrate transmission T(x) up to this x.
  double img_dx = img.width() / cutoff_length / 2.;
  img.DrawRect(0, 0, img_dx * cutoff_length, img.height()-1);
  Index3 lambda_idx = Color::LambdaIdxClosestToRGBPrimaries();
  SpectralN sigma_s = Color::RGBToSpectrum(1._rgb/length_scales);
  SpectralN sigma_a = Color::RGBToSpectrum(1._rgb/length_scales);
  SpectralN sigma_t = sigma_s + sigma_a;
  HomogeneousMedium medium{
    sigma_s, 
    sigma_a, 
    0};
  int N = 1000;
  Spectral3 integral{0.};
  for (int i=0; i<N; ++i)
  {
    PathContext context{lambda_idx};
    Medium::InteractionSample s = medium.SampleInteractionPoint(RaySegment{{{0.,0.,0.}, {0., 0., 1.,}}, cutoff_length}, sampler, context);
    if (s.t  > cutoff_length)
      img.SetColor(255, 0, 0);
    else
    {
      img.SetColor(255, 255, 255);
      // Estimate transmission by integrating sigma_e*T(x).
      // Divide out sigma_s which is baked into the weight.
      // Multiply by sigma_e.
      SpectralN integrand = (sigma_a + sigma_s) / sigma_s;
      integral += s.weight * Take(integrand, lambda_idx);
    }
    int imgx = std::min<int>(img.width()-1, s.t * img_dx);
    img.DrawLine(imgx, 0, imgx, img.height()-1);
  }
  integral *= 1./N;
  Spectral3 exact_solution = Take((1. - (-sigma_t*cutoff_length).exp()).eval(), lambda_idx);
  for (int k=0; k<static_size<Spectral3>(); ++k)
    EXPECT_NEAR(integral[k], exact_solution[k], 0.1 * integral[k]);
  display.show(img);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
}


TEST_F(RandomSamplingFixture, TriangleSampling)
{
  Eigen::Matrix3d m;
  auto X = m.col(0);
  auto Y = m.col(1);
  auto Z = m.col(2);
  X = Double3{0.4, 0.2, 0.};
  Y = Double3{0.1, 0.8, 0.};
  Z = Double3{0. , 0. , 1.};
  Eigen::Matrix3d minv = m.inverse();
  Double3 offset{0.1, 0.1, 0.1};
  Triangle t(
    offset,
    offset+X,
    offset+Y);
  static constexpr int NUM_SAMPLES = 100;
  for (int i=0; i<NUM_SAMPLES; ++i)
  {
    {
      Double3 barry = SampleTrafo::ToTriangleBarycentricCoords(sampler.UniformUnitSquare());
      EXPECT_NEAR(barry[0]+barry[1]+barry[2], 1., 1.e-3);
      EXPECT_GE(barry[0], 0.); EXPECT_LE(barry[0], 1.);
      EXPECT_GE(barry[1], 0.); EXPECT_LE(barry[1], 1.);
      EXPECT_GE(barry[2], 0.); EXPECT_LE(barry[2], 1.);
    }
    HitId hit = t.SampleUniformPosition(sampler);
    Double3 pos, normal, shading_normal;
    t.GetLocalGeometry(hit, pos, normal, shading_normal);
    Double3 local = minv * (pos - offset);
    EXPECT_NEAR(local[2], 0., 1.e-3);
    EXPECT_GE(local[0], 0.); EXPECT_LE(local[0], 1.);
    EXPECT_GE(local[1], 0.); EXPECT_LE(local[1], 1.);
    EXPECT_LE(local[0] + local[1], 1.);
  }
}


class PhasefunctionTests : public testing::Test
{
public:
  Sampler sampler;
  static constexpr int Nbins = 9;
  std::ofstream output;  // If really deperate!

  // Use a cube mapping of 6 uniform grids to the 6 side faces of a cube projected to the unit sphere.
  using CubeBinDouble = std::array<std::array<std::array<double, Nbins>, Nbins>, 6>;
  using CubeVertexDouble = std::array<std::array<std::array<double, Nbins+1>, Nbins+1>, 6>;
    
  void TestPfSampling(const PhaseFunctions::PhaseFunction &pf, const Double3 &reverse_incident_dir, int num_samples, double number_of_sigmas_threshold)
  {
    /* I want to check if the sampling frequency of some solid angle areas match the probabilities
     * returned by the Evaluate function. I further want to validate the normalization constraint. */
    auto bin_probabilities = ComputeBinProbabilities(pf, reverse_incident_dir);
    Spectral3 integral{0.};
    int bin_sample_count[6][Nbins][Nbins] = {}; // Zero initialize?
    for (int snum = 0; snum<num_samples; ++snum)
    {
      auto smpl = pf.SampleDirection(reverse_incident_dir, sampler);
      int side, i, j;
      CalcCubeIndices(smpl.coordinates, side, i, j);
      i = std::max(0, std::min(i, Nbins-1));
      j = std::max(0, std::min(j, Nbins-1));
      bin_sample_count[side][i][j] += 1.;
      integral += smpl.value / smpl.pdf_or_pmf;
      if (output.is_open())
        output << smpl.coordinates[0] << " " << smpl.coordinates[1] << " " << smpl.coordinates[2] << " " << smpl.pdf_or_pmf << std::endl;
    }
    integral /= num_samples; // Due to the normalization contraint, this integral should equal 1.
    EXPECT_NEAR(integral[0], 1., 0.01);
    EXPECT_NEAR(integral[1], 1., 0.01);
    EXPECT_NEAR(integral[2], 1., 0.01);
    for (int side = 0; side < 6; ++side)
    {
      for (int i = 0; i<Nbins; ++i)
      {
        for (int j = 0; j<Nbins; ++j)
        {
          CheckNumberOfSamplesInBin(
            nullptr, //strconcat(side,"[",i,",",j,"]").c_str(),
            bin_sample_count[side][i][j], 
            num_samples, 
            bin_probabilities[side][i][j],
            number_of_sigmas_threshold);
        }
      }
    }
  }
 
 
  CubeBinDouble ComputeBinProbabilities(const PhaseFunctions::PhaseFunction &pf, const Double3 &reverse_incident_dir)
  {
    // Cube projection.
    // +/-x,y,z which makes 6 grid, one per side face, and i,j indices of the corresponding grid.
    CubeVertexDouble bin_corner_densities;
    CubeBinDouble bin_probabilities;
    for (int side = 0; side < 6; ++side)
    {
      for (int i = 0; i<=Nbins; ++i)
      {
        for (int j=0; j<=Nbins; ++j)
        {
          Double3 corner = CalcCubeGridVertex(side, i, j);
          pf.Evaluate(reverse_incident_dir, corner, &bin_corner_densities[side][i][j]);
          if (output.is_open())
            output << corner[0] << " " << corner[1] << " " << corner[2] << " " << bin_corner_densities[side][i][j] << std::endl;
        }
      }
    }
    if (output.is_open())
      output << std::endl;

    double total_probability = 0.;
    double total_solid_angle = 0.;
    for (int side = 0; side < 6; ++side)
    {
      for (int i = 0; i<Nbins; ++i)
      {
        for (int j = 0; j<Nbins; ++j)
        {
          Double3 w[2][2] = {
            { 
              CalcCubeGridVertex(side, i, j),
              CalcCubeGridVertex(side, i, j+1) 
            },
            {
              CalcCubeGridVertex(side, i+1, j),
              CalcCubeGridVertex(side, i+1, j+1)
            }
          };
          // Crude approximation of the surface integral over pieces of the unit sphere.
          // https://en.wikipedia.org/wiki/Surface_integral
          Double3 du = 0.5 * (w[1][0]+w[1][1] - w[0][0]-w[0][1]);
          Double3 dv = 0.5 * (w[0][1]+w[1][1] - w[0][0]-w[1][0]);
          double spanned_solid_angle = Length(Cross(du,dv));
          bin_probabilities[side][i][j] = 0.25 * spanned_solid_angle *
            (bin_corner_densities[side][i][j] + 
             bin_corner_densities[side][i+1][j] + 
             bin_corner_densities[side][i+1][j+1] + 
             bin_corner_densities[side][i][j+1]);
          total_probability += bin_probabilities[side][i][j];
          total_solid_angle += spanned_solid_angle;
        }
      }
    }
    EXPECT_NEAR(total_solid_angle, UnitSphereSurfaceArea, 0.1);
    EXPECT_NEAR(total_probability, 1.0, 0.01);
//    // Renormalize to make probabilities sum to one.
//    // This accounts for inaccuracies through which
//    // the sum of the original probabilities deviated from 1.
//    for (int side = 0; side < 6; ++side)
//    {
//      for (int i = 0; i<Nbins; ++i)
//      {
//        for (int j = 0; j<Nbins; ++j)
//        {
//          bin_probabilities[side][i][j] /= total_probability;
//        }
//      }
//    }
    return bin_probabilities;
  }
  
  
  Double3 CalcCubeGridVertex(int side, int i, int j)
  {
    double delta = 1./Nbins;
    double u = 2.*i*delta-1.;
    double v = 2.*j*delta-1.;
    Double3 x;
    switch (side)
    {
      case 0:
        x = Double3{1., u, v}; break;
      case 1:
        x = Double3{-1., u, v}; break;
      case 2:
        x = Double3{u, 1., v}; break;
      case 3:
        x = Double3{u, -1., v}; break;
      case 4:
        x = Double3{u, v, 1.}; break;
      case 5:
        x = Double3{u, v, -1.}; break;
      default:
        EXPECT_FALSE((bool)"We should not get here.");
    }
    return Normalized(x);
  }
  
  
  void CalcCubeIndices(const Double3 &w, int &side, int &i, int &j)
  {
    double u, v, z;
    int max_abs_axis;
    z = w.array().abs().maxCoeff(&max_abs_axis);
    side = max_abs_axis*2 + (w[max_abs_axis]>0 ? 0 : 1);
    switch(side)
    {
      case 0:
        u = w[1]; v = w[2]; z = w[0]; break;
      case 1:
        u = w[1]; v = w[2]; z = -w[0]; break;
      case 2:
        u = w[0]; v = w[2]; z = w[1]; break;
      case 3:
        u = w[0]; v = w[2]; z = -w[1]; break;
      case 4:
        u = w[0]; v = w[1]; z = w[2]; break;
      case 5:
        u = w[0]; v = w[1]; z = -w[2]; break;
      default:
        FAIL();
    }
    ASSERT_GT(z, 0.);
    u /= z;
    v /= z;
    i = (u+1.)*0.5*Nbins;
    j = (v+1.)*0.5*Nbins;
  }
};


TEST_F(PhasefunctionTests, CubeGridMapping)
{
  // I need a test for the test!
  for (int side = 0; side < 6; ++side)
  {
    std::cout << "Corners of side " << side << std::endl;
    std::cout << CalcCubeGridVertex(side, 0, 0) << std::endl;
    std::cout << CalcCubeGridVertex(side, Nbins, 0) << std::endl;
    std::cout << CalcCubeGridVertex(side, Nbins, Nbins) << std::endl;
    std::cout << CalcCubeGridVertex(side, 0, Nbins) << std::endl;

    for (int i = 0; i<Nbins; ++i)
    {
      for (int j=0; j<Nbins; ++j)
      {
        Double3 center = 0.25 * (
            CalcCubeGridVertex(side, i, j) +
            CalcCubeGridVertex(side, i+1, j) +
            CalcCubeGridVertex(side, i+1, j+1) +
            CalcCubeGridVertex(side, i, j+1));
        int side_, i_, j_;
        CalcCubeIndices(center, side_, i_, j_);
        EXPECT_EQ(side_, side);
        EXPECT_EQ(i, i_);
        EXPECT_EQ(j, j_);
      }
    }
  }
}


TEST_F(PhasefunctionTests, Uniform)
{
  PhaseFunctions::Uniform pf;
  TestPfSampling(pf, Double3{0,0,1}, 10000, 3.5);
}


TEST_F(PhasefunctionTests, Rayleigh)
{
  // output.open("PhasefunctionTests.Rayleight.txt");
  PhaseFunctions::Rayleigh pf;
  TestPfSampling(pf, Double3{0,0,1}, 10000, 3.5);
}


TEST_F(PhasefunctionTests, HenleyGreenstein)
{
  //output.open("PhasefunctionTests.HenleyGreenstein.txt");
  PhaseFunctions::HenleyGreenstein pf(0.4);
  TestPfSampling(pf, Double3{0,0,1}, 10000, 4.0);
}


TEST_F(PhasefunctionTests, Combined)
{
  PhaseFunctions::HenleyGreenstein pf1{0.4};
  PhaseFunctions::Uniform pf2;
  PhaseFunctions::Combined pf{{1., 1., 1.}, {.1, .2, .3}, pf1, {.3, .4, .5}, pf2};
  TestPfSampling(pf, Double3{0,0,1}, 10000, 3.5);
}


TEST_F(PhasefunctionTests, SimpleCombined)
{
  PhaseFunctions::HenleyGreenstein pf1{0.4};
  PhaseFunctions::Uniform pf2;
  PhaseFunctions::SimpleCombined pf{{.1, .2, .3}, pf1, {.3, .4, .5}, pf2};
  TestPfSampling(pf, Double3{0,0,1}, 10000, 3.5);
}


class StratifiedFixture : public testing::Test
{
public:
  Stratified2DSamples stratified;
  StratifiedFixture() :
    stratified(2,2)
  {}
  
  Double2 Get(Double2 v) 
  { 
    return stratified.UniformUnitSquare(v); 
  }
  
  void CheckTrafo(double r1, double r2)
  {
    stratified.current_x = 0;
    stratified.current_y = 0;
    Double2 r = Get({r1, r2});
    ASSERT_FLOAT_EQ(r[0], r1 * 0.5);
    ASSERT_FLOAT_EQ(r[1], r2 * 0.5);
  }
  
  void Next()
  {
    stratified.UniformUnitSquare({0.,0.});
  }
};


TEST_F(StratifiedFixture, Incrementing)
{
  ASSERT_EQ(stratified.current_x, 0);
  ASSERT_EQ(stratified.current_y, 0);
  Next();
  ASSERT_EQ(stratified.current_x, 1);
  ASSERT_EQ(stratified.current_y, 0);
  Next();
  ASSERT_EQ(stratified.current_x, 0);
  ASSERT_EQ(stratified.current_y, 1);
  Next();
  ASSERT_EQ(stratified.current_x, 1);
  ASSERT_EQ(stratified.current_y, 1);
  Next();
  ASSERT_EQ(stratified.current_x, 0);
  ASSERT_EQ(stratified.current_y, 0);
  Next();
}


TEST_F(StratifiedFixture, SampleLocation)
{
  CheckTrafo(0., 0.);
  CheckTrafo(1., 1.);
  CheckTrafo(0.5, 0.5);
}



class PerspectiveCameraTesting : public testing::Test
{
  Double3 pos{0, 0, 0},
          dir{0, 0, 1},
          up{0, 1, 0};
  double fov = 90;
  double bound_at_z1 = 0.5;
  Sampler sampler;
  InfinitePlane imageplane;
  std::unique_ptr<PerspectiveCamera> cam;
public:
  PerspectiveCameraTesting() :
    imageplane({0, 0, 1}, {0, 0, 1})
  {
  }
  
  void init(int _xres, int _yres)
  {
    cam = std::make_unique<PerspectiveCamera>(pos, dir, up, fov, _xres, _yres);
  }
  
  void run(int _pixel_x, int _pixel_y)
  {
  /* Pixel bins:
    |         i=0         |           i=1           |        .....           |         x=xres-1       |
    At distance z=1:
    x=0                   x=i/xres                 x=2/xres           ....   x=(xres-1)/xres          x=1
  */
    Double3 possum{0,0,0}, possqr{0,0,0};
    Box box;
    constexpr int Nsamples = 100;
    for (int i = 0; i < Nsamples; ++i)
    {
      auto s = cam->TakeDirectionSampleFrom(
        cam->PixelToUnit({_pixel_x, _pixel_y}), pos, sampler,
        PathContext{Color::LambdaIdxClosestToRGBPrimaries()});
      HitId hit;
      double length = 100.;
      bool is_hit = imageplane.Intersect({pos, s.coordinates}, length, hit);
      ASSERT_TRUE(is_hit);
      Double3 endpos = pos + length * s.coordinates;
      possum += endpos;
      possqr += Product(endpos, endpos);
      box.Extend(endpos);
    }
    // int_x=-s-s x^2 / (2s) dx = 1/3 x^3 /2s | x=-s..s = 1/(6s) 2*(s^3) = 1/3 s^2. -> std = sqrt(1/3) s
    // s = 1 -> std = 0.577
    Double3 average = possum / Nsamples;
    Double3 stddev  = (possqr / Nsamples - Product(average, average)).array().sqrt().matrix();
    std::cout << "@Pixel: ix=" << _pixel_x << "," << _pixel_y << std::endl;
    std::cout << "Pixel: " << average << " +/- " << stddev << std::endl;
    Double3 exactpos {((_pixel_x + 0.5) * 2.0 / cam->xres - 1.0),
                      ((_pixel_y + 0.5) * 2.0 / cam->yres - 1.0),
                      1.0};
    std::cout << "Box: " << box.min << " to " << box.max << std::endl;
    Double3 radius { 2.0/cam->xres, 2.0/cam->yres, 0. };
    std::cout << "Exact: " << exactpos << " +/- " << radius << std::endl;
  }
};


TEST_F(PerspectiveCameraTesting, Sampling1)
{
  init(11,11);
  run(0,0);
  run(10,10);
  run(5,5);
}

namespace {
void CheckSceneParsedWithScopes(const Scene &scene);
}

TEST(Parser, Scopes)
{
  const char* scenestr = R"""(
s 1 2 3 0.5
transform 5 6 7
diffuse themat 1 1 1 0.9
s 0 0 0 0.5
{
transform 8 9 10
diffuse themat 1 1 1 0.3
s 11 12 13 0.5
}
s 14 15 16 0.5
)""";
  Scene scene;
  scene.ParseNFFString(scenestr);
  CheckSceneParsedWithScopes(scene);
}


TEST(Parser, ScopesAndIncludes)
{
  namespace fs = boost::filesystem;
  auto path1 = fs::temp_directory_path() / fs::unique_path("scene1-%%%%-%%%%-%%%%-%%%%.nff");
  std::cout << "scenepath1: " << path1.string() << std::endl;
  const char* scenestr1 = R"""(
transform 5 6 7
diffuse themat 1 1 1 0.9
s 0 0 0 0.5
)""";
  {
    std::ofstream os(path1.string());
    os.write(scenestr1, strlen(scenestr1));
  }
  const char* scenestr2 = R"""(
transform 8 9 10
diffuse themat 1 1 1 0.3
s 11 12 13 0.5
)""";
  auto path2 = fs::unique_path("scene2-%%%%-%%%%-%%%%-%%%%.nff"); // Relative filepath.
  auto path2_full = fs::temp_directory_path() / path2;
  std::cout << "scenepath2: " << path2.string() << std::endl;
  {
    std::ofstream os(path2_full.string());
    os.write(scenestr2, strlen(scenestr2));
  }
  const char* scenestr_fmt = R"""(
s 1 2 3 0.5
include %
{
include %
}
s 14 15 16 0.5
)""";
  auto path3 = fs::temp_directory_path() / fs::unique_path("scene3-%%%%-%%%%-%%%%-%%%%.nff");
  std::string scenestr = strformat(scenestr_fmt, path1.string(), path2.string());
  {
    std::ofstream os(path3.string());
    os.write(scenestr.c_str(), scenestr.size());
  }  
  Scene scene;
  scene.ParseNFF(path3);
  CheckSceneParsedWithScopes(scene);
}


namespace 
{
  
void CheckSceneParsedWithScopes(const Scene &scene)
{
  auto Getter = [&scene](int i) -> auto 
  { 
    return scene.GetPrimitive(i).CalcBounds().Center();
  };
  ASSERT_EQ(scene.GetNumPrimitives(), 4);
  std::array<Double3,4> c{ 
    Getter(0),
    Getter(1),
    Getter(2),
    Getter(3)
  };
  // Checking the coordinates for correct application of the transform statements.
  ASSERT_NEAR(c[0][0], 1., 1.e-3);
  ASSERT_NEAR(c[1][0], 5., 1.e-3);
  ASSERT_NEAR(c[2][0], 8.+11., 1.e-3); // Using the child scope transform.
  ASSERT_NEAR(c[3][0], 14.+5., 1.e-3); // Using the parent scope transform.
  ASSERT_EQ(scene.GetPrimitive(3).shader, scene.GetPrimitive(1).shader); // Shaders don't persist beyond scopes.
  ASSERT_NE(scene.GetPrimitive(2).shader, scene.GetPrimitive(1).shader); // Shader within the scope was actually created and assigned.
  ASSERT_NE(scene.GetPrimitive(2).shader, scene.GetPrimitive(0).shader); // Shader within the scope is not the default.
  ASSERT_NE(scene.GetPrimitive(2).shader, nullptr); // And ofc it should not be null.
}
  
}



TEST(Parser, ImportDAE)
{
  const char* scenestr = R"""(
diffuse DefaultMaterial 1 1 1 0.5
m scenes/unitcube.dae
)""";
  Scene scene;
  scene.ParseNFFString(scenestr);
  constexpr double tol = 1.e-2;
  Box outside; 
  outside.Extend({-0.5-tol, -0.5-tol, -0.5-tol}); 
  outside.Extend({0.5+tol, 0.5+tol, 0.5+tol});
  Box inside;
  inside.Extend({-0.5+tol, -0.5+tol, -0.5+tol});
  inside.Extend({0.5-tol, 0.5-tol, 0.5-tol});
  EXPECT_EQ(scene.GetNumPrimitives(), 6 * 2);
  for (int i=0; i<scene.GetNumPrimitives(); ++i)
  {
    auto* prim = dynamic_cast<const Triangle*>(&scene.GetPrimitive(i));
    ASSERT_NE(prim, nullptr);
    Box b = prim->CalcBounds();
    ASSERT_TRUE(b.InBox(outside));
    ASSERT_TRUE(!b.InBox(inside));
  }
}


TEST(Parser, ImportCompleteSceneWithDaeBearingMaterials)
{
  const char* scenestr = R"""(
v
from 0 1.2 -1.3
at 0 0.6 0
up 0 1 0
resolution 128 128
angle 50

l 0 0.75 0  1 1 1 1

diffuse white  1 1 1 0.5
diffuse red    1 0 0 0.5
diffuse green  0 1 0 0.5
diffuse blue   0 0 1 0.5
diffuse boxmat 1 1 0 0.8
diffuse spheremat 0 1 1 0.8

m scenes/cornelbox2.dae
)""";
  Scene scene;
  scene.ParseNFFString(scenestr);
  Box b = scene.CalcBounds();
  double size = Length(b.max - b.min);
  ASSERT_GE(size, 1.);
  ASSERT_LE(size, 3.);
}




namespace Atmosphere
{

TEST(SimpleAtmosphereTest, Altitude)
{
  Atmosphere::SphereGeometry geom{{0.,0.,1.}, 100.};
  Double3 pos{0., 110., 1.};
  double altitude = geom.ComputeAltitude(pos);
  EXPECT_NEAR(altitude, 10., 1.e-6);
}


TEST(SimpleAtmosphereTest, CollisionCoefficients)
{
  Atmosphere::ExponentialConstituentDistribution constituents{};
  double altitude = 10.;
  Spectral3 sigma_s, sigma_a;
  Index3 lambda_idx = Color::LambdaIdxClosestToRGBPrimaries();
  constituents.ComputeCollisionCoefficients(altitude, sigma_s, sigma_a, lambda_idx);
  double expected_sigma_s =
      std::exp(-altitude/8.)*0.0076 +
      std::exp(-altitude/1.2)*20.e-3;
  double expected_sigma_a =
      std::exp(-altitude/8.)*0.e-3 +
      std::exp(-altitude/1.2)*2.22e-3;
  EXPECT_NEAR(sigma_s[0], expected_sigma_s, expected_sigma_s*1.e-2);
  EXPECT_NEAR(sigma_a[0], expected_sigma_a, expected_sigma_a*1.e-2);
}


TEST(SimpleAtmosphereTest, LowestPoint)
{
  Atmosphere::SphereGeometry geom{{0.,0.,1.}, 100.};
  { // Looking down through the atmospheric layer.
    RaySegment seg{{{-1000., 0., 101.}, {1., 0., 0.}}, 2000.};
    Double3 p = geom.ComputeLowestPointAlong(seg);
    EXPECT_NEAR(p[0], 0., 1.e-8);
    EXPECT_NEAR(p[1], 0., 1.e-8);
    EXPECT_NEAR(p[2], 101., 1.e-8);
  }

  { // Same as above but limit the distance.
    RaySegment seg{{{-1000., 0., 101.}, {1., 0., 0.}}, 20.};
    Double3 p = geom.ComputeLowestPointAlong(seg);
    EXPECT_NEAR(p[0], -1000+20., 1.e-8);
    EXPECT_NEAR(p[1], 0., 1.e-8);
    EXPECT_NEAR(p[2], 101., 1.e-8);
  }

  { // Look up
    RaySegment seg{{{0., 0., 101.}, Normalized(Double3{1., 0., 10.})}, 5.};
    Double3 p = geom.ComputeLowestPointAlong(seg);
    EXPECT_NEAR(p[0], 0., 1.e-8);
    EXPECT_NEAR(p[1], 0., 1.e-8);
    EXPECT_NEAR(p[2], 101., 1.e-8);
  }
}


TEST(AtmosphereTest, LoadTabulatedData)
{
  const std::string filename = "scenes/earth_atmosphere_collision_coefficients.json";
  std::ifstream is(filename.c_str(), std::ios::binary | std::ios::ate);
  std::string data;
  { // Following http://en.cppreference.com/w/cpp/io/basic_istream/read
    auto size = is.tellg();
    data.resize(size);
    is.seekg(0);
    is.read(&data[0], size);
  }
  rapidjson::Document d;
  d.Parse(data.c_str());
  ASSERT_TRUE(d.IsObject());
  const rapidjson::Value& val = d["H0"];
  ASSERT_TRUE(std::isfinite(val.GetDouble()));
  const auto& sigma_t = d["sigma_t"]; // Note: My version of rapidjson is age old. In up to date versions this should read blabla.GetArray().
  ASSERT_GT(sigma_t.Size(), 0);
  const auto& sigma_t_at_altitude0 = sigma_t[0];
  ASSERT_EQ(sigma_t_at_altitude0.Size(), Color::NBINS);
  const auto& value = sigma_t_at_altitude0[0];
  ASSERT_TRUE(std::isfinite(value.GetDouble()));
}


}


TEST(Misc, Sample)
{
  struct Testing {};
  Sample<double, double, Testing> smpl_pdf{1., 1., 42.};
  Sample<double, double, Testing> smpl_pmf{1., 1., 42.};
  Sample<double, double, Testing> zero_pmf{1., 1., 0.};
  Sample<double, double, Testing> zero_pdf{1., 1., 0.};
  SetPmfFlag(zero_pmf);
  SetPmfFlag(smpl_pmf);
  ASSERT_TRUE(IsFromPdf(smpl_pdf));
  ASSERT_TRUE(IsFromPdf(zero_pdf));
  ASSERT_FALSE(IsFromPdf(smpl_pmf));
  ASSERT_FALSE(IsFromPdf(zero_pmf));
  ASSERT_EQ(PdfValue(smpl_pdf),42.);
  ASSERT_EQ(PmfValue(smpl_pmf),42.);
}


int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}