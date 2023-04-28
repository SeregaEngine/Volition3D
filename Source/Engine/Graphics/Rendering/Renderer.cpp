#include <cstdarg>
#include "Engine/Core/Platform/Memory.h"
#include "Engine/Core/Window.h"
#include "Engine/World/World.h"
#include "Engine/Graphics/Rendering/Renderer.h"

namespace Volition
{

VRenderer Renderer;

VLN_DEFINE_LOG_CHANNEL(hLogRenderer, "Renderer");

void VRenderer::StartUp()
{
    // Get window surface and init pixel format
    SDL_Surface* SDLSurface;
    {
        SDLSurface = SDL_GetWindowSurface(Window.SDLWindow);
        VLN_ASSERT(SDLSurface);

        Config.RenderSpec.SDLPixelFormat = SDLSurface->format;
        Config.RenderSpec.SDLPixelFormatEnum = Config.RenderSpec.SDLPixelFormat->format;
    }

    // Create video and back surfaces
    {
        VideoSurface.Create(SDLSurface);

        Config.RenderSpec.TargetSize = { VideoSurface.Width, VideoSurface.Height };

        Config.RenderSpec.MinClip = { 0, 0 };
        Config.RenderSpec.MaxClip = { Config.RenderSpec.TargetSize.X - 1, Config.RenderSpec.TargetSize.Y - 1 };

        Config.RenderSpec.MinClipFloat = { (f32)Config.RenderSpec.MinClip.X, (f32)Config.RenderSpec.MinClip.Y };
        Config.RenderSpec.MaxClipFloat = { (f32)Config.RenderSpec.MaxClip.X, (f32)Config.RenderSpec.MaxClip.Y };

        BackSurface.Create(Config.RenderSpec.TargetSize.X, Config.RenderSpec.TargetSize.Y);
    }

    // Initialize TTF
    {
        static constexpr i32f CharsPerLine = 80;
        static constexpr f32 PointDivPixel = 0.75f;
        static constexpr f32 QualityMultiplier = 2.0f;

        const i32 Res = TTF_Init();
        VLN_ASSERT(Res == 0);

        FontCharWidth = Config.RenderSpec.TargetSize.X / CharsPerLine;
        FontCharHeight = (i32)(FontCharWidth * 1.25f);

        Font = TTF_OpenFont("Default.ttf", (i32)( (f32)FontCharWidth * PointDivPixel * QualityMultiplier ));
        VLN_ASSERT(Font);
    }

    // Init renderer stuff 
    {
        RenderList = new VRenderList();
        ZBuffer.Create(Config.RenderSpec.TargetSize.X, Config.RenderSpec.TargetSize.Y);

        Memory.MemSetByte(Materials, 0, sizeof(Materials));
        ResetLights();
    }

    // Allocate memory
    {
        OriginalLitColors = new VColorARGB[VRenderList::MaxPoly][3];
        OriginalColors    = new VColorARGB[VRenderList::MaxPoly];
        OriginalAttrs     = new u32[VRenderList::MaxPoly];
    }

    // Log
    VLN_NOTE(hLogRenderer, "Initialized with %s pixel format\n", SDL_GetPixelFormatName(Config.RenderSpec.SDLPixelFormatEnum));
}

void VRenderer::ShutDown()
{
    // Shut down TTF
    {
        TTF_CloseFont(Font);
        TTF_Quit();
    }

    // Free renderer stuff
    {
        ResetMaterials();
        ResetLights();

        ZBuffer.Destroy();
        delete RenderList;

        BackSurface.Destroy();

        delete[] OriginalAttrs;
        delete[] OriginalColors;
        delete[] OriginalLitColors;
    }
}

void VRenderer::PreRender()
{
    ZBuffer.Clear();
    RenderList->Reset();

    BackSurface.FillRectHW(nullptr, MAP_XRGB32(0x00, 0x00, 0x00));
}

void VRenderer::Render()
{
    // Set up camera
    VCamera& Camera = *World.Camera;
    Camera.BuildWorldToCameraMat44();

    float CameraAngle = Math.Mod(Camera.Dir.Y + World.CubemapMovementEffectAngle, 360.0f);

    // Process cubemap
    VSurface& Cubemap = World.Cubemap;
    if (Cubemap.SDLSurface->pixels)
    {
        // Blit first cubemap
        VRelativeRectInt Src;
        Src.X = (i32)((CameraAngle / 360.0f) * (f32)World.Cubemap.Width + 0.5f);
        Src.Y = 0;
        Src.W = GetScreenWidth();
        Src.H = Cubemap.Height;

        Cubemap.BlitHW(&Src, &BackSurface, nullptr);

        // If we have empty space on screen on right - blit this area
        i32f Remainder = Cubemap.Width - Src.X;
        if (Remainder < GetScreenWidth())
        {
            Src.X = 0;

            VRelativeRectInt Dest;

            Dest.X = Remainder;
            Dest.Y = 0;
            Dest.W = GetScreenWidth();
            Dest.H = GetScreenHeight();
            Cubemap.BlitHW(&Src, &BackSurface, &Dest);
        }
    }

    // Get buffer
    u32* Buffer;
    i32 Pitch;
    BackSurface.Lock(Buffer, Pitch);

    // Insert terrain
    VMesh* TerrainMesh = World.GetTerrain()->Mesh;

    // We already transformed terrain on VTerrain::Update()
    TerrainMesh->ResetRenderState();
    TerrainMesh->Cull(Camera);

    RenderList->InsertMesh(*TerrainMesh, false);

    // Proccess and insert meshes
    for (const auto Entity : World.Entities)
    {
        if (Entity && Entity->Mesh)
        {
            VMesh* Mesh = Entity->Mesh;

            Mesh->ResetRenderState();
            Mesh->TransformModelToWorld();
            Mesh->Cull(Camera);

            RenderList->InsertMesh(*Mesh, false);

            // Make shadow
            if (!OccluderLight)
            {
                continue;
            }

            VPoly* PolyList = Mesh->PolyList;
            static constexpr VColorARGB ShadowColor = VColorARGB(248, 0, 0, 0);

            // Set new color and attributes for mesh
            for (i32f I = 0; I < Mesh->NumPoly; ++I)
            {
                OriginalLitColors[I][0] = PolyList[I].LitColor[0];
                OriginalLitColors[I][1] = PolyList[I].LitColor[1];
                OriginalLitColors[I][2] = PolyList[I].LitColor[2];

                OriginalColors[I] = PolyList[I].OriginalColor;
                OriginalAttrs[I]  = PolyList[I].Attr;

                PolyList[I].LitColor[0] = ShadowColor;
                PolyList[I].LitColor[1] = ShadowColor;
                PolyList[I].LitColor[2] = ShadowColor;

                PolyList[I].OriginalColor = ShadowColor;
                PolyList[I].Attr          = EPolyAttr::ShadeModeEmissive | EPolyAttr::Transparent;
            }

            // Compute shadow vertex positions
            float YShadowPosition = TerrainMesh->Position.Y + 10.0f;

            VVertex* VtxList = Mesh->TransVtxList;
            for (i32f I = 0; I < Mesh->NumVtx; ++I)
            {
                VVector4 Direction = (VtxList[I].Position - OccluderLight->Pos);
                float T = (YShadowPosition - OccluderLight->Pos.Y) / Direction.Y;

                VtxList[I].X = OccluderLight->Pos.X + (T * Direction.X);
                VtxList[I].Y = YShadowPosition;
                VtxList[I].Z = OccluderLight->Pos.Z + (T * Direction.Z);
            }

            // Insert shadow mesh
            Mesh->State &= ~EMeshState::Culled;
            RenderList->InsertMesh(*Mesh, false);

            // Restore mesh color and attributes
            for (i32f I = 0; I < Mesh->NumPoly; ++I)
            {
                PolyList[I].LitColor[0] = OriginalLitColors[I][0];
                PolyList[I].LitColor[1] = OriginalLitColors[I][1];
                PolyList[I].LitColor[2] = OriginalLitColors[I][2];

                PolyList[I].OriginalColor = OriginalColors[I];
                PolyList[I].Attr          = OriginalAttrs[I];
            }
        }
    }

    // Proccess render list
    {
        if (Config.RenderSpec.bBackfaceRemoval)
        {
            RenderList->RemoveBackfaces(Camera);
        }

        RenderList->TransformWorldToCamera(Camera);
        RenderList->Clip(Camera);

        TransformLights(Camera);
        RenderList->Light(Camera, Renderer.Lights, Renderer.MaxLights);

        if (Config.RenderSpec.bSortPolygons)
        {
            RenderList->SortPolygons(ESortPolygonsMethod::Average);
        }
        RenderList->TransformCameraToScreen(Camera);
    }

    // Render stuff
    {
        InterpolationContext.Buffer = Buffer;
        InterpolationContext.BufferPitch = Pitch;

        Config.RenderSpec.bRenderSolid ? RenderSolid() : RenderWire();
    }

    // Unlock buffer
    BackSurface.Unlock();
}

void VRenderer::RenderUI()
{
    for (const auto& TextElement : TextQueue)
    {
        // Render text
        SDL_Surface* SDLSurface = TTF_RenderText_Solid(Font, TextElement.Text, TextElement.Color);
        VLN_ASSERT(SDLSurface);
        SDL_SetColorKey(SDLSurface, SDL_TRUE, static_cast<u32*>(SDLSurface->pixels)[0]);

        // Convert surface
        SDL_Surface* SDLConverted = SDL_ConvertSurface(SDLSurface, Config.RenderSpec.SDLPixelFormat, 0);
        VLN_ASSERT(SDLConverted);

        // Blit
        SDL_Rect Dest = { TextElement.Position.X, TextElement.Position.Y, (i32f)std::strlen(TextElement.Text) * FontCharWidth, FontCharHeight };
        SDL_BlitScaled(SDLConverted, nullptr, BackSurface.SDLSurface, &Dest);

        // Free memory
        SDL_FreeSurface(SDLSurface);
        SDL_FreeSurface(SDLConverted);
    }
}

void VRenderer::PostRender()
{
    SDL_BlitSurface(BackSurface.SDLSurface, nullptr, VideoSurface.SDLSurface, nullptr);
    SDL_UpdateWindowSurface(Window.SDLWindow);

    TextQueue.Clear();
    DebugTextY = 0;
}

void VRenderer::DrawLine(u32* Buffer, i32 Pitch, i32 X1, i32 Y1, i32 X2, i32 Y2, u32 Color)
{
    i32f DX, DY, DX2, DY2, XInc, YInc, Error;

    // Compute deltas
    DX = X2 - X1;
    DY = Y2 - Y1;

    // Get X/Y increments and positive deltas
    if (DX >= 0)
    {
        XInc = 1;
    }
    else
    {
        XInc = -1;
        DX = -DX;
    }

    if (DY >= 0)
    {
        YInc = Pitch;
    }
    else
    {
        YInc = -Pitch;
        DY = -DY;
    }

    DX2 = DX << 1;
    DY2 = DY << 1;

    // Compute buffer start
    Buffer += Y1 * Pitch + X1;

    if (DX > DY)
    {
        Error = DY2 - DX;

        for (i32f I = 0; I < DX; ++I)
        {
            *Buffer = Color;

            if (Error >= 0)
            {
                Error -= DX2;
                Buffer += YInc;
            }

            Error += DY2;
            Buffer += XInc;
        }
    }
    else
    {
        Error = DX2 - DY;

        for (i32f I = 0; I < DY; ++I)
        {
            *Buffer = Color;

            if (Error >= 0)
            {
                Error -= DY2;
                Buffer += XInc;
            }

            Error += DX2;
            Buffer += YInc;
        }
    }
}

void VRenderer::DrawLineSlow(u32* Buffer, i32 Pitch, i32 X1, i32 Y1, i32 X2, i32 Y2, u32 Color)
{
    if (Math.Abs(X2 - X1) > Math.Abs(Y2 - Y1))
    {
        i32 T;
        if (X1 > X2)
        {
            VLN_SWAP(X1, X2, T);
            VLN_SWAP(Y1, Y2, T);
        }

        const f32 M = (f32)(Y2 - Y1) / (f32)(X2 - X1);
        const f32 B = Y1 - X1 * M;

        for (i32 X = X1; X < X2; ++X)
        {
            f32 Y = M * (f32)X + B;
            Buffer[(i32f)Y * Pitch + X] = Color;
        }
    }
    else
    {
        i32 T;
        if (Y1 > Y2)
        {
            VLN_SWAP(X1, X2, T);
            VLN_SWAP(Y1, Y2, T);
        }

        const f32 M = (f32)(X2 - X1) / (f32)(Y2 - Y1);
        const f32 B = X1 - Y1 * M;

        for (i32 Y = Y1; Y < Y2; ++Y)
        {
            f32 X = M * (f32)Y + B;
            Buffer[Y * Pitch + (i32f)X] = Color;
        }
    }
}

b32 VRenderer::ClipLine(i32& X1, i32& Y1, i32& X2, i32& Y2) const
{
    /* NOTE(sean):
        We can optimize this function if we throw away floating
        point computations, but we could loss in accuracy...
    */

    enum EClipCode
    {
        CC_N = VLN_BIT(1),
        CC_W = VLN_BIT(2),
        CC_S = VLN_BIT(3),
        CC_E = VLN_BIT(4),

        CC_NW = CC_N | CC_W,
        CC_NE = CC_N | CC_E,
        CC_SW = CC_S | CC_W,
        CC_SE = CC_S | CC_E,
    };

    i32 CX1 = X1, CY1 = Y1, CX2 = X2, CY2 = Y2;
    i32 Code1 = 0, Code2 = 0;

    // Define codes
    if (CX1 < Config.RenderSpec.MinClip.X)
        Code1 |= CC_W;
    else if (CX1 > Config.RenderSpec.MaxClip.X)
        Code1 |= CC_E;
    if (CY1 < Config.RenderSpec.MinClip.Y)
        Code1 |= CC_N;
    else if (CY1 > Config.RenderSpec.MaxClip.Y)
        Code1 |= CC_S;

    if (CX2 < Config.RenderSpec.MinClip.X)
        Code2 |= CC_W;
    else if (CX2 > Config.RenderSpec.MaxClip.X)
        Code2 |= CC_E;
    if (CY2 < Config.RenderSpec.MinClip.Y)
        Code2 |= CC_N;
    else if (CY2 > Config.RenderSpec.MaxClip.Y)
        Code2 |= CC_S;

    // Center is 0, so check if we can't see whole line
    if (Code1 & Code2)
        return false;

    // We see whole line, don't need to clip
    if (Code1 == 0 && Code2 == 0)
        return true;

    // Clip line
    switch (Code1)
    {
    case CC_N:
    {
        CY1 = Config.RenderSpec.MinClip.Y;
        CX1 = (i32)(
            0.5f + (X1 + (CY1 - Y1) * (X2-X1) / (f32)(Y2-Y1))
        );
    } break;

    case CC_S:
    {
        CY1 = Config.RenderSpec.MaxClip.Y;
        CX1 = (i32)(
            0.5f + (X1 + (CY1 - Y1) * (X2-X1) / (f32)(Y2-Y1))
        );
    } break;

    case CC_W:
    {
        CX1 = Config.RenderSpec.MinClip.X;
        CY1 = (i32)(
            0.5f + (Y1 + (CX1 - X1) * (Y2-Y1) / (f32)(X2-X1))
        );
    } break;

    case CC_E:
    {
        CX1 = Config.RenderSpec.MaxClip.X;
        CY1 = (i32)(
            0.5f + (Y1 + (CX1 - X1) * (Y2-Y1) / (f32)(X2-X1))
        );
    } break;

    case CC_NW:
    {
        CY1 = Config.RenderSpec.MinClip.Y;
        CX1 = (i32)(
            0.5f + (X1 + (CY1 - Y1) * (X2-X1) / (f32)(Y2-Y1))
        );

        if (CX1 < Config.RenderSpec.MinClip.X || CX1 > Config.RenderSpec.MaxClip.X)
        {
            CX1 = Config.RenderSpec.MinClip.X;
            CY1 = (i32)(
                0.5f + (Y1 + (CX1 - X1) * (Y2-Y1) / (f32)(X2-X1))
            );
        }
    } break;

    case CC_NE:
    {
        CY1 = Config.RenderSpec.MinClip.Y;
        CX1 = (i32)(
            0.5f + (X1 + (CY1 - Y1) * (X2-X1) / (f32)(Y2-Y1))
        );

        if (CX1 < Config.RenderSpec.MinClip.X || CX1 > Config.RenderSpec.MaxClip.X)
        {
            CX1 = Config.RenderSpec.MaxClip.X;
            CY1 = (i32)(
                0.5f + (Y1 + (CX1 - X1) * (Y2-Y1) / (f32)(X2-X1))
            );
        }
    } break;

    case CC_SW:
    {
        CY1 = Config.RenderSpec.MaxClip.Y;
        CX1 = (i32)(
            0.5f + (X1 + (CY1 - Y1) * (X2-X1) / (f32)(Y2-Y1))
        );

        if (CX1 < Config.RenderSpec.MinClip.X || CX1 > Config.RenderSpec.MaxClip.X)
        {
            CX1 = Config.RenderSpec.MinClip.X;
            CY1 = (i32)(
                0.5f + (Y1 + (CX1 - X1) * (Y2-Y1) / (f32)(X2-X1))
            );
        }
    } break;

    case CC_SE:
    {
        CY1 = Config.RenderSpec.MaxClip.Y;
        CX1 = (i32)(
            0.5f + (X1 + (CY1 - Y1) * (X2-X1) / (f32)(Y2-Y1))
        );

        if (CX1 < Config.RenderSpec.MinClip.X || CX1 > Config.RenderSpec.MaxClip.X)
        {
            CX1 = Config.RenderSpec.MaxClip.X;
            CY1 = (i32)(
                0.5f + (Y1 + (CX1 - X1) * (Y2-Y1) / (f32)(X2-X1))
            );
        }
    } break;

    default: {} break;
    }

    switch (Code2)
    {
    case CC_N:
    {
        CY2 = Config.RenderSpec.MinClip.Y;
        CX2 = (i32)(
            0.5f + (X2 + (CY2 - Y2) * (X1-X2) / (f32)(Y1-Y2))
        );
    } break;

    case CC_S:
    {
        CY2 = Config.RenderSpec.MaxClip.Y;
        CX2 = (i32)(
            0.5f + (X2 + (CY2 - Y2) * (X1-X2) / (f32)(Y1-Y2))
        );
    } break;

    case CC_W:
    {
        CX2 = Config.RenderSpec.MinClip.X;
        CY2 = (i32)(
            0.5f + (Y2 + (CX2 - X2) * (Y1-Y2) / (f32)(X1-X2))
        );
    } break;

    case CC_E:
    {
        CX2 = Config.RenderSpec.MaxClip.X;
        CY2 = (i32)(
            0.5f + (Y2 + (CX2 - X2) * (Y1-Y2) / (f32)(X1-X2))
        );
    } break;

    case CC_NW:
    {
        CY2 = Config.RenderSpec.MinClip.Y;
        CX2 = (i32)(
            0.5f + (X2 + (CY2 - Y2) * (X1-X2) / (f32)(Y1-Y2))
        );

        if (CX2 < Config.RenderSpec.MinClip.X || CX2 > Config.RenderSpec.MaxClip.X)
        {
            CX2 = Config.RenderSpec.MinClip.X;
            CY2 = (i32)(
                0.5f + (Y2 + (CX2 - X2) * (Y1-Y2) / (f32)(X1-X2))
            );
        }
    } break;

    case CC_NE:
    {
        CY2 = Config.RenderSpec.MinClip.Y;
        CX2 = (i32)(
            0.5f + (X2 + (CY2 - Y2) * (X1-X2) / (f32)(Y1-Y2))
        );

        if (CX2 < Config.RenderSpec.MinClip.X || CX2 > Config.RenderSpec.MaxClip.X)
        {
            CX2 = Config.RenderSpec.MaxClip.X;
            CY2 = (i32)(
                0.5f + (Y2 + (CX2 - X2) * (Y1-Y2) / (f32)(X1-X2))
            );
        }
    } break;

    case CC_SW:
    {
        CY2 = Config.RenderSpec.MaxClip.Y;
        CX2 = (i32)(
            0.5f + (X2 + (CY2 - Y2) * (X1-X2) / (f32)(Y1-Y2))
        );

        if (CX2 < Config.RenderSpec.MinClip.X || CX2 > Config.RenderSpec.MaxClip.X)
        {
            CX2 = Config.RenderSpec.MinClip.X;
            CY2 = (i32)(
                0.5f + (Y2 + (CX2 - X2) * (Y1-Y2) / (f32)(X1-X2))
            );
        }
    } break;

    case CC_SE:
    {
        CY2 = Config.RenderSpec.MaxClip.Y;
        CX2 = (i32)(
            0.5f + (X2 + (CY2 - Y2) * (X1-X2) / (f32)(Y1-Y2))
        );

        if (CX2 < Config.RenderSpec.MinClip.X || CX2 > Config.RenderSpec.MaxClip.X)
        {
            CX2 = Config.RenderSpec.MaxClip.X;
            CY2 = (i32)(
                0.5f + (Y2 + (CX2 - X2) * (Y1-Y2) / (f32)(X1-X2))
            );
        }
    } break;

    default: {} break;
    }

    // Check if we still can't see the line
    if (CX1 < Config.RenderSpec.MinClip.X || CX1 > Config.RenderSpec.MaxClip.X ||
        CX2 < Config.RenderSpec.MinClip.X || CX2 > Config.RenderSpec.MaxClip.X ||
        CY1 < Config.RenderSpec.MinClip.Y || CY1 > Config.RenderSpec.MaxClip.Y ||
        CY2 < Config.RenderSpec.MinClip.Y || CY2 > Config.RenderSpec.MaxClip.Y)
    {
        return false;
    }

    X1 = CX1;
    X2 = CX2;
    Y1 = CY1;
    Y2 = CY2;

    return true;
}

void VRenderer::DrawTriangle(VInterpolationContext& InterpolationContext)
{
    enum class ETriangleCase
    {
        Top,
        Bottom,
        General
    };

    u32* Buffer = InterpolationContext.Buffer;
    const i32 Pitch = InterpolationContext.BufferPitch;

    i32 V0 = 0, V1 = 1, V2 = 2;

    // Sort by Y
    i32 TempInt;
    if (InterpolationContext.Vtx[V1].Y < InterpolationContext.Vtx[V0].Y)
    {
        VLN_SWAP(V0, V1, TempInt);
    }
    if (InterpolationContext.Vtx[V2].Y < InterpolationContext.Vtx[V0].Y)
    {
        VLN_SWAP(V0, V2, TempInt);
    }
    if (InterpolationContext.Vtx[V2].Y < InterpolationContext.Vtx[V1].Y)
    {
        VLN_SWAP(V1, V2, TempInt);
    }

    // Test if we can't see it
    if (InterpolationContext.Vtx[V2].Y < Config.RenderSpec.MinClipFloat.Y ||
        InterpolationContext.Vtx[V0].Y > Config.RenderSpec.MaxClipFloat.Y ||
        (InterpolationContext.Vtx[V0].X < Config.RenderSpec.MinClipFloat.X &&
         InterpolationContext.Vtx[V1].X < Config.RenderSpec.MinClipFloat.X &&
         InterpolationContext.Vtx[V2].X < Config.RenderSpec.MinClipFloat.X) ||
        (InterpolationContext.Vtx[V0].X > Config.RenderSpec.MaxClipFloat.X &&
         InterpolationContext.Vtx[V1].X > Config.RenderSpec.MaxClipFloat.X &&
         InterpolationContext.Vtx[V2].X > Config.RenderSpec.MaxClipFloat.X))
    {
        return;
    }

    // Convert Y to integers
    i32 Y0 = (i32)(InterpolationContext.Vtx[V0].Y + 0.5f);
    i32 Y1 = (i32)(InterpolationContext.Vtx[V1].Y + 0.5f);
    i32 Y2 = (i32)(InterpolationContext.Vtx[V2].Y + 0.5f);

    // Found triangle case and sort vertices by X
    ETriangleCase TriangleCase;
    if (Y0 == Y1)
    {
        if (InterpolationContext.Vtx[V1].X < InterpolationContext.Vtx[V0].X)
        {
            VLN_SWAP(V0, V1, TempInt);
            VLN_SWAP(Y0, Y1, TempInt);
        }
        TriangleCase = ETriangleCase::Top;
    }
    else if (Y1 == Y2)
    {
        if (InterpolationContext.Vtx[V2].X < InterpolationContext.Vtx[V1].X)
        {
            VLN_SWAP(V1, V2, TempInt);
            VLN_SWAP(Y1, Y2, TempInt);
        }
        TriangleCase = ETriangleCase::Bottom;
    }
    else
    {
        TriangleCase = ETriangleCase::General;
    }

    // Convert coords to integer
    i32 X0 = (i32)(InterpolationContext.Vtx[V0].X + 0.5f);
    i32 X1 = (i32)(InterpolationContext.Vtx[V1].X + 0.5f);
    i32 X2 = (i32)(InterpolationContext.Vtx[V2].X + 0.5f);

    // Vertical, horizontal triangle test
    if ((Y0 == Y1 && Y1 == Y2) || (X0 == X1 && X1 == X2))
    {
        return;
    }

    InterpolationContext.VtxIndices[0] = V0;
    InterpolationContext.VtxIndices[1] = V1;
    InterpolationContext.VtxIndices[2] = V2;

    SetInterpolators();

    for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
    {
        InterpolationContext.Interpolators[InterpIndex]->SetInterpolationContext(InterpolationContext);
        InterpolationContext.Interpolators[InterpIndex]->Start();
    }

    i32 YStart;
    i32 YEnd;

    fx28 ZVtx0 = IntToFx28(1) / (i32)(InterpolationContext.Vtx[V0].Z + 0.5f);
    fx28 ZVtx1 = IntToFx28(1) / (i32)(InterpolationContext.Vtx[V1].Z + 0.5f);
    fx28 ZVtx2 = IntToFx28(1) / (i32)(InterpolationContext.Vtx[V2].Z + 0.5f);

    // Fixed coords, color channels for rasterization
    fx16 XLeft;
    fx16 XRight;
    fx28 ZLeft, ZRight;

    // Coords, colors fixed deltas by Y
    fx16 XDeltaLeftByY;
    fx28 ZDeltaLeftByY;

    fx16 XDeltaRightByY;
    fx28 ZDeltaRightByY;

    fx28* ZBufferArray;

    if (TriangleCase == ETriangleCase::Top ||
        TriangleCase == ETriangleCase::Bottom)
    {
        i32 YDiff = Y2 - Y0;

        if (TriangleCase == ETriangleCase::Top)
        {
            // Compute deltas for coords, colors
            XDeltaLeftByY = IntToFx16(X2 - X0) / YDiff;
            ZDeltaLeftByY = (ZVtx2 - ZVtx0) / YDiff;

            XDeltaRightByY = IntToFx16(X2 - X1) / YDiff;
            ZDeltaRightByY = (ZVtx2 - ZVtx1) / YDiff;

            for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
            {
                InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltas(YDiff, V0, V2, YDiff, V1, V2);
            }

            // Clipping Y
            if (Y0 < Config.RenderSpec.MinClip.Y)
            {
                YDiff = Config.RenderSpec.MinClip.Y - Y0;
                YStart = Config.RenderSpec.MinClip.Y;

                XLeft = IntToFx16(X0) + YDiff * XDeltaLeftByY;
                ZLeft = (ZVtx0) + YDiff * ZDeltaLeftByY;

                XRight = IntToFx16(X1) + YDiff * XDeltaRightByY;
                ZRight = (ZVtx1) + YDiff * ZDeltaRightByY;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->InterpolateY(YDiff, YDiff);
                }
            }
            else
            {
                YStart = Y0;

                XLeft = IntToFx16(X0);
                ZLeft = (ZVtx0);

                XRight = IntToFx16(X1);
                ZRight = (ZVtx1);
            }
        }
        else // Bottom case
        {
            // Compute deltas for coords, colors
            XDeltaLeftByY = IntToFx16(X1 - X0) / YDiff;
            ZDeltaLeftByY = (ZVtx1 - ZVtx0) / YDiff;

            XDeltaRightByY = IntToFx16(X2 - X0) / YDiff;
            ZDeltaRightByY = (ZVtx2 - ZVtx0) / YDiff;

            for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
            {
                InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltas(YDiff, V0, V1, YDiff, V0, V2);
            }

            // Clipping Y
            if (Y0 < Config.RenderSpec.MinClip.Y)
            {
                YDiff = Config.RenderSpec.MinClip.Y - Y0;
                YStart = Config.RenderSpec.MinClip.Y;

                XLeft = IntToFx16(X0) + YDiff * XDeltaLeftByY;
                ZLeft = (ZVtx0) + YDiff * ZDeltaLeftByY;

                XRight = IntToFx16(X0) + YDiff * XDeltaRightByY;
                ZRight = (ZVtx0) + YDiff * ZDeltaRightByY;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->InterpolateY(YDiff, YDiff);
                }
            }
            else
            {
                YStart = Y0;

                XLeft = IntToFx16(X0);
                ZLeft = (ZVtx0);

                XRight = IntToFx16(X0);
                ZRight = (ZVtx0);
            }
        }

        // Clip bottom Y
        if (Y2 > Config.RenderSpec.MaxClip.Y)
        {
            YEnd = Config.RenderSpec.MaxClip.Y + 1; // + 1 because of top-left fill convention
        }
        else
        {
            YEnd = Y2;
        }

        // Test for clipping X
        if (X0 < Config.RenderSpec.MinClip.X || X1 < Config.RenderSpec.MinClip.X || X2 < Config.RenderSpec.MinClip.X ||
            X0 > Config.RenderSpec.MaxClip.X || X1 > Config.RenderSpec.MaxClip.X || X2 > Config.RenderSpec.MaxClip.X)
        {
            // Align buffer pointer
            Buffer += Pitch * YStart;
            ZBufferArray = (fx28*)ZBuffer.Buffer + (ZBuffer.Pitch * YStart);

            // Process each Y
            for (InterpolationContext.Y = YStart; InterpolationContext.Y < YEnd; ++InterpolationContext.Y)
            {
                // Compute starting values
                i32f XStart = Fx16ToIntRounded(XLeft);
                i32f XEnd = Fx16ToIntRounded(XRight);

                fx16 Z = ZLeft;
                fx16 ZDeltaByX;

                // Compute deltas for X interpolation
                const i32f XDiff = XEnd - XStart;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->ComputeXStartsAndDeltas(XDiff, ZLeft, ZRight);
                }

                if (XDiff > 0)
                {
                    ZDeltaByX = (ZRight - ZLeft) / XDiff;
                }
                else
                {
                    ZDeltaByX = (ZRight - ZLeft);
                }

                // X clipping
                if (XStart < Config.RenderSpec.MinClip.X)
                {
                    const i32 XDiff = Config.RenderSpec.MinClip.X - XStart;
                    XStart = Config.RenderSpec.MinClip.X;

                    Z += XDiff * ZDeltaByX;

                    for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                    {
                        InterpolationContext.Interpolators[InterpIndex]->InterpolateX(XDiff);
                    }
                }
                if (XEnd > Config.RenderSpec.MaxClip.X)
                {
                    XEnd = Config.RenderSpec.MaxClip.X + 1;
                }

                // Process each X
                for (i32f X = XStart; X < XEnd; ++X)
                {
                    if (Z > ZBufferArray[X])
                    {
                        InterpolationContext.Pixel = 0xFFFFFFFF;
                        InterpolationContext.X = X;
                        InterpolationContext.Z = Z;

                        for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                        {
                            InterpolationContext.Interpolators[InterpIndex]->ProcessPixel();
                        }

                        Buffer[X] = InterpolationContext.Pixel;

                        ZBufferArray[X] = Z;
                    }

                    // Interpolate by X
                    Z += ZDeltaByX;

                    for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                    {
                        InterpolationContext.Interpolators[InterpIndex]->InterpolateX();
                    }
                }

                // Interpolate by Y
                XLeft += XDeltaLeftByY;
                ZLeft += ZDeltaLeftByY;

                XRight += XDeltaRightByY;
                ZRight += ZDeltaRightByY;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->InterpolateY();
                }

                Buffer += Pitch;
                ZBufferArray += ZBuffer.Pitch;
            }
        }
        else // Non-clipped version
        {
            // Align buffer pointer
            Buffer += Pitch * YStart;
            ZBufferArray = (fx28*)ZBuffer.Buffer + (ZBuffer.Pitch * YStart);

            // Process each Y
            for (InterpolationContext.Y = YStart; InterpolationContext.Y < YEnd; ++InterpolationContext.Y)
            {
                // Compute starting values
                const i32f XStart = Fx16ToIntRounded(XLeft);
                const i32f XEnd = Fx16ToIntRounded(XRight);

                fx16 Z = ZLeft;
                fx16 ZDeltaByX;

                // Compute deltas for X interpolation
                const i32f XDiff = XEnd - XStart;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->ComputeXStartsAndDeltas(XDiff, ZLeft, ZRight);
                }

                if (XDiff > 0)
                {
                    ZDeltaByX = (ZRight - ZLeft) / XDiff;
                }
                else
                {
                    ZDeltaByX = (ZRight - ZLeft);
                }

                // Process each X
                for (i32f X = XStart; X < XEnd; ++X)
                {
                    if (Z > ZBufferArray[X])
                    {
                        InterpolationContext.Pixel = 0xFFFFFFFF;
                        InterpolationContext.X = X;
                        InterpolationContext.Z = Z;

                        for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                        {
                            InterpolationContext.Interpolators[InterpIndex]->ProcessPixel();
                        }

                        Buffer[X] = InterpolationContext.Pixel;

                        ZBufferArray[X] = Z;
                    }

                    // Interpolate by X
                    Z += ZDeltaByX;

                    for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                    {
                        InterpolationContext.Interpolators[InterpIndex]->InterpolateX();
                    }
                }

                // Interpolate by Y
                XLeft += XDeltaLeftByY;
                ZLeft += ZDeltaLeftByY;

                XRight += XDeltaRightByY;
                ZRight += ZDeltaRightByY;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->InterpolateY();
                }

                Buffer += Pitch;
                ZBufferArray += ZBuffer.Pitch;
            }
        }
    }
    else // General case
    {
        b32 bRestartInterpolationAtLeftHand = true;
        i32 YRestartInterpolation = Y1;

        // Clip bottom Y
        if (Y2 > Config.RenderSpec.MaxClip.Y)
        {
            YEnd = Config.RenderSpec.MaxClip.Y + 1;
        }
        else
        {
            YEnd = Y2;
        }

        // Clip top Y
        if (Y1 < Config.RenderSpec.MinClip.Y)
        {
            // Compute deltas
            const i32 YDiffLeft = (Y2 - Y1);
            XDeltaLeftByY = IntToFx16(X2 - X1) / YDiffLeft;
            ZDeltaLeftByY = (ZVtx2 - ZVtx1) / YDiffLeft;

            const i32 YDiffRight = (Y2 - Y0);
            XDeltaRightByY = IntToFx16(X2 - X0) / YDiffRight;
            ZDeltaRightByY = (ZVtx2 - ZVtx0) / YDiffRight;

            // Do clipping
            const i32 YOverClipLeft = (Config.RenderSpec.MinClip.Y - Y1);
            XLeft = IntToFx16(X1) + YOverClipLeft * XDeltaLeftByY;
            ZLeft = (ZVtx1) + YOverClipLeft * ZDeltaLeftByY;

            const i32 YOverClipRight = (Config.RenderSpec.MinClip.Y - Y0);
            XRight = IntToFx16(X0) + YOverClipRight * XDeltaRightByY;
            ZRight = (ZVtx0) + YOverClipRight * ZDeltaRightByY;

            // Do both for interpolators
            for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
            {
                InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltas(YDiffLeft, V1, V2, YDiffRight, V0, V2);
                InterpolationContext.Interpolators[InterpIndex]->InterpolateY(YOverClipLeft, YOverClipRight);
            }

            YStart = Config.RenderSpec.MinClip.Y;

            /* NOTE(sean):
                Test if we need swap to keep rendering left to right.
                It can happen because we assume that
                Y1 is on left hand side and Y2 on right.
            */
            if (XDeltaRightByY > XDeltaLeftByY)
            {
                VLN_SWAP(XDeltaLeftByY, XDeltaRightByY, TempInt);
                VLN_SWAP(ZDeltaLeftByY, ZDeltaRightByY, TempInt);

                VLN_SWAP(XLeft, XRight, TempInt);
                VLN_SWAP(ZLeft, ZRight, TempInt);

                VLN_SWAP(X1, X2, TempInt);
                VLN_SWAP(Y1, Y2, TempInt);
                VLN_SWAP(ZVtx1, ZVtx2, TempInt);

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->SwapLeftRight();
                }

                bRestartInterpolationAtLeftHand = false; // Restart at right hand side
            }
        }
        else if (Y0 < Config.RenderSpec.MinClip.Y)
        {
            // Compute deltas
            const i32 YDiffLeft = (Y1 - Y0);
            XDeltaLeftByY = IntToFx16(X1 - X0) / YDiffLeft;
            ZDeltaLeftByY = (ZVtx1 - ZVtx0) / YDiffLeft;

            const i32 YDiffRight = (Y2 - Y0);
            XDeltaRightByY = IntToFx16(X2 - X0) / YDiffRight;
            ZDeltaRightByY = (ZVtx2 - ZVtx0) / YDiffRight;

            // Do clipping
            const i32 YOverClip = (Config.RenderSpec.MinClip.Y - Y0);
            XLeft = IntToFx16(X0) + YOverClip * XDeltaLeftByY;
            ZLeft = (ZVtx0) + YOverClip * ZDeltaLeftByY;

            XRight = IntToFx16(X0) + YOverClip * XDeltaRightByY;
            ZRight = (ZVtx0) + YOverClip * ZDeltaRightByY;

            for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
            {
                InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltas(YDiffLeft, V0, V1, YDiffRight, V0, V2);
                InterpolationContext.Interpolators[InterpIndex]->InterpolateY(YOverClip, YOverClip);
            }

            YStart = Config.RenderSpec.MinClip.Y;

            /* NOTE(sean):
                Test if we need swap to keep rendering left to right.
                It can happen because we assume that
                Y1 is on left hand side and Y2 on right.
            */
            if (XDeltaRightByY < XDeltaLeftByY)
            {
                VLN_SWAP(XDeltaLeftByY, XDeltaRightByY, TempInt);
                VLN_SWAP(ZDeltaLeftByY, ZDeltaRightByY, TempInt);

                VLN_SWAP(XLeft, XRight, TempInt);
                VLN_SWAP(ZLeft, ZRight, TempInt);

                VLN_SWAP(X1, X2, TempInt);
                VLN_SWAP(Y1, Y2, TempInt);
                VLN_SWAP(ZVtx1, ZVtx2, TempInt);

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->SwapLeftRight();
                }

                bRestartInterpolationAtLeftHand = false; // Restart at right hand side
            }
        }
        else // No top Y clipping
        {
            i32 YDiffLeft = (Y1 - Y0);
            XDeltaLeftByY = IntToFx16(X1 - X0) / YDiffLeft;
            ZDeltaLeftByY = (ZVtx1 - ZVtx0) / YDiffLeft;

            i32 YDiffRight = (Y2 - Y0);
            XDeltaRightByY = IntToFx16(X2 - X0) / YDiffRight;
            ZDeltaRightByY = (ZVtx2 - ZVtx0) / YDiffRight;

            XRight = XLeft = IntToFx16(X0);
            ZRight = ZLeft = (ZVtx0);

            for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
            {
                InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltas(YDiffLeft, V0, V1, YDiffRight, V0, V2);
            }

            YStart = Y0;

            /* NOTE(sean):
                Test if we need swap to keep rendering left to right.
                It can happen because we assume that
                Y1 is on left hand side and Y2 on right.
            */
            if (XDeltaRightByY < XDeltaLeftByY)
            {
                VLN_SWAP(XDeltaLeftByY, XDeltaRightByY, TempInt);
                VLN_SWAP(ZDeltaLeftByY, ZDeltaRightByY, TempInt);

                VLN_SWAP(XLeft, XRight, TempInt);
                VLN_SWAP(ZLeft, ZRight, TempInt);

                VLN_SWAP(X1, X2, TempInt);
                VLN_SWAP(Y1, Y2, TempInt);
                VLN_SWAP(ZVtx1, ZVtx2, TempInt);

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->SwapLeftRight();
                }

                bRestartInterpolationAtLeftHand = false; // Restart at right hand side
            }
        }

        // Test for clipping X
        if (X0 < Config.RenderSpec.MinClip.X || X1 < Config.RenderSpec.MinClip.X || X2 < Config.RenderSpec.MinClip.X ||
            X0 > Config.RenderSpec.MaxClip.X || X1 > Config.RenderSpec.MaxClip.X || X2 > Config.RenderSpec.MaxClip.X)
        {
            // Align buffer pointer
            Buffer += Pitch * YStart;
            ZBufferArray = (fx28*)ZBuffer.Buffer + (ZBuffer.Pitch * YStart);

            // Process each Y
            for (InterpolationContext.Y = YStart; InterpolationContext.Y < YEnd; ++InterpolationContext.Y)
            {
                // Compute starting values
                i32f XStart = Fx16ToIntRounded(XLeft);
                i32f XEnd = Fx16ToIntRounded(XRight);

                fx16 Z = ZLeft;
                fx16 ZDeltaByX;

                // Compute deltas for X interpolation
                const i32f XDiff = XEnd - XStart;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->ComputeXStartsAndDeltas(XDiff, ZLeft, ZRight);
                }

                if (XDiff > 0)
                {
                    ZDeltaByX = (ZRight - ZLeft) / XDiff;
                }
                else
                {
                    ZDeltaByX = (ZRight - ZLeft);
                }

                // X clipping
                if (XStart < Config.RenderSpec.MinClip.X)
                {
                    const i32 XDiff = Config.RenderSpec.MinClip.X - XStart;
                    XStart = Config.RenderSpec.MinClip.X;

                    Z += XDiff * ZDeltaByX;

                    for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                    {
                        InterpolationContext.Interpolators[InterpIndex]->InterpolateX(XDiff);
                    }
                }
                if (XEnd > Config.RenderSpec.MaxClip.X)
                {
                    XEnd = Config.RenderSpec.MaxClip.X + 1;
                }

                // Process each X
                for (i32f X = XStart; X < XEnd; ++X)
                {
                    if (Z > ZBufferArray[X])
                    {
                        InterpolationContext.Pixel = 0xFFFFFFFF;
                        InterpolationContext.X = X;
                        InterpolationContext.Z = Z;

                        for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                        {
                            InterpolationContext.Interpolators[InterpIndex]->ProcessPixel();
                        }

                        Buffer[X] = InterpolationContext.Pixel;

                        ZBufferArray[X] = Z;
                    }

                    // Interpolate by X
                    Z += ZDeltaByX;

                    for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                    {
                        InterpolationContext.Interpolators[InterpIndex]->InterpolateX();
                    }
                }

                // Interpolate by Y
                XLeft += XDeltaLeftByY;
                ZLeft += ZDeltaLeftByY;

                XRight += XDeltaRightByY;
                ZRight += ZDeltaRightByY;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->InterpolateY();
                }

                Buffer += Pitch;
                ZBufferArray += ZBuffer.Pitch;

                // Test for changing interpolant
                if (InterpolationContext.Y == YRestartInterpolation)
                {
                    if (bRestartInterpolationAtLeftHand)
                    {
                        // Compute new values to get from Y1 to Y2
                        const i32 YDiff = (Y2 - Y1);

                        XDeltaLeftByY = IntToFx16(X2 - X1) / YDiff;
                        ZDeltaLeftByY = (ZVtx2 - ZVtx1) / YDiff;

                        XLeft = IntToFx16(X1);
                        ZLeft = (ZVtx1);

                        // Align down on 1 Y
                        XLeft += XDeltaLeftByY;
                        ZLeft += ZDeltaLeftByY;

                        // Do both for interpolators
                        for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                        {
                            InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltasLeft(YDiff, V1, V2);
                            InterpolationContext.Interpolators[InterpIndex]->InterpolateYLeft();
                        }
                    }
                    else
                    {
                        // Compute new values to get from Y2 to Y1 because we swapped them
                        const i32 YDiff = (Y1 - Y2);

                        XDeltaRightByY = IntToFx16(X1 - X2) / YDiff;
                        ZDeltaRightByY = (ZVtx1 - ZVtx2) / YDiff;

                        XRight = IntToFx16(X2);
                        ZRight = (ZVtx2);

                        // Align down on 1 Y
                        XRight += XDeltaRightByY;
                        ZRight += ZDeltaRightByY;

                        // Do both for interpolators
                        for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                        {
                            InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltasRight(YDiff, V2, V1);
                            InterpolationContext.Interpolators[InterpIndex]->InterpolateYRight();
                        }
                    }
                }
            }
        }
        else // No X clipping
        {
            // Align buffer pointer
            Buffer += Pitch * YStart;
            ZBufferArray = (fx28*)ZBuffer.Buffer + (ZBuffer.Pitch * YStart);

            // Process each Y
            for (InterpolationContext.Y = YStart; InterpolationContext.Y < YEnd; ++InterpolationContext.Y)
            {
                // Compute starting values
                const i32f XStart = Fx16ToIntRounded(XLeft);
                const i32f XEnd = Fx16ToIntRounded(XRight);

                fx16 Z = ZLeft;
                fx16 ZDeltaByX;

                // Compute deltas for X interpolation
                const i32f XDiff = XEnd - XStart;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->ComputeXStartsAndDeltas(XDiff, ZLeft, ZRight);
                }

                if (XDiff > 0)
                {
                    ZDeltaByX = (ZRight - ZLeft) / XDiff;
                }
                else
                {
                    ZDeltaByX = (ZRight - ZLeft);
                }

                // Process each X
                for (i32f X = XStart; X < XEnd; ++X)
                {
                    if (Z > ZBufferArray[X])
                    {
                        InterpolationContext.Pixel = 0xFFFFFFFF;
                        InterpolationContext.X = X;
                        InterpolationContext.Z = Z;

                        for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                        {
                            InterpolationContext.Interpolators[InterpIndex]->ProcessPixel();
                        }

                        Buffer[X] = InterpolationContext.Pixel;

                        ZBufferArray[X] = Z;
                    }

                    // Interpolate by X
                    Z += ZDeltaByX;

                    for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                    {
                        InterpolationContext.Interpolators[InterpIndex]->InterpolateX();
                    }
                }

                // Interpolate by Y
                XLeft += XDeltaLeftByY;
                ZLeft += ZDeltaLeftByY;

                XRight += XDeltaRightByY;
                ZRight += ZDeltaRightByY;

                for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                {
                    InterpolationContext.Interpolators[InterpIndex]->InterpolateY();
                }

                Buffer += Pitch;
                ZBufferArray += ZBuffer.Pitch;

                // Test for changing interpolant
                if (InterpolationContext.Y == YRestartInterpolation)
                {
                    if (bRestartInterpolationAtLeftHand)
                    {
                        // Compute new values to get from Y1 to Y2
                        const i32 YDiff = (Y2 - Y1);

                        XDeltaLeftByY = IntToFx16(X2 - X1) / YDiff;
                        ZDeltaLeftByY = (ZVtx2 - ZVtx1) / YDiff;

                        XLeft = IntToFx16(X1);
                        ZLeft = (ZVtx1);

                        // Align down on 1 Y
                        XLeft += XDeltaLeftByY;
                        ZLeft += ZDeltaLeftByY;

                        // Do both for interpolators
                        for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                        {
                            InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltasLeft(YDiff, V1, V2);
                            InterpolationContext.Interpolators[InterpIndex]->InterpolateYLeft();
                        }
                    }
                    else
                    {
                        // Compute new values to get from Y2 to Y1 because we swapped them
                        const i32 YDiff = (Y1 - Y2);

                        XDeltaRightByY = IntToFx16(X1 - X2) / YDiff;
                        ZDeltaRightByY = (ZVtx1 - ZVtx2) / YDiff;

                        XRight = IntToFx16(X2);
                        ZRight = (ZVtx2);

                        // Align down on 1 Y
                        XRight += XDeltaRightByY;
                        ZRight += ZDeltaRightByY;

                        // Do both for interpolators
                        for (i32f InterpIndex = 0; InterpIndex < InterpolationContext.NumInterpolators; ++InterpIndex)
                        {
                            InterpolationContext.Interpolators[InterpIndex]->ComputeYStartsAndDeltasRight(YDiff, V2, V1);
                            InterpolationContext.Interpolators[InterpIndex]->InterpolateYRight();
                        }
                    }
                }
            }
        }
    }
}

void VRenderer::VarDrawText(i32 X, i32 Y, VColorARGB Color, const char* Format, std::va_list VarList)
{
    // Prepare text
    VTextElement TextElement;
    std::vsnprintf(TextElement.Text, VTextElement::TextSize, Format, VarList);

    // Convert color
    TextElement.Color.a = Color.A;
    TextElement.Color.r = Color.R;
    TextElement.Color.g = Color.G;
    TextElement.Color.b = Color.B;

    // Set position
    TextElement.Position = { X, Y };

    TextQueue.EmplaceBack(std::move(TextElement));
}

void VRenderer::DrawText(i32 X, i32 Y, VColorARGB Color, const char* Format, ...)
{
    std::va_list VarList;
    va_start(VarList, Format);

    VarDrawText(X, Y, Color, Format, VarList);

    va_end(VarList);
}

void VRenderer::DrawDebugText(const char* Format, ...)
{
    std::va_list VarList;
    va_start(VarList, Format);

    VarDrawText(0, DebugTextY, { 0xFF, 0xFF, 0xFF, 0xFF }, Format, VarList);
    DebugTextY += FontCharHeight;

    va_end(VarList);
}

void VRenderer::SetInterpolators()
{
    InterpolationContext.NumInterpolators = 0;

    if (InterpolationContext.PolyAttr & EPolyAttr::ShadeModeGouraud ||
        InterpolationContext.PolyAttr & EPolyAttr::ShadeModePhong)
    {
        InterpolationContext.Interpolators[InterpolationContext.NumInterpolators] = &InterpolationContext.GouraudInterpolator;
    }
    else
    {
        InterpolationContext.Interpolators[InterpolationContext.NumInterpolators] = &InterpolationContext.FlatInterpolator;
    }
    ++InterpolationContext.NumInterpolators;

    if (InterpolationContext.PolyAttr & EPolyAttr::ShadeModeTexture)
    {
        const i32 MaxMipMaps = Config.RenderSpec.MaxMipMaps;

        if (MaxMipMaps > 0)
        {
            f32 Distance = InterpolationContext.Distance;

            InterpolationContext.MipMappingLevel = (i32)(
                Distance / (World.Camera->ZFarClip / (f32)MaxMipMaps)
            );

            if (Distance < 10000.0f)
            {
                InterpolationContext.Interpolators[InterpolationContext.NumInterpolators] = &InterpolationContext.BillinearPerspectiveTextureInterpolator;
            }
            else if (Distance < 15000.0f)
            {
                InterpolationContext.Interpolators[InterpolationContext.NumInterpolators] = &InterpolationContext.PerspectiveCorrectTextureInterpolator;
            }
            else if (Distance < 50000.0f)
            {
                InterpolationContext.Interpolators[InterpolationContext.NumInterpolators] = &InterpolationContext.LinearPiecewiseTextureInterpolator;
            }
            else
            {
                InterpolationContext.Interpolators[InterpolationContext.NumInterpolators] = &InterpolationContext.AffineTextureInterpolator;
            }
        }
        else
        {
            InterpolationContext.MipMappingLevel = 0;
            InterpolationContext.Interpolators[InterpolationContext.NumInterpolators] = &InterpolationContext.BillinearPerspectiveTextureInterpolator;
        }

        ++InterpolationContext.NumInterpolators;
    }

    InterpolationContext.Interpolators[InterpolationContext.NumInterpolators] = &InterpolationContext.AlphaInterpolator;
    ++InterpolationContext.NumInterpolators;
}

void VRenderer::RenderSolid()
{
    for (i32f I = 0; I < RenderList->NumPoly; ++I)
    {
        VPolyFace* Poly = RenderList->PolyPtrList[I];
        if (!Poly ||
            ~Poly->State & EPolyState::Active ||
            Poly->State & EPolyState::BackFace ||
            Poly->State & EPolyState::Clipped)
        {
            continue;
        }

        InterpolationContext.Vtx = Poly->TransVtx;
        InterpolationContext.Material = Poly->Material;

        InterpolationContext.OriginalColor = Poly->OriginalColor;
        InterpolationContext.LitColor[0] = Poly->LitColor[0];
        InterpolationContext.LitColor[1] = Poly->LitColor[1];
        InterpolationContext.LitColor[2] = Poly->LitColor[2];

        InterpolationContext.PolyAttr = Poly->Attr;

        InterpolationContext.Distance = Poly->TransVtx[0].Z;

        Renderer.DrawTriangle(InterpolationContext);
    }
}

void VRenderer::RenderWire()
{
    for (i32f I = 0; I < RenderList->NumPoly; ++I)
    {
        const VPolyFace* Poly = RenderList->PolyPtrList[I];
        if (!Poly ||
            ~Poly->State & EPolyState::Active ||
            Poly->State & EPolyState::BackFace ||
            Poly->State & EPolyState::Clipped)
        {
            continue;
        }

        Renderer.DrawClippedLine(
            InterpolationContext.Buffer, InterpolationContext.BufferPitch,
            (i32)Poly->TransVtx[0].X, (i32)Poly->TransVtx[0].Y,
            (i32)Poly->TransVtx[1].X, (i32)Poly->TransVtx[1].Y,
            Poly->LitColor[0]
        );
        Renderer.DrawClippedLine(
            InterpolationContext.Buffer, InterpolationContext.BufferPitch,
            (i32)Poly->TransVtx[1].X, (i32)Poly->TransVtx[1].Y,
            (i32)Poly->TransVtx[2].X, (i32)Poly->TransVtx[2].Y,
            Poly->LitColor[1]
        );
        Renderer.DrawClippedLine(
            InterpolationContext.Buffer, InterpolationContext.BufferPitch,
            (i32)Poly->TransVtx[2].X, (i32)Poly->TransVtx[2].Y,
            (i32)Poly->TransVtx[0].X, (i32)Poly->TransVtx[0].Y,
            Poly->LitColor[2]
        );
    }
}


}