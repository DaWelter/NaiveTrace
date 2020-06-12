#pragma once

#include "very_strong_typedef.hxx"


struct tag_MaterialIndex {};
struct tag_ShaderIndex {};
struct tag_MediumIndex {};
using MaterialIndex = very_strong_typedef<short, tag_MaterialIndex>;
using ShaderIndex = very_strong_typedef<short, tag_ShaderIndex>;
using MediumIndex = very_strong_typedef<short, tag_MaterialIndex>;
inline static constexpr size_t MAX_NUM_MATERIALS = std::numeric_limits<short>::max();

class Shader;
class Scene;
class Shader;
namespace materials {
    class Medium;
}
using materials::Medium;
class Camera;
class Sampler;
class Geometry;
class Texture;

namespace RadianceOrImportance {
class AreaEmitter;
class PointEmitter;
class EnvironmentalRadianceField;
}

struct SurfaceInteraction;