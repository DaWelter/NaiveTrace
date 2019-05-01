#ifndef PRIMITIVE_HXX
#define PRIMITIVE_HXX

#include "types.hxx"
#include "util.hxx"
#include "box.hxx"

using scene_index_t = std::int32_t;
static constexpr int INVALID_PRIM_INDEX {-1};

struct PrimRef // Reference to Primitive
{
  const Geometry *geom = { nullptr };
  int index = INVALID_PRIM_INDEX;
  
  operator bool() const
  {
    assert(geom == nullptr || index!=INVALID_PRIM_INDEX);
    return geom != nullptr;
  }
};


struct HitId : public PrimRef
{
  HitId() = default;
  HitId(const Geometry* _geom, int index, const Double3 &_barry) 
    : PrimRef{_geom, index}, barry{_barry}
  {}
  Double3 barry;
};


class Geometry
{
public:
  using index_t = scene_index_t;
  unsigned int identifier = { (unsigned int)-1 }; // Used by embree. It's the geometry identifier.
  enum Type {
    PRIMITIVES_TRIANGLES,
    PRIMITIVES_SPHERES
  } const type;
  ToyVector<MaterialIndex> material_indices; // Per primitive    
  
  Geometry(Type _t) : type{_t} {}
  virtual ~Geometry() = default;
  virtual HitId SampleUniformPosition(index_t index, Sampler &sampler) const = 0;
  virtual double Area(index_t index) const = 0;
  virtual index_t Size() const = 0;
  virtual void GetLocalGeometry(SurfaceInteraction &interaction) const = 0;
};


class Mesh : public Geometry
{
  public:
    // Float because Embree :-(
    // Also, see https://eigen.tuxfamily.org/dox/group__TopicStorageOrders.html
    // Memory address varies fastest w.r.t column index.
    using Vectors3d = Eigen::Matrix<float, Eigen::Dynamic, 3, Eigen::RowMajor>;
    using Vectors2d = Eigen::Matrix<float, Eigen::Dynamic, 2, Eigen::RowMajor>;
    // Embree requires unsigned int for triangle indices!
    using Indices3d = Eigen::Matrix<unsigned int, Eigen::Dynamic, 3, Eigen::RowMajor>;
    Vectors3d vertices;
    Indices3d vert_indices; // Vertex indices per triangle.
    Vectors3d normals; // Per vertex.
    Vectors2d uvs; // Per vertex.

    Mesh(index_t num_triangles, index_t num_vertices);
    
    void Append(const Mesh &other);
    void MakeFlatNormals();
    
    inline index_t NumVertices() const { return (index_t)vertices.rows(); }
    inline index_t NumTriangles() const { return (index_t)vert_indices.rows(); }
    
    HitId SampleUniformPosition(index_t index, Sampler &sampler) const override;
    double Area(index_t index) const override;
    index_t Size() const override { return NumTriangles(); }
    void GetLocalGeometry(SurfaceInteraction &interaction) const override;
};

void AppendSingleTriangle(Mesh &mesh,
  const Float3 &a, const Float3 &b, const Float3 &c, const Float3 &n);


class Spheres : public Geometry
{
  public:
    using Vector4f = Eigen::Matrix<float, 1, 4>;
    ToyVector<Vector4f> spheres; // position and radius;
    
    Spheres();
    void Append(const Float3 pos, const float radius, MaterialIndex material_index);
    void Append(const Spheres &other);
    inline index_t NumSpheres() const { return (index_t)spheres.size(); }
    inline std::pair<Float3, float> Get(index_t i) const;
    HitId SampleUniformPosition(index_t index, Sampler &sampler) const override;
    double Area(index_t index) const override;
    int Size() const override { return NumSpheres(); }
    void GetLocalGeometry(SurfaceInteraction &interaction) const override;
};

inline std::pair<Float3, float> Spheres::Get(int i) const 
{ 
  return std::pair<Float3, float>(spheres[i].head<3>(), spheres[i][3]);
}


void FillPosBoundsTriangle(SurfaceInteraction &interaction, const Float3 &p0, const Float3 &p1, const Float3 &p2);
void FillPosBoundsSphere(SurfaceInteraction &interaction);
// Return if there is something left of the segment, and the clipped near/far coordinate along the ray.
std::tuple<bool, double, double> ClipRayToSphereInterior(const Double3 &ray_org, const Double3 &ray_dir, double tnear, double tfar, const Double3 &sphere_p, double sphere_r);

#endif
