#include "scene.hxx"
#include "perspectivecamera.hxx"
#include "sphere.hxx"
#include "infiniteplane.hxx"
#include "triangle.hxx"
#include "shader.hxx"
#include "util.hxx"
#include "atmosphere.hxx"

#define LINESIZE 1000

#include <libgen.h> // unix specific. Need for dirname.
#include <vector>
#include <cstdio>
#include <unordered_map>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

template<class Thing>
class CurrentThing
{
  Thing* currentThing;
  std::unordered_map<std::string, Thing*> things;
  std::string name;
public:
  CurrentThing(const std::string &_name, const std::string &default_name, Thing* default_thing) :
    name(_name), currentThing(default_thing)
  {
    things[default_name] = default_thing;
  }
  
  int size() const
  {
    return things.size();
  }
  
  void activate(const std::string &name)
  {
    auto it = things.find(name);
    if (it != things.end())
    {
      currentThing = it->second;
    }
    else
    {
      char buffer[1024];
      std::snprintf(buffer, 1024, "Error: %s %s not defined. Define it in the NFF file prior to referencing it.", this->name.c_str(), name.c_str());
      throw std::runtime_error(buffer);
    }
  }
  
  void set_and_activate(const char* name, Thing* thing)
  {
    currentThing = thing;
    things[name] = thing;
  }
  
  Thing* operator()() const
  {
    return currentThing;
  }
};


class NFFParser
{
  Scene* scene;
  CurrentThing<Shader> shaders;
  CurrentThing<Medium> mediums;
  Eigen::Transform<double,3,Eigen::Affine> currentTransform;
  std::string dirname;
  RenderingParameters *render_params;
  friend class AssimpReader;
public:
  NFFParser(Scene* _scene, RenderingParameters *_render_params) :
    scene(_scene),
    shaders("Shader", "invisible", new InvisibleShader()),
    mediums("Medium", "default", new VacuumMedium()),
    render_params(_render_params)
  {
    shaders.set_and_activate("default", new DiffuseShader(Double3(0.8, 0.8, 0.8)));
    currentTransform = decltype(currentTransform)::Identity();
  }
  void Parse(const char *fileName);
private:
  void AssignCurrentMaterialParams(Primitive &primitive);
  Double3 ApplyTransform(const Double3 &p) const;
  Double3 ApplyTransformNormal(const Double3 &v) const;
  void ParseMesh(const char *filename);
  std::string GenerateFilePath(const std::string &filename) const;
};


Double3 NFFParser::ApplyTransform(const Double3 &p) const
{
  return currentTransform * p;
}


Double3 NFFParser::ApplyTransformNormal(const Double3 &v) const
{
  return currentTransform.linear() * v;
}


void NFFParser::AssignCurrentMaterialParams(Primitive& primitive)
{
  primitive.shader = shaders();
  primitive.medium = mediums();
  assert(primitive.shader && primitive.medium);
}


std::string NFFParser::GenerateFilePath(const std::string &filename) const
{
  // Assuming we get a relative path as input.
  std::string fullpath = dirname + (dirname.empty() ? "" : "/") + filename;
  return fullpath;
}


void NFFParser::Parse(const char* fileName)
{
  char line[LINESIZE+1];
  char token[LINESIZE+1];
  char *str;
  int i;

  {
    auto *dirnamec_storage = strdup(fileName); // modifies its argument.
    auto *dirnamec = ::dirname(dirnamec_storage);
    this->dirname.assign(dirnamec);
    free(dirnamec_storage);
  }
  
  FILE *file = fopen(fileName,"r");
  if (!file) 
  {
    char buffer[1024];
    std::snprintf(buffer, 1024, "could not open input file %s", fileName);
    throw std::runtime_error(buffer);
  }
  
  int line_no = 0;
  while (!feof(file)) 
  {
    str = std::fgets(line,LINESIZE,file);
    if (str == nullptr)
      continue;
    //std::cout << (++line_no) << ": " << str;
    
    if (str[0] == '#') // '#' : comment
      continue;
    
    int numtokens = sscanf(line,"%s",token);
    if (numtokens <= 0) // empty line, except for whitespaces
      continue; 
    
    if (!strcmp(token,"begin_hierarchy")) {
      line[strlen(line)-1] = 0; // remove trailing eol indicator '\n'
      //Parse(file, fileName);
      continue;
    }

    
    if (!strcmp(token,"end_hierarchy")) {
      //return;
      continue;
    }
    
    if (!strcmp(token, "transform"))
    {
      Double3 t, r;
      int n = std::sscanf(str,"transform %lg %lg %lg %lg %lg %lg\n",&t[0], &t[1], &t[2], &r[0], &r[1], &r[2]);
      if (n == 6)
      {
        // The heading, pitch, bank convention assuming Y is up and Z is forward!
        Eigen::Matrix3d m{
          Eigen::AngleAxisd(r[0], Eigen::Vector3d::UnitY()) *
          Eigen::AngleAxisd(r[1], Eigen::Vector3d::UnitX()) * 
          Eigen::AngleAxisd(r[2], Eigen::Vector3d::UnitZ()) };
        currentTransform = Eigen::Translation3d(t) * m;
        std::cout << "Transform: t=\n" << currentTransform.translation() << "\nr=\n" << currentTransform.linear() << std::endl;
        continue;
      }
      else
      {
        throw std::runtime_error(strconcat("error in ", fileName, " : ", line));
      }
    }
    /* camera */

    if (!strcmp(token,"v")) {
      // FORMAT:
      //     v
      //     from %lg %lg %lg
      //     at %lg %lg %lg
      //     up %lg %lg %lg
      //     angle %lg
      //     hither %lg
      //     resolution %d %d
      Double3 pos, at, up;
      double angle, hither;
      int resX, resY;
      fscanf(file,"from %lg %lg %lg\n",&pos[0],&pos[1],&pos[2]);
      fscanf(file,"at %lg %lg %lg\n",&at[0],&at[1],&at[2]);
      fscanf(file,"up %lg %lg %lg\n",&up[0],&up[1],&up[2]);
      fscanf(file,"angle %lg\n",&angle);
      fscanf(file,"hither %lg\n",&hither);
      fscanf(file,"resolution %d %d\n",&resX,&resY);
      if (render_params)
      {
        if (render_params->height > 0)
          resY = render_params->height;
        else
          render_params->height = resY;
        if (render_params->width > 0)
          resX = render_params->width;
        else
          render_params->width = resX;
      }
      scene->SetCamera<PerspectiveCamera>(pos,at-pos,up,angle,resX,resY);
      continue;
    }
    
    /* sphere */

    if (!strcmp(token,"s")) {
      Double3 pos;
      double rad;
      sscanf(str,"s %lg %lg %lg %lg",&pos[0],&pos[1],&pos[2],&rad);
      AssignCurrentMaterialParams(
        scene->AddPrimitive<Sphere>(pos,rad));
      continue;
    }

    /* polygon (with normals) */
    
    if (!strcmp(token,"pp")) {
      int vertices;
      sscanf(str,"pp %d",&vertices);
      Double3 *vertex = new Double3[vertices];
      Double3 *normal = new Double3[vertices];

      for (i=0;i<vertices;i++) 
      {
        fscanf(file,"%lg %lg %lg %lg %lg %lg\n",
          &vertex[i][0],&vertex[i][1],&vertex[i][2],
          &normal[i][0],&normal[i][1],&normal[i][2]);
        vertex[i] = ApplyTransform(vertex[i]);
        normal[i] = ApplyTransformNormal(normal[i]);
      }

      for (i=2;i<vertices;i++) 
      {
        AssignCurrentMaterialParams(
          scene->AddPrimitive<SmoothTriangle>(
          vertex[0],
          vertex[i-1],
          vertex[i],
          normal[i-1],
          normal[i],
          normal[0]));
      }
      delete[] vertex;
      delete[] normal;
      continue;
    }

/* polygon (with normals and uv) */
    if (!strcmp(token,"tpp")) 
	{
		int vertices;
		sscanf(str,"tpp %d",&vertices);
		Double3 *vertex = new Double3[vertices];
		Double3 *normal = new Double3[vertices];
		Double3 *uv     = new Double3[vertices];

		for (i=0;i<vertices;i++) 
    {
			fscanf(file,"%lg %lg %lg %lg %lg %lg %lg %lg %lg\n",
        &vertex[i][0],&vertex[i][1],&vertex[i][2],
        &normal[i][0],&normal[i][1],&normal[i][2],
        &uv[i][0],&uv[i][1],&uv[i][2]);
      vertex[i] = ApplyTransform(vertex[i]);
      normal[i] = ApplyTransformNormal(normal[i]);
		}

		for (i=2;i<vertices;i++) {
      AssignCurrentMaterialParams(
        scene->AddPrimitive<TexturedSmoothTriangle>(
            vertex[0],
            vertex[i-1],
            vertex[i],
            normal[0],
            normal[i-1],
            normal[i],
            uv[0],
            uv[i-1],
            uv[i]));
		}
		delete[] vertex;
		delete[] normal;
		continue;
    }

   
    /* polygon */

    if (!strcmp(token,"p")) {
		int vertices;
		sscanf(str,"p %d",&vertices);
		Double3 *vertex = new Double3[vertices];
		for (i=0;i<vertices;i++) 
    {
			fscanf(file,"%lg %lg %lg\n",&vertex[i][0],&vertex[i][1],&vertex[i][2]);
      vertex[i] = ApplyTransform(vertex[i]);
		}

		for (i=2;i<vertices;i++) {
			AssignCurrentMaterialParams(
        scene->AddPrimitive<Triangle>(
          vertex[0],
					vertex[i-1],
					vertex[i]));
			}
		delete[] vertex;
		continue;
    }
    
    /* shader parameters */
    
    if (!strcmp(token,"shader"))
    {
      double r,g,b,kd,ks,shine,t,ior;
      char texture_filename[LINESIZE];
      char name[LINESIZE];
      
      int num = std::sscanf(line,"shader %s %lg %lg %lg %lg %lg %lg %lg %lg %s\n",name, &r,&g,&b,&kd,&ks,&shine,&t,&ior,texture_filename);
      Double3 color(r,g,b);
      if(num==1)
      {
        shaders.activate(name);
      }
      else if(num < 10)
      {
        shaders.set_and_activate(name, 
          new DiffuseShader(kd * color));
      }
      else if(num >= 10)
      {
        auto fullpath = GenerateFilePath(texture_filename);
        auto texture = std::make_unique<Texture>(fullpath);
        shaders.set_and_activate(name,
          new TexturedDiffuseShader(kd * color, std::move(texture)));
      }
      else {
        std::cout << "error in " << fileName << " : " << line << std::endl;
      }
      continue;
    }
    
    if (!strcmp(token, "medium"))
    {
      Spectral sigma_s{0.}, sigma_a{1.};
      char name[LINESIZE];
      int num = std::sscanf(line, "medium %s %lg %lg %lg %lg %lg %lg\n", name, &sigma_s[0], &sigma_s[1], &sigma_s[2], &sigma_a[0], &sigma_a[1], &sigma_a[2]);
      if (num==1)
      {
        mediums.activate(name);
      }
      else if(num == 7)
      {
        auto *medium = new HomogeneousMedium(sigma_s, sigma_a, mediums.size());
        mediums.set_and_activate(
          name, medium);
      }
      else
      {
        std::cout << "error in " << fileName << " : " << line << std::endl;
      }
      continue;
    }
  
    if (!strcmp(token, "pf"))
    {
      if (auto* medium = dynamic_cast<HomogeneousMedium*>(mediums()))
      {
        double g;
        char name[LINESIZE];
        int num = std::sscanf(line,"pf %s %lg\n",name, &g);
        if (num > 0)
        {
          if (!strcmp(name, "rayleigh"))
          {
            medium->phasefunction.reset(new PhaseFunctions::Rayleigh());
          }
          else if (!strcmp(name, "henleygreenstein") && num>1)
          {
            medium->phasefunction.reset(new PhaseFunctions::HenleyGreenstein(g));
          }
          else
          {
            std::cerr << "Error parsing phasefunction " << name << std::endl;
            exit(-1);
          }
        }
        else
        {
          medium->phasefunction.reset(new PhaseFunctions::Uniform());
        }
      }
      else
      {
        std::cout << "Warning: Phasefunction definition only admissible following a HomogeneousMedium definition." << std::endl;
      }
      continue;
    }


    if (!strcmp(token, "simpleatmosphere"))
    {
      Double3 planet_center;
      double radius;
      char name[LINESIZE];
      int num = std::sscanf(line, "simpleatmosphere %s %lg %lg %lg %lg\n", name, &planet_center[0], &planet_center[1], &planet_center[2], &radius);
      if (num==1)
      {
        mediums.activate(name);
      }
      else if(num == 5)
      {
        auto *medium = new Atmosphere::Simple(planet_center, radius, mediums.size());
        mediums.set_and_activate(
          name, medium);
      }
      else
      {
        std::cout << "error in " << fileName << " : " << line << std::endl;
      }
      continue;
    }

  
    if (!strcmp(token, "lddirection"))
    {
      Double3 dir_out;
      Spectral col;
      int num = sscanf(line,"lddirection %lg %lg %lg %lg %lg %lg",
          &dir_out[0],&dir_out[1],&dir_out[2],
          &col[0],&col[1],&col[2]);
      Normalize(dir_out);
      if (num == 6)
      {
        scene->AddLight(std::make_unique<DistantDirectionalLight>(col, dir_out));
      }
      else
      {
        std::cout << "error in " << fileName << " : " << line << std::endl;
      }
      continue;
    }
  
  
    if (!strcmp(token, "lddome"))
    {
      Double3 dir_up;
      Spectral col;
      int num = sscanf(line,"lddome %lg %lg %lg %lg %lg %lg",
          &dir_up[0],&dir_up[1],&dir_up[2],
          &col[0],&col[1],&col[2]);
      Normalize(dir_up);
      if (num == 6)
      {
        scene->AddLight(std::make_unique<DistantDomeLight>(col, dir_up));
      }
      else
      {
        std::cout << "error in " << fileName << " : " << line << std::endl;
      }
      continue;
    }
  
  
    /* lightsource */
  if (!strcmp(token,"l")) 
	{
		Double3 pos;
    Spectral col;
		int num = sscanf(line,"l %lg %lg %lg %lg %lg %lg",
			   &pos[0],&pos[1],&pos[2],
			   &col[0],&col[1],&col[2]);
    col *= 1.0/255.9999;
		if (num == 3) {
			// light source with position only
			col = Spectral(1,1,1);
			scene->AddLight(std::make_unique<PointLight>(col,pos));	
		} else if (num == 6) {
			// light source with position and color
			scene->AddLight(std::make_unique<PointLight>(col,pos));	
		} else {
			std::cout << "error in " << fileName << " : " << line << std::endl;
		}
		continue;
    }

	
// 	if(!strcmp(token,"sl"))
// 	{
// 		Double3 pos,dir,col;
// 		double min=0,max=10;
// 		int num = sscanf(line,"sl %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg",
// 				&pos[0],&pos[1],&pos[2],&dir[0],&dir[1],&dir[2],&col[0],&col[1],&col[2],&min,&max); 
// 		if(num == 11) {
// 			scene->AddLight(std::make_unique<SpotLight>(col,pos,dir,min,max));
// 		}
// 		else {
// 			std::cout << "error in "<<fileName<<" : " << line << std::endl;
// 		}
// 		continue;
// 	}
	

  /* include new NFF file */
  if (!strcmp(token,"include"))
  {
    char name[LINESIZE];
    int num = std::sscanf(line,"include %s\n", name);
    if (num != 1)
    {
      throw std::runtime_error("Unable to parse include line");
    }
    else
    {
      auto fullpath = GenerateFilePath(name);
      std::cout << "including file " << fullpath << std::endl;
      Parse(fullpath.c_str());
    }
    continue;
  }

	
	if (!strcmp(token, "m"))
  {
    char meshfile[LINESIZE];
    int num = sscanf(line, "m %s", meshfile);
    if (num == 1)
    {
      auto fullpath = GenerateFilePath(meshfile);
      ParseMesh(fullpath.c_str());
    }
    else
    {
      std::cout << "error in " << fileName << " : " << line << std::endl;
    }
    continue;
  }
  
    /* background color */

    if (!strcmp(token,"b")) {
      sscanf(line,"b %lg %lg %lg",&scene->bgColor[0],&scene->bgColor[1],&scene->bgColor[2]);
      scene->bgColor /= 255;
      continue;
    }
  
    char buffer[1024];
    std::snprintf(buffer, 1024, "Unkown directive in %s : %s", fileName, line);
    throw std::runtime_error(buffer);
  }
  
  fclose(file);
};


// Example see: https://github.com/assimp/assimp/blob/master/samples/SimpleOpenGL/Sample_SimpleOpenGL.c
class AssimpReader
{
  struct NodeRef
  {
    NodeRef(aiNode* _node, const aiMatrix4x4 &_mTrafoParent)
      : node(_node), local_to_world(_mTrafoParent * _node->mTransformation)
    {}
    aiMatrix4x4 local_to_world;
    aiNode* node;
  };
public:
  void Read(const char *filename, NFFParser* parser, Scene *scene)
  {
    std::printf("Reading Mesh: %s\n", filename);
    this->aiscene = aiImportFile(filename, 0);
    this->scene = scene;
    this->parser = parser;
    
    if (!aiscene)
    {
      char buffer[1024];
      std::snprintf(buffer, 1024, "Error: could not load file %s. because: %s", filename, aiGetErrorString());
      throw std::runtime_error(buffer);
    }
    
    std::vector<NodeRef> nodestack{ {aiscene->mRootNode, aiMatrix4x4{}} };
    while (!nodestack.empty())
    {
      auto ndref = nodestack.back();
      nodestack.pop_back();
      for (unsigned int i = 0; i< ndref.node->mNumChildren; ++i)
      {
        nodestack.push_back({ndref.node->mChildren[i], ndref.local_to_world});
      }
      
      ReadNode(ndref);
    }
    
    aiReleaseImport(this->aiscene);
  }
  
private:
  void ReadNode(const NodeRef &ndref)
  {
    const auto *nd = ndref.node;
    for (unsigned int mesh_idx = 0; mesh_idx < nd->mNumMeshes; ++mesh_idx)
    {
      const aiMesh* mesh = aiscene->mMeshes[nd->mMeshes[mesh_idx]];
      std::printf("Mesh %i (%s), mat_idx=%i\n", mesh_idx, mesh->mName.C_Str(), mesh->mMaterialIndex);
      DealWithTheMaterialOf(mesh);
      ReadMesh(mesh, ndref);
    }
  }
  
  void DealWithTheMaterialOf(const aiMesh* mesh)
  {
    if (mesh->mMaterialIndex < aiscene->mNumMaterials)
    {
      SetCurrentShader(aiscene->mMaterials[mesh->mMaterialIndex]);
    }
  }
  
  void ReadMesh(const aiMesh* mesh, const NodeRef &ndref)
  {
    auto m = ndref.local_to_world;
    bool generateTexturedTriangles =
        mesh->GetNumUVChannels()>0 &&
        mesh->HasNormals();
    for (unsigned int face_idx = 0; face_idx < mesh->mNumFaces; ++face_idx)
    {
      const aiFace* face = &mesh->mFaces[face_idx];
      auto fetchVertex = [this,mesh,face,&m](int corner) -> Double3 {
        return parser->ApplyTransform(
                aiVector3_to_myvector(m * mesh->mVertices[face->mIndices[corner]]));
      };
      auto fetchNormal = [this,mesh,face,&m](int corner) -> Double3 {
        return parser->ApplyTransformNormal(
          aiVector3_to_myvector(m * mesh->mNormals[face->mIndices[corner]]));
      };
      auto fetchUV = [this,mesh,face,&m](int corner) -> Double3 {
        return aiVector3_to_myvector(mesh->mTextureCoords[0][face->mIndices[corner]]);
      };
      Double3 vertex0 = fetchVertex(0);
      Double3 normal0, uv0;
      if (generateTexturedTriangles)
      {
        normal0 = fetchNormal(0);
        uv0 = fetchUV(0);
      }
      for (int i=2; i<face->mNumIndices; i++)
      {
        auto vertex1 = fetchVertex(i-1);
        auto vertex2 = fetchVertex(i);
        if (generateTexturedTriangles)
        {
          auto normal1 = fetchNormal(i-1);
          auto normal2 = fetchNormal(i);
          auto uv1 = fetchUV(i-1);
          auto uv2 = fetchUV(i);
          parser->AssignCurrentMaterialParams(
            scene->AddPrimitive<TexturedSmoothTriangle>(
              vertex0, vertex1, vertex2,
              normal0, normal1, normal2,
              uv0, uv1, uv2
           ));
        }
        else
        {
          parser->AssignCurrentMaterialParams(
            scene->AddPrimitive<Triangle>(
                vertex0,
                vertex1,
                vertex2));
        }
      }
    }
  }
  
  void SetCurrentShader(const aiMaterial *mat)
  {
//     for (int prop_idx = 0; prop_idx < mat->mNumProperties; ++prop_idx)
//     {
//       const auto *prop = mat->mProperties[prop_idx];
//       aiString name;
//       
//       std::printf("Mat %p key[%i/%s]\n", (void*)mat, prop_idx, prop->mKey.C_Str());
//     }
    aiString ainame;
    mat->Get(AI_MATKEY_NAME,ainame);
    auto name = std::string(ainame.C_Str());
    if (name != "DefaultMaterial")
      parser->shaders.activate(name);
  }

private:
  const aiScene *aiscene = { nullptr };
  Scene* scene = { nullptr };
  NFFParser *parser = {nullptr};

  inline Double3 aiVector3_to_myvector(const aiVector3D &v)
  {
    return Double3{v[0], v[1], v[2]};
  }
};


void NFFParser::ParseMesh(const char* filename)
{
  AssimpReader().Read(filename, this, scene);
}


void Scene::ParseNFF(const char* fileName, RenderingParameters *render_params)
{
  // parse file, add all items to 'primitives'
  NFFParser(this, render_params).Parse(fileName);
}



