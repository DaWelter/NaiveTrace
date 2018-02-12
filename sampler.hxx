#pragma once

#include "vec3f.hxx"
#include "util.hxx"

#include <cmath>
#include <random>
#include <array>

namespace SampleTrafo
{
Double3 ToUniformDisc(Double2 r);
Double3 ToUniformSphere(Double2 r);
Double3 ToUniformHemisphere(Double2 r);
Double3 ToUniformSphereSection(double cos_opening_angle, Double2 r);
Double3 ToCosHemisphere(Double2 r);
Double3 ToPhongHemisphere(Double2 r, double alpha);
Double3 ToBeckmanHemisphere(Double2 r, double alpha);
Double3 ToTriangleBarycentricCoords(Double2 r);
}


class Sampler
{
  std::mt19937 random_engine;
  std::uniform_real_distribution<double> uniform;
public: 
  Sampler();
  void Uniform01(double *dest, int count);
  int UniformInt(int a, int b_inclusive);
  
  inline double Uniform01() 
  {
    double r;
    Uniform01(&r, 1);
    return r;
  }
  
  inline Double2 UniformUnitSquare()
  {
    Double2 r;
    Uniform01(r.data(), 2);
    return r;
  }
};


class Stratified2DSamples
{
    int nx, ny;
    inline void FastIncrementCoordinates()
    {
      // Avoid branching. Use faster conditional moves.
      ++current_x;
      current_y = (current_x >= nx) ? current_y+1 : current_y;
      current_y = (current_y >= ny) ? 0 : current_y;
      current_x = (current_x >= nx) ? 0 : current_x;
    }
  public:
    int current_x, current_y;
    
    Stratified2DSamples(int _nx, int _ny)
      : nx(_nx), ny(_ny),
        current_x(0), current_y(0)
      {}
    
    Double2 UniformUnitSquare(Double2 r)
    {
      Double2 ret{
        (current_x + r[0]) / nx,
        (current_y + r[1]) / ny
      };
      FastIncrementCoordinates();
      return ret;
    }
};


inline double PowerHeuristic(double prob_of_estimator_evaluated, std::initializer_list<double> other_probs)
{
  double sum = 0.;
  for (double p: other_probs)
    sum += p*p;
  double pp = Sqr(prob_of_estimator_evaluated);
  assert(sum+pp > 0.);
  // One of the densities should be positive. 
  // If something goes wrong and they are all zero, there is an added Epsilon to protect form returning NaN.
  // In this case, the path throughput better be zero or the result will be wrong!
  return pp/(pp+sum+Epsilon);
}


template<int n_, class T>
inline int TowerSampling(const T *probs, T r)
{
#ifndef NDEBUG
  for (int i=0; i<n_; ++i)
    assert(probs[i] >= 0.);
#endif
  // |- p0 -|- p1 -|- p2 -|- p3 -|
  //            r <--------------| // r falls in one of those bins.
  // Linear search. Measure r from the "rear".
  int n = n_-1;
  while (r >= probs[n] && n>0)
  {
    // Shed the last bin.
    r -= probs[n];
    --n;
  }
  return n;
}


namespace PdfConversion
{
// It is important in MIS weighting to express the pdf of various sampling 
// strategies w.r.t. to the same integration domain. E.g. Solid angle or area.
// However it should be okay to compose the joint pdfs of paths using different sub-domains, e.g.:
// pdf(path) = pdf_w1(w1)*pdf_a2(x2)*pdf_a3(x3)*... This is cool as long as the product space is
// consistently used.
// Ref: Veach's Thesis and PBRT book.
inline double AreaToSolidAngle(double segment_length, const Double3 &direction, const Double3 &normal)
{
  double result = Sqr(segment_length) / std::abs(Dot(direction, normal)+Epsilon);
  assert(result >= 0 && std::isfinite(result));
  return result;
}

inline double SolidAngleToArea(double segment_length, const Double3 &direction, const Double3 &normal)
{
  double result = std::abs(Dot(direction, normal)) / (Sqr(segment_length)+Epsilon);
  assert(result >= 0 && std::isfinite(result));
  return result;
}

// Area density is projected parallel to direction onto surface oriented according to normal.
inline double ProjectArea(const Double3 &direction, const Double3 &normal)
{
  return std::abs(Dot(direction, normal));
}

}



namespace SampleNamespace
{


struct Pdf
{
  double value;
  operator double () const { return std::abs(value); }
  Pdf() : value{NaN} {}
  Pdf(double _value) : value{_value} 
  {
    assert(_value>=0);
  }
  Pdf(const Pdf &other) : value{other.value} {}
  Pdf& operator=(const Pdf &other) { value = other.value; return *this; }
  
  bool IsFromDelta() const { return std::signbit(value); }  
  
  Pdf& operator *= (double q) 
  {
    assert(q >= 0);
    value *= q;
    return *this;
  }
  
  // Not permitted. Use only for BSDF/Emitter samples which may come from delta distributions. Do not use for path densities.
  friend Pdf operator*(Pdf a, Pdf b);
  
  friend Pdf operator*(double q, Pdf pdf)
  {
    pdf *= q;
    return pdf;
  }
  
  friend Pdf MakeFromDelta(Pdf pdf) 
  {
    pdf.value = std::copysign(pdf.value, -1.);
    return pdf;
  }
};


  


template<class C, class U, class T>
struct Sample
{
  using CoordinateType = C;
  using ValueType = U;
  using Tag = T;
  
  CoordinateType coordinates;
  ValueType value; 
  Pdf pdf_or_pmf;
  
  template<class Other>
  auto as() const
  {
    return Sample<CoordinateType, ValueType, typename Other::Tag>{coordinates, value, pdf_or_pmf};
  }
  
  inline friend void SetPmfFlag(Sample &s) { s.pdf_or_pmf = MakeFromDelta(s.pdf_or_pmf); }
  inline friend bool IsFromPmf(const Sample &s) { return s.pdf_or_pmf.IsFromDelta(); }
  inline friend bool IsFromPdf(const Sample &s) { return !IsFromPmf(s); }
  inline friend double PdfValue(const Sample &s) { assert(!IsFromPmf(s)); return (double)s.pdf_or_pmf; }
  inline friend double PmfValue(const Sample &s) { assert(IsFromPmf(s)); return (double)s.pdf_or_pmf; }
  inline friend double PmfOrPdfValue(const Sample &s) { return std::abs(s.pdf_or_pmf); }
};

}

using SampleNamespace::Pdf;
using SampleNamespace::Sample;
