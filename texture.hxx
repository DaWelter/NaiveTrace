#ifndef TEXTURE_HXX
#define TEXTURE_HXX

#include"image.hxx"
#include"vec3f.hxx"
#include"spectral.hxx"

class Texture
{
	Image bm;
public:
  Texture(const std::string &filename)
  {
    bm.Read(filename);
    /*
        ubyte r,g,b;
        for(int x=0; x<bm.width(); x++)
        for(int y=0; y<bm.height(); y++)
        {
          bm.getpixel(x,y,r,g,b);
          std::cout<<"bm["<<x<<","<<y<<"] = "<<r<<" "<<g<<" "<<b<<std::endl;
        }
    */
  }

  RGB GetTexel(double u, double v) const
  {
    double dummy;
    u = modf(u, &dummy);
    v = modf(v, &dummy);
    if (u < 0) u = 1. + u;
    if (v < 0) v = 1. + v;
    if (u > 1. - Epsilon) u -= Epsilon;
    if (v > 1. - Epsilon) v -= Epsilon;
    if (bm.empty()) return RGB::Constant(1._rgb);
    int x = u * bm.width();
    int y = v * bm.height();
    auto rgb = bm.get_pixel_uc3(x, y);
    RGB c;
    c[0] = Color::SRGBToLinear(Color::RGBScalar(std::get<0>(rgb) / 255.0));
    c[1] = Color::SRGBToLinear(Color::RGBScalar(std::get<1>(rgb) / 255.0));
    c[2] = Color::SRGBToLinear(Color::RGBScalar(std::get<2>(rgb) / 255.0));
    return c;
  }
};

#endif
