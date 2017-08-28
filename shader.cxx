#include "shader.hxx"

#include "ray.hxx"
#include "scene.hxx"


DiffuseShader::DiffuseShader(const Double3 &_albedo)
  : albedo(_albedo)
{
}


Double3 DiffuseShader::EvaluateBRDF(const SurfaceHit& inbound_surface_hit, const Double3& out_direction) const
{
  return albedo;
}


double DiffuseShader::EvaluatePDF(const SurfaceHit& inbound_surface_hit, const Double3& out_direction) const
{
  constexpr double hemisphere_surface_area = 2.*Pi;
  return 1./hemisphere_surface_area;
}


DirectionSample DiffuseShader::SampleBRDF(const SurfaceHit& inbound_surface_hit) const
{
  DirectionSample s;
  //TODO: Implement
  //Double3 v = sampler.UniformHemisphere();
  return s;
}


/*
Double3 EyeLightShader::Shade(Ray &ray,Scene *scene) 
{
	Double3 normal = ray.hit->GetNormal(ray);
	double dot = -Dot(normal,ray.dir);
	//Assert(dot>0.);
	dot=fabs(dot);
	return col*(dot);
}


Double3 TexturedEyeLightShader::Shade(Ray &ray,Scene *scene) 
{
	Double3 col,texcol;
	col = EyeLightShader::Shade(ray,scene);

	Double3 uv = ray.hit->GetUV(ray);
	texcol = tex.GetTexel(uv[0],uv[1]);
	
	return Product(texcol,col);
}


Double3 ReflectiveEyeLightShader::Shade(Ray &ray,Scene *scene)
{
	Double3 normal = ray.hit->GetNormal(ray);
	double dot = -Dot(normal,ray.dir);
	
	Double3 result = fabs(dot)*col;

	if(ray.level>=MAX_RAY_DEPTH) return result;

	Ray reflRay;
	reflRay.level = ray.level+1;
	reflRay.org = ray.org+ray.t*ray.dir;
	reflRay.t = 1000.;
	reflRay.dir = ray.dir+2.*dot*normal;

	Double3 reflcol = scene->RayTrace(reflRay);

	result = result + reflcol*reflectivity;
	return result;
}



Double3 PhongShader::Shade(Ray &ray,Scene *scene)
{
	Double3 normal = ray.hit->GetNormal(ray);
	Double3 refldir = ray.dir-2.*Dot(normal,ray.dir)*normal;
	Double3 pos = ray.org + ray.t * ray.dir;
	Double3 cold(0),cols(0);
	Double3 res(0);

	Double3 La(1,1,1); // ambient radiation 
	res += ka*Product(La,ca);

	for(int i=0; i<scene->lights.size(); i++)
	{
		Double3 intensity;
		Ray lightray;
		lightray.org = pos;
		if(!scene->lights[i]->Illuminate(lightray,intensity)) continue;
		if(scene->Occluded(lightray)) continue;

		double ddot = Dot(lightray.dir,normal);
		if(ddot>0)
			cold += intensity*Dot(lightray.dir,normal);

		double sdot = Dot(lightray.dir,refldir);
		if(sdot>0) 
			cols += intensity*pow(sdot,ke);
	}
	res += kd*Product(cold,cd) + ks*Product(cols,cs);

	if(ray.level>=MAX_RAY_DEPTH || kr<Epsilon) return res;
	Ray reflRay;
	reflRay.level = ray.level+1;
	reflRay.org = pos;
	reflRay.t = 1000.;
	reflRay.dir = refldir;

	Double3 colr = scene->RayTrace(reflRay);

	res += colr*kr;
	return res;
}*/