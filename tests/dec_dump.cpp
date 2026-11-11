// Vuelca los frames decodificados por decodeGif (RGBA) a un .bin para compararlos con Pillow (tests/check_decoder.py).
// g++ -O1 -std=c++20 -Iinclude -Isrc/engine tests/dec_dump.cpp -o /tmp/dec_dump
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "gifanim.hpp"
#include <fstream>
#include <iostream>
using namespace gdx;
int main(int argc,char**argv){ std::ifstream f(argv[1],std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});
 GifData g=decodeGif(b.data(),b.size()); std::ofstream o(argv[2],std::ios::binary);
 int n=g.frames.size(); o.write((char*)&n,4); o.write((char*)&g.w,4); o.write((char*)&g.h,4);
 for(auto&fr:g.frames) o.write((const char*)fr.rgba.data(),fr.rgba.size()); }
