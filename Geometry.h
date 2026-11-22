#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace geo 
{
    constexpr double Pi = 3.14159265358979323846;
    constexpr double RadiusKm = 6371.0;
    struct Vec3 
    {
        double x=0, y=0, z=0;
        Vec3 operator+(Vec3 b) const 
        {
            return {x+b.x,y+b.y,z+b.z}; 
        }
        Vec3 operator-(Vec3 b) const 
        {
            return {x-b.x,y-b.y,z-b.z}; 
        }
        Vec3 operator*(double s) const 
        {
            return {x*s,y*s,z*s}; 
        }
    };

    inline double dot(Vec3 a, Vec3 b) 
    { 
        return a.x*b.x+a.y*b.y+a.z*b.z; 
    }

    inline Vec3 cross(Vec3 a, Vec3 b) 
    { 
        return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; 
    }

    inline double length(Vec3 v) 
    { 
        return std::sqrt(dot(v,v)); 
    }

    inline Vec3 unit(Vec3 v) 
    {
        double n=length(v); 
        return n>1e-12 ? v*(1/n) : Vec3{0,0,1}; 
    }

    // Spherical Earth, NOT WGS84 ellipsoidal ECEF. Input degrees / meters; output km.
    inline Vec3 position(double lon, double lat, double heightM=0) 
    {
        double l = lon * Pi / 180;
        double p = lat * Pi / 180;
        double r = RadiusKm + heightM / 1000;
        return {r*std::cos(p)*std::cos(l), r*std::cos(p)*std::sin(l), r*std::sin(p)};
    }

    struct Vertex 
    { 
        Vec3 position, normal; 
        float r,g,b; 
        double u=0,v=0; 
    };

    struct Mesh 
    { 
        std::vector<Vertex> vertices; 
        std::vector<uint32_t> indices;
    };

    inline void triangle(Mesh& m,uint32_t a,uint32_t b,uint32_t c)
    {
        m.indices.insert(m.indices.end(),{a,b,c});
    }

    inline Mesh globe() 
    {
        Mesh m;
        const int nx = 360;
        const int ny = 180;
        for (int j = 0; j <= ny; ++j)
        {
            for (int i = 0; i <= nx; ++i)
            {
                auto p = position(-180 + i, -90 + j);
                m.vertices.push_back({ p, unit(p), 0.075f, 0.25f, 0.47f, double(i) / nx,double(j) / ny });
            }
        }
        for(int j = 0; j < ny; ++j) 
        {
            for (int i = 0; i < nx; ++i)
            {
                uint32_t a = j * (nx + 1) + i, b = a + 1, c = a + nx + 1, d = c + 1;
                if (j > 0) 
                    triangle(m, a, b, c);

                if (j < ny - 1) 
                    triangle(m, b, d, c);
            }
        }
        return m;
    }

    // Row 0 = SOUTH; column 0 = WEST. Values are meters above the sample sphere.
    struct HeightGrid 
    {
        int cols = 0;
        int rows = 0;
        double west = 124;
        double east = 132;
        double south = 33;
        double north = 39;
        std::vector<double> meters;
    };

    inline HeightGrid syntheticGrid() 
    {
        HeightGrid g; 
        g.cols=161; 
        g.rows=121;
        g.meters.resize(g.cols*g. rows);
        for (int j = 0; j < g.rows; ++j)
        {
            for (int i = 0; i < g.cols; ++i)
            {
                double u = double(i) / (g.cols - 1);
                double v = double(j) / (g.rows - 1);
                double x = (u - 0.5) * 2, y = (v - 0.5) * 2;
                double envelope = std::pow(std::sin(Pi * u) * std::sin(Pi * v), 2);
                double ridge = 0.5 + 0.5 * std::sin(12 * x + 4 * std::sin(5 * y));
                g.meters[j * g.cols + i] = envelope * (300 + 1700 * ridge + 900 * std::exp(-8 * (x * x + y * y)));
            }
        }
        return g;
    }

    inline Mesh terrain(const HeightGrid& g, double exaggeration) 
    {
        if(g.cols<2 || g.rows<2 || g.meters.size()!=size_t(g.cols)*g.rows ||
           !(g.east>g.west) || !(g.north>g.south) || g.south < -90||g.north>90 ||
           !std::isfinite(exaggeration) || exaggeration<=0)
           throw std::invalid_argument("Invalid height grid");
        Mesh m;
        for(int j=0; j<g.rows; ++j) 
            for(int i=0; i<g.cols; ++i) 
        {
            double h = g.meters[j*g.cols+i];
            if(!std::isfinite(h)||h<0) 
                throw std::invalid_argument("Resolve NoData/negative heights before rendering this overlay");
            double lon=g.west+(g.east-g.west)*i/(g.cols-1);
            double lat=g.south+(g.north-g.south)*j/(g.rows-1);
            double t=std::clamp(h/2800.0, 0.0, 1.0);
            // 50m display bias avoids coplanar overlap with the sea sphere.
            auto p=position(lon, lat, h*exaggeration+50);
            m.vertices.push_back({p,{},float(0.15+0.75*t),float(0.52+0.35*t),float(0.13+0.72*t),(lon+180)/360,(lat+90)/180});
        }
        for(int j=0; j<g.rows-1; ++j) 
        {
            for (int i = 0; i < g.cols - 1; ++i)
            {
                uint32_t a = j * g.cols + i, b = a + 1, c = a + g.cols, d = c + 1;
                triangle(m, a, b, c); triangle(m, b, d, c);
            }
        }
        // Area-weighted normals from the exaggerated geometry.
        for(size_t k=0; k<m.indices.size(); k+=3) 
        {
            auto a=m.indices[k],b=m.indices[k+1],c=m.indices[k+2];
            auto n=cross(m.vertices[b].position-m.vertices[a].position,
                         m.vertices[c].position-m.vertices[a].position);
            for(auto idx:{a,b,c}) 
                m.vertices[idx].normal = m.vertices[idx].normal+n;
        }
        for(auto& v:m.vertices) 
            v.normal=unit(v.normal);

        return m;
    }
}

namespace geo
{
    struct Geographic 
    { 
        double lon;
        double lat;
        double heightM;
    };

    inline Geographic geographic(Vec3 p)
    {
        double r=length(p);
        return {std::atan2(p.y,p.x)*180/Pi,
            std::asin(std::clamp(p.z/r,-1.0,1.0))*180/Pi,(r-RadiusKm)*1000};
    }
    // Ray direction must be normalized. Returns the nearest positive distance in km.
    inline bool raySphere(Vec3 origin,Vec3 direction,double& t) 
    {
        double b = dot(origin, direction);
        double c = dot(origin, origin) - RadiusKm * RadiusKm;
        double discriminant=b*b-c;
        if(discriminant<0)
            return false;

        double root=std::sqrt(discriminant);
        t=-b-root;
        if(t<0) 
            t=-b+root;

        return t>=0;
    }
    inline bool rayTriangle(Vec3 o,Vec3 d,Vec3 a,Vec3 b,Vec3 c,double& t) 
    {
        Vec3 e1=b-a,e2=c-a,p=cross(d,e2);
        double determinant=dot(e1,p);
        if(std::abs(determinant)<1e-12) 
            return false;

        double inv=1/determinant;
        Vec3 s=o-a;
        double u=dot(s,p)*inv;
        if(u<0||u>1) 
            return false;

        Vec3 q=cross(s,e1);
        double v=dot(d,q)*inv;
        if(v<0||u+v>1)
            return false;

        t=dot(e2,q)*inv;

        return t>=0;
    }

    struct Pick
    {
        bool hit = false;
        bool terrain = false; 
        Vec3 position;
    };

    inline Pick pickSurface(Vec3 o,Vec3 d,const Mesh& terrainMesh)
    {
        Pick result;
        double closest = 1e100;
        double t = 0;
        if(raySphere(o,d,t)) 
        { 
            closest=t; 
            result.hit=true; 
        }
        for(size_t i=0;i<terrainMesh.indices.size();i+=3) 
        {
            if(rayTriangle(o,d,terrainMesh.vertices[terrainMesh.indices[i]].position,
                terrainMesh.vertices[terrainMesh.indices[i+1]].position,
                terrainMesh.vertices[terrainMesh.indices[i+2]].position,t) && t<closest) 
            {
                closest=t; 
                result.hit=true; 
                result.terrain=true;
            }
        }
        if(result.hit) 
            result.position=o+d*closest;

        return result;
    }
}
