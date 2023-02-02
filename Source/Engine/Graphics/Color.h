/* TODO:
    - Maybe we need [0.0f;1.0f] colors?
 */

#pragma once

#include "Engine/Core/Platform.h"
#include "Engine/Core/Types.h"

#define MAP_ARGB32(A, R, G, B) ( (u32)( ((A) << 24) | ((R) << 16) | ((G) << 8) | (B)) )
#define MAP_XRGB32(R, G, B) MAP_ARGB32(0xFF, R, G, B)

#define MAP_RGBA32(R, G, B, A) ( (u32) ( ((R) << 24) | ((G) << 16) | ((B) << 8) | (A)) )
#define MAP_RGBX32(R, G, B) MAP_RGBA32(R, G, B, 0xFF)

class VColorARGB
{
public:
    union
    {
        u32 ARGB;
        struct
        {
#if VL_ENDIANNESS == VL_LITTLE_ENDIAN
            u8 B, G, R, A;
#else
            u8 A, R, G, B;
#endif
        };
        u8 C[4];
    };

public:
    VL_FINLINE VColorARGB() = default;
    VL_FINLINE VColorARGB(u8 A, u8 R, u8 G, u8 B) :
        ARGB(MAP_ARGB32(A, R, G, B))
    {}
    VL_FINLINE VColorARGB(u32 InARGB) :
        ARGB(InARGB)
    {}

    VL_FINLINE operator u32() const
    {
        return ARGB;
    }
};

// TODO(sean): Do we really need it?
class VColorRGBA
{
public:
    union
    {
        u32 RGBA;
        struct
        {
#if VL_ENDIANNESS == VL_LITTLE_ENDIAN
            u8 A, B, G, R;
#else
            u8 R, G, B, A;
#endif
        };
        u8 C[4];
    };

public:
    VL_FINLINE VColorRGBA() = default;
    VL_FINLINE VColorRGBA(u8 R, u8 G, u8 B, u8 A) :
        RGBA(MAP_RGBA32(R, G, B, A))
    {}
    VL_FINLINE VColorRGBA(u32 InRGBA) :
        RGBA(InRGBA)
    {}

    VL_FINLINE operator u32() const
    {
        return RGBA;
    }
};