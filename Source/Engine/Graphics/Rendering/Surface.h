#pragma once

#include "SDL.h"
#include "Common/Types/Common.h"
#include "Common/Math/Vector.h"
#include "Common/Math/Rect.h"
#include "Common/Platform/Platform.h"
#include "Common/Platform/Assert.h"
#include "Engine/Core/Config/Config.h"

namespace Volition
{

class VSurface
{
protected:
    SDL_Surface* SDLSurface;

    u32* Buffer;
    i32 Pitch; /** In pixels */

    i32 Width;
    i32 Height;

    b32 bLocked : 1;
    b32 bDestroyable : 1;

public:
    VSurface();

    void Create(i32 InWidth, i32 InHeight);
    void Create(SDL_Surface* InSDLSurface);

    void Load(const char* Path, u32 SDLPixelFormat);

    VLN_FINLINE void Load(const char* Path)
    {
        Load(Path, Config.RenderSpec.SDLPixelFormatEnum);
    }

    void Destroy();

    void Lock(u32*& OutBuffer, i32& OutPitch);
    void Unlock();

    /** For textures */
    void CorrectColorsSlow(const VVector3& ColorCorrection = { 1.0f, 1.0f, 1.0f });
    void CorrectColorsFast(const VVector3& ColorCorrection = { 1.0f, 1.0f, 1.0f });

    void SetAlphaMode(b32 bMode);

    u32* GetBuffer();
    const u32* GetBuffer() const;

    i32 GetPitch() const;
    b32 IsLocked() const;

    i32 GetWidth() const;
    i32 GetHeight() const;

    void Blit(VRelativeRectInt* SourceRect, VSurface* Dest, VRelativeRectInt* DestRect);
    void FillRect(VRelativeRectInt* Rect, u32 Color);

    friend class VRenderer;
};

VLN_FINLINE void VSurface::Lock(u32*& OutBuffer, i32& OutPitch)
{
    // We don't have to lock SDL surface since we don't use RLE
    VLN_ASSERT(!bLocked);

    OutBuffer = Buffer = (u32*)SDLSurface->pixels;
    OutPitch = Pitch = SDLSurface->pitch >> 2; // Divide by 4 (Bytes per pixel)

    bLocked = true;
}

VLN_FINLINE void VSurface::Unlock()
{
    VLN_ASSERT(bLocked);
    bLocked = false;
}

VLN_FINLINE u32* VSurface::GetBuffer()
{
    VLN_ASSERT(bLocked);
    return Buffer;
}

VLN_FINLINE const u32* VSurface::GetBuffer() const
{
    VLN_ASSERT(bLocked);
    return Buffer;
}

VLN_FINLINE i32 VSurface::GetPitch() const
{
    VLN_ASSERT(bLocked);
    return Pitch;
}

VLN_FINLINE b32 VSurface::IsLocked() const
{
    return bLocked;
}

VLN_FINLINE i32 VSurface::GetWidth() const
{
    return Width;
}

VLN_FINLINE i32 VSurface::GetHeight() const
{
    return Height;
}

VLN_FINLINE void VSurface::Blit(VRelativeRectInt* SourceRect, VSurface* Dest, VRelativeRectInt* DestRect)
{
    SDL_BlitScaled(SDLSurface, (SDL_Rect*)SourceRect, Dest->SDLSurface, (SDL_Rect*)DestRect);
}

VLN_FINLINE void VSurface::FillRect(VRelativeRectInt* Rect, u32 Color)
{
    SDL_FillRect(SDLSurface, (SDL_Rect*)Rect, Color); // SDL_Rect has the same footprint as VRelativeRectInt
}

}
