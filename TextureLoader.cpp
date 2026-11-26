#include <wincodec.h>
#include <wrl/client.h>
#include "TextureLoader.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <cstring>

using Microsoft::WRL::ComPtr;
namespace 
{
    void Check(HRESULT hr) 
    { 
        if(FAILED(hr)) 
            throw std::runtime_error("WIC image decode failed"); 
    }

    std::vector<uint8_t> ReadTile(IWICImagingFactory* factory,const std::filesystem::path& path)
    {
        ComPtr<IWICBitmapDecoder> decoder;
        Check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
        ComPtr<IWICBitmapFrameDecode> frame;
        Check(decoder->GetFrame(0, frame.GetAddressOf()));

        UINT w = 0;
        UINT h = 0;
        Check(frame->GetSize(&w, &h));
        if(w!=256 || h!=256) 
            throw std::runtime_error("Expected 256x256 NaturalEarthII JPEG tiles");

        ComPtr<IWICFormatConverter> converted;
        Check(factory->CreateFormatConverter(converted.GetAddressOf()));
        Check(converted->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        std::vector<uint8_t> rgba(256*256*4);
        Check(converted->CopyPixels(nullptr, 256*4, static_cast<UINT>(rgba.size()), rgba.data()));

        return rgba;
    }
}

/*
EarthImage LoadNaturalEarthII(const std::filesystem::path& root,int maxTextureSize, int level) 
{
    // Dedicated to Cesium's global-geodetic, JPG, levels 0..2 tile layout.
    // This is not a general TMS / XYZ / WebMercator importer.
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf())));
    for(int level=2;level>=0;--level) 
    {
        int nx = 2 << level;
        int ny = 1 << level;
        if(nx*256>maxTextureSize || ny*256>maxTextureSize)
            continue;

        bool complete=true;
        for(int x=0; x<nx; ++x) 
        {
            for (int y = 0; y < ny; ++y)
            {
                auto tile = root / std::to_wstring(level) / std::to_wstring(x) / (std::to_wstring(y) + L".jpg");

                if (!std::filesystem::is_regular_file(tile))
                    complete = false;
            }
        }

        if(!complete) 
            continue;
        
        EarthImage image;
        image.width=nx*256; 
        image.height=ny*256; 
        image.level=level;
        image.rgba.resize(size_t(image.width)*image.height*4);

        for(int x=0;x<nx;++x) 
        { 
            for (int y = 0; y < ny; ++y)
            {
                auto tile = ReadTile(factory.Get(), root / std::to_wstring(level) / std::to_wstring(x) / (std::to_wstring(y) + L".jpg"));
                for (int row = 0; row < 256; ++row)
                {
                    // JPEG row 0 is north; OpenGL row 0 is south. Reverse INSIDE each TMS tile.
                    size_t dst = (size_t(y * 256 + 255 - row) * image.width + x * 256) * 4;
                    std::memcpy(image.rgba.data() + dst, tile.data() + row * 256 * 4, 256 * 4);
                }
            }
        }
        return image;
    }
    throw std::runtime_error("No complete NaturalEarthII level 0, 1 or 2 found");
}
*/

EarthImage LoadNaturalEarthII(const std::filesystem::path& root, int maxTextureSize, int level)
{
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf())));
    for (int level = 2; level >= 0; --level)
    {
        int nx = 2 << level;
        int ny = 1 << level;
        if (nx * 256 > maxTextureSize || ny * 256 > maxTextureSize)
            continue;

        bool complete = true;
        for (int x = 0; x < nx; ++x)
        {
            for (int y = 0; y < ny; ++y)
            {
                auto tile = root / std::to_wstring(level) / std::to_wstring(x) / (std::to_wstring(y) + L".jpg");

                if (!std::filesystem::is_regular_file(tile))
                    complete = false;
            }
        }

        if (!complete)
            continue;

        EarthImage image;
        image.width = nx * 256;
        image.height = ny * 256;
        image.level = level;
        image.rgba.resize(size_t(image.width) * image.height * 4);

        for (int x = 0; x < nx; ++x)
        {
            for (int y = 0; y < ny; ++y)
            {
                auto tile = ReadTile(factory.Get(), root / std::to_wstring(level) / std::to_wstring(x) / (std::to_wstring(y) + L".jpg"));
                for (int row = 0; row < 256; ++row)
                {
                    // JPEG row 0 is north; OpenGL row 0 is south. Reverse INSIDE each TMS tile.
                    size_t dst = (size_t(y * 256 + 255 - row) * image.width + x * 256) * 4;
                    std::memcpy(image.rgba.data() + dst, tile.data() + row * 256 * 4, 256 * 4);
                }
            }
        }
        return image;
    }
    throw std::runtime_error("No complete NaturalEarthII level 0, 1 or 2 found");
}