#ifndef GRAPHICS_IMPL_GRAPHICS_SDL_H_

#include "SDL.h"
#include "SDL_ttf.h"
#include "Core/Platform.h"
#include "Math/Rect.h"
#include "Graphics/Impl/Surface_SDL.h"

// Macroses for fast mapping ARGB32/XRGB32 format
#define _ALPHA_SHIFT (24)
#define _RED_SHIFT (16)
#define _GREEN_SHIFT (8)
#define _BLUE_SHIFT (0)

#define _RGBA32(A, R, G, B) ( ((A) << _ALPHA_SHIFT) | ((R) << _RED_SHIFT) | ((G) << _GREEN_SHIFT) | ((B)) << _BLUE_SHIFT )
#define _RGB32(R, G, B) _RGBA32(0, R, G, B)

#define _GET_ALPHA(COLOR) ( ((COLOR) >> _ALPHA_SHIFT) & 0xFF )
#define _GET_RED(COLOR) ( ((COLOR) >> _RED_SHIFT) & 0xFF )
#define _GET_GREEN(COLOR) ( ((COLOR) >> _GREEN_SHIFT) & 0xFF )
#define _GET_BLUE(COLOR) ( ((COLOR) >> _BLUE_SHIFT ) & 0xFF )

class VGraphics
{
public:
    static constexpr i32f BytesPerPixel = 4;
    static constexpr i32f BitsPerPixel = 32;

private:
    VSurface* VideoSurface;
    VSurface* BackSurface;

    SDL_PixelFormat* SDLPixelFormat;
    u32 SDLPixelFormatEnum;

    i32 ScreenWidth;
    i32 ScreenHeight;

    TTF_Font* Font;
    i32 FontCharWidth; // In pixels
    i32 FontCharHeight;

public:
    void StartUp();
    void ShutDown();

    void PrepareToRender();
    void Render();

    FINLINE void DrawSurface(VSurface* Surface, VRelRectI* Source, VRelRectI* Dest)
    {
        BlitScaled(Surface, BackSurface, Source, Dest);
    }
    void DrawText(i32 X, i32 Y, u32 Color, const char* Format, ...);

    static FINLINE void BlitSurface(VSurface* Source, VSurface* Dest, VRelRectI* SourceRect, VRelRectI* DestRect)
    {
        SDL_BlitSurface(Source->SDLSurface, (SDL_Rect*)SourceRect, Dest->SDLSurface, (SDL_Rect*)DestRect);
    }
    static FINLINE void BlitScaled(VSurface* Source, VSurface* Dest, VRelRectI* SourceRect, VRelRectI* DestRect)
    {
        SDL_BlitScaled(Source->SDLSurface, (SDL_Rect*)SourceRect, Dest->SDLSurface, (SDL_Rect*)DestRect);
    }
    static FINLINE void FillRect(VSurface* Dest, VRelRectI* Rect, u32 Color)
    {
        SDL_FillRect(Dest->SDLSurface, (SDL_Rect*)Rect, Color);
    }

private:
    void Flip();

    friend class VSurface;
};

#define GRAPHICS_IMPL_GRAPHICS_SDL_H_
#endif