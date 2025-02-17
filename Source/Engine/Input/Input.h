#pragma once

#include "SDL.h"
#include "Common/Platform/Platform.h"
#include "Common/Types/Common.h"
#include "Common/Math/Minimal.h"
#include "Engine/Core/Events/Event.h"

namespace Volition
{

enum EKeycode
{
    Unknown = SDLK_UNKNOWN,
    Return = SDLK_RETURN,
    Escape = SDLK_ESCAPE,
    Backspace = SDLK_BACKSPACE,
    Tab = SDLK_TAB,
    Space = SDLK_SPACE,
    Exclaim = SDLK_EXCLAIM,
    DoubleQuote = SDLK_QUOTEDBL,
    Hash = SDLK_HASH,
    Percent = SDLK_PERCENT,
    Dollar = SDLK_DOLLAR,
    Ampersand = SDLK_AMPERSAND,
    Quote = SDLK_QUOTE,
    LeftParen = SDLK_LEFTPAREN,
    RightParen = SDLK_RIGHTPAREN,
    Asterisk = SDLK_ASTERISK,
    Plus = SDLK_PLUS,
    Comma = SDLK_COMMA,
    Minus = SDLK_MINUS,
    Period = SDLK_PERIOD,
    Slash = SDLK_SLASH,
    N0 = SDLK_0,
    N1 = SDLK_1,
    N2 = SDLK_2,
    N3 = SDLK_3,
    N4 = SDLK_4,
    N5 = SDLK_5,
    N6 = SDLK_6,
    N7 = SDLK_7,
    N8 = SDLK_8,
    N9 = SDLK_9,
    Colon = SDLK_COLON,
    Semicolon = SDLK_SEMICOLON,
    Less = SDLK_LESS,
    Equals = SDLK_EQUALS,
    Greater = SDLK_GREATER,
    Question = SDLK_QUESTION,
    At = SDLK_AT,
    LeftBracket = SDLK_LEFTBRACKET,
    Backslash = SDLK_BACKSLASH,
    RightBracket = SDLK_RIGHTBRACKET,
    Caret = SDLK_CARET,
    Underscore = SDLK_UNDERSCORE,
    Backquote = SDLK_BACKQUOTE,
    A = SDLK_a,
    B = SDLK_b,
    C = SDLK_c,
    D = SDLK_d,
    E = SDLK_e,
    F = SDLK_f,
    G = SDLK_g,
    H = SDLK_h,
    I = SDLK_i,
    J = SDLK_j,
    K = SDLK_k,
    L = SDLK_l,
    M = SDLK_m,
    N = SDLK_n,
    O = SDLK_o,
    P = SDLK_p,
    Q = SDLK_q,
    R = SDLK_r,
    S = SDLK_s,
    T = SDLK_t,
    U = SDLK_u,
    V = SDLK_v,
    W = SDLK_w,
    X = SDLK_x,
    Y = SDLK_y,
    Z = SDLK_z,
    Capslock = SDLK_CAPSLOCK,
    F1 = SDLK_F1,
    F2 = SDLK_F2,
    F3 = SDLK_F3,
    F4 = SDLK_F4,
    F5 = SDLK_F5,
    F6 = SDLK_F6,
    F7 = SDLK_F7,
    F8 = SDLK_F8,
    F9 = SDLK_F9,
    F10 = SDLK_F10,
    F11 = SDLK_F11,
    F12 = SDLK_F12,
    PrintScreen = SDLK_PRINTSCREEN,
    ScrollLock = SDLK_SCROLLLOCK,
    Pause = SDLK_PAUSE,
    Insert = SDLK_INSERT,
    Home = SDLK_HOME,
    PageUp = SDLK_PAGEUP,
    Delete = SDLK_DELETE,
    End = SDLK_END,
    PageDown = SDLK_PAGEDOWN,
    Right = SDLK_RIGHT,
    Left = SDLK_LEFT,
    Down = SDLK_DOWN,
    Up = SDLK_UP,
    LCtrl = SDLK_LCTRL,
    LShift = SDLK_LSHIFT,
    LAlt = SDLK_LALT,
    RCtrl = SDLK_RCTRL,
    RShift = SDLK_RSHIFT,
    RAlt = SDLK_RALT,
};

enum class EMouseButton
{
    Left   = SDL_BUTTON_LMASK,
    Middle = SDL_BUTTON_MMASK,
    Right  = SDL_BUTTON_RMASK
};

class VInput
{
private:
    static constexpr i32f InitialKeyDownEventsCapacity = 16;

private:
    const VEvent* MouseMoveEvent = nullptr;
    u32 MouseState = 0;

    VVector2i MouseAbsolutePosition = { 0, 0 };
    VVector2i MouseRelativePosition = { 0, 0 };

    TArray<const VEvent*> KeyDownEvents;
    const u8* KeyState;

public:
    void StartUp();
    void ShutDown();

    void ProcessEvents();

    b32 IsKeyDown(EKeycode Key) const;
    b32 IsEventKeyDown(EKeycode Key) const;

    /** Checks actual state, not events */
    b32 IsMouseDown(EMouseButton Button) const;

    const VVector2i& GetMouseAbsolutePosition() const;
    const VVector2i& GetMouseRelativePosition() const;
};

inline VInput Input;

VLN_FINLINE b32 VInput::IsMouseDown(EMouseButton Button) const
{
    return MouseState & (u32)Button;
}

VLN_FINLINE const VVector2i& VInput::GetMouseAbsolutePosition() const
{
    return MouseAbsolutePosition;
}

VLN_FINLINE const VVector2i& VInput::GetMouseRelativePosition() const
{
    return MouseRelativePosition;
}

}
