/* psprecomp -- a window to play in. See include/psprecomp/window.h. */

#include "psprecomp/window.h"
#include "psprecomp/hle.h"
#include "psprecomp/overlay.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <GL/gl.h>
#include <stdio.h>

#define W 480
#define H 272

/* PSP button bits (SceCtrlData.Buttons). */
enum {
    B_SELECT = 0x0001, B_START = 0x0008,
    B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040, B_LEFT = 0x0080,
    B_L = 0x0100, B_R = 0x0200,
    B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000, B_CROSS = 0x4000, B_SQUARE = 0x8000
};

static volatile LONG g_closed;
static int g_scale;
static const char *g_title = "psprecomp";
static uint32_t g_px[W * H];

/* Hardware OpenGL presentation state */
static HDC g_gl_dc = NULL;
static HGLRC g_gl_rc = NULL;
static GLuint g_gl_tex = 0;
static int g_gl_ready = 0;
static int g_filter_linear = 0; /* 0 = Nearest (Pixel Art), 1 = Linear (Smooth) */
static int g_initial_fullscreen = 0;

void psp_window_set_initial_fullscreen(int enable) {
    g_initial_fullscreen = enable;
}

static uint32_t keyboard(uint8_t *ax, uint8_t *ay) {
    static const struct { int vk; uint32_t bit; } K[] = {
        /* Arrow keys */
        { VK_UP, B_UP }, { VK_RIGHT, B_RIGHT }, { VK_DOWN, B_DOWN }, { VK_LEFT, B_LEFT },
        /* WASD navigation */
        { 'W', B_UP }, { 'D', B_RIGHT }, { 'S', B_DOWN }, { 'A', B_LEFT },
        /* Primary Action Buttons (Left Hand Z/X/C/V + Space/Esc) */
        { 'Z', B_CROSS }, { VK_SPACE, B_CROSS },
        { 'X', B_CIRCLE }, { VK_ESCAPE, B_CIRCLE },
        { 'C', B_SQUARE },
        { 'V', B_TRIANGLE },
        /* Secondary Action Buttons (Right Hand J/K/U/I) */
        { 'J', B_CROSS },
        { 'K', B_CIRCLE },
        { 'U', B_SQUARE },
        { 'I', B_TRIANGLE },
        /* Triggers */
        { 'Q', B_L }, { VK_LSHIFT, B_L },
        { 'E', B_R }, { 'O', B_R }, { VK_RSHIFT, B_R },
        /* System / Menu */
        { VK_RETURN, B_START },
        { VK_TAB, B_SELECT }, { VK_BACK, B_SELECT },
    };

    uint32_t b = 0;
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) {
        if (GetAsyncKeyState(K[i].vk) & 0x8000) b |= K[i].bit;
    }

    /* Simulate Analog Stick if directional keys pressed */
    if (b & B_UP)    *ay = 0;
    if (b & B_DOWN)  *ay = 255;
    if (b & B_LEFT)  *ax = 0;
    if (b & B_RIGHT) *ax = 255;

    return b;
}

/* Check XInput controllers (ports 0..3) */
static uint32_t pad(uint8_t *ax, uint8_t *ay) {
    static const struct { WORD x; uint32_t bit; } P[] = {
        { XINPUT_GAMEPAD_DPAD_UP, B_UP }, { XINPUT_GAMEPAD_DPAD_RIGHT, B_RIGHT },
        { XINPUT_GAMEPAD_DPAD_DOWN, B_DOWN }, { XINPUT_GAMEPAD_DPAD_LEFT, B_LEFT },
        { XINPUT_GAMEPAD_A, B_CROSS }, { XINPUT_GAMEPAD_B, B_CIRCLE },
        { XINPUT_GAMEPAD_X, B_SQUARE }, { XINPUT_GAMEPAD_Y, B_TRIANGLE },
        { XINPUT_GAMEPAD_LEFT_SHOULDER, B_L }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, B_R },
        { XINPUT_GAMEPAD_START, B_START }, { XINPUT_GAMEPAD_BACK, B_SELECT },
    };

    XINPUT_STATE s;
    DWORD res = ERROR_DEVICE_NOT_CONNECTED;
    for (DWORD i = 0; i < 4; i++) {
        res = XInputGetState(i, &s);
        if (res == ERROR_SUCCESS) break;
    }
    if (res != ERROR_SUCCESS) return 0;

    uint32_t b = 0;
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
        if (s.Gamepad.wButtons & P[i].x) b |= P[i].bit;
    }
    if (s.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) b |= B_L;
    if (s.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) b |= B_R;

    /* The PSP stick is 0..255 with 128 at rest and y growing downwards. */
    const int lx = s.Gamepad.sThumbLX, ly = s.Gamepad.sThumbLY;
    const int deadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    if (lx * lx + ly * ly > deadzone * deadzone) {
        *ax = (uint8_t)((lx + 32768) >> 8);
        *ay = (uint8_t)((32767 - ly) >> 8);

        /* Mirror analog stick to D-Pad for easy menu navigation */
        if (ly > 16000) b |= B_UP;
        if (ly < -16000) b |= B_DOWN;
        if (lx < -16000) b |= B_LEFT;
        if (lx > 16000) b |= B_RIGHT;
    }

    /* Process Right Stick (R3) for Free Camera */
    psp_overlay_process_camera(s.Gamepad.sThumbRX, s.Gamepad.sThumbRY);

    return b;
}

static void toggle_fullscreen(HWND h) {
    static WINDOWPLACEMENT g_wpPrev;
    static int g_wp_inited = 0;
    if (!g_wp_inited) {
        ZeroMemory(&g_wpPrev, sizeof(g_wpPrev));
        g_wpPrev.length = sizeof(g_wpPrev);
        g_wp_inited = 1;
    }
    DWORD dwStyle = GetWindowLongA(h, GWL_STYLE);
    if (dwStyle & WS_OVERLAPPEDWINDOW) {
        MONITORINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (GetWindowPlacement(h, &g_wpPrev) &&
            GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongA(h, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(h, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_overlay.fullscreen = 1;
        }
    } else {
        SetWindowLongA(h, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(h, &g_wpPrev);
        SetWindowPos(h, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_overlay.fullscreen = 0;
    }
}

static void update_title(HWND h) {
    char buf[256];
    if (g_gl_ready) {
        snprintf(buf, sizeof(buf), "%s [OpenGL Hardware - %s | F1: Filter, F11: Fullscreen]",
                 g_title, g_filter_linear ? "Bilinear" : "Nearest Crisp");
    } else {
        snprintf(buf, sizeof(buf), "%s [GDI Fallback]", g_title);
    }
    SetWindowTextA(h, buf);
}

static void paint_gl(HWND h) {
    RECT r;
    GetClientRect(h, &r);
    const int cw = r.right, ch = r.bottom;
    if (cw <= 0 || ch <= 0) return;

    /* Maintain aspect ratio according to g_overlay.aspect_ratio */
    int w = cw, hh = cw * H / W;
    if (g_overlay.aspect_ratio == 0) { /* 16:9 (Native PSP 480:272) */
        if (hh > ch) { hh = ch; w = ch * W / H; }
    } else if (g_overlay.aspect_ratio == 1) { /* 4:3 (Classic CRT) */
        hh = cw * 3 / 4;
        if (hh > ch) { hh = ch; w = ch * 4 / 3; }
    } else { /* Stretched (Full Window) */
        w = cw;
        hh = ch;
    }
    const int x0 = (cw - w) / 2, y0 = (ch - hh) / 2;

    glViewport(0, 0, cw, ch);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, cw, ch, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Update texture filtering based on upscaler setting */
    GLint filter = (g_overlay.upscaler == 0) ? GL_NEAREST : GL_LINEAR;
    glBindTexture(GL_TEXTURE_2D, g_gl_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

    if (psp_display_read(g_px) == 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_gl_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g_px);

        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f((float)x0, (float)y0);
        glTexCoord2f(1.0f, 0.0f); glVertex2f((float)(x0 + w), (float)y0);
        glTexCoord2f(1.0f, 1.0f); glVertex2f((float)(x0 + w), (float)(y0 + hh));
        glTexCoord2f(0.0f, 1.0f); glVertex2f((float)x0, (float)(y0 + hh));
        glEnd();

        glDisable(GL_TEXTURE_2D);
    }

    /* Render In-Game Settings Overlay & OSD on top */
    psp_overlay_render(cw, ch);

    SwapBuffers(g_gl_dc);
}

static void paint_gdi(HWND h, HDC dc) {
    RECT r;
    GetClientRect(h, &r);
    const int cw = r.right, ch = r.bottom;
    int w = cw, hh = cw * H / W;
    if (hh > ch) { hh = ch; w = ch * W / H; }
    const int x0 = (cw - w) / 2, y0 = (ch - hh) / 2;
    PatBlt(dc, 0, 0, cw, y0, BLACKNESS);
    PatBlt(dc, 0, y0 + hh, cw, ch - y0 - hh, BLACKNESS);
    PatBlt(dc, 0, 0, x0, ch, BLACKNESS);
    PatBlt(dc, x0 + w, 0, cw - x0 - w, ch, BLACKNESS);

    if (psp_display_read(g_px) == 0) {
        for (int i = 0; i < W * H; i++) {
            const uint32_t c = g_px[i];
            g_px[i] = (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
        }
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, x0, y0, w, hh, 0, 0, W, H, g_px, &bi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        PatBlt(dc, x0, y0, w, hh, BLACKNESS);
    }
}

static void paint(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    if (g_gl_ready) {
        paint_gl(h);
    } else {
        paint_gdi(h, dc);
    }
    EndPaint(h, &ps);
}

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER: {
        psp_overlay_tick();

        uint8_t ax = 128, ay = 128;
        uint32_t b = pad(&ax, &ay);
        if (GetForegroundWindow() == h) b |= keyboard(&ax, &ay);

        /* Route input to In-Game Settings Overlay when open */
        if (psp_overlay_is_open()) {
            psp_overlay_handle_pad(b);
            psp_ctrl_set(0, 128, 128); /* Freeze game character motion */
        } else {
            psp_ctrl_set(b, ax, ay);
        }

        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_F11 || (wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) {
            toggle_fullscreen(h);
            return 0;
        }
        if (psp_overlay_handle_key((int)wp)) {
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;
    case WM_PAINT:      paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY:
        if (g_gl_rc) {
            if (g_gl_tex) glDeleteTextures(1, &g_gl_tex);
            wglMakeCurrent(NULL, NULL);
            wglDeleteContext(g_gl_rc);
            g_gl_rc = NULL;
        }
        if (g_gl_dc) {
            ReleaseDC(h, g_gl_dc);
            g_gl_dc = NULL;
        }
        PostQuitMessage(0);
        return 0;
    default:            return DefWindowProcA(h, msg, wp, lp);
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static DWORD WINAPI window_main(LPVOID arg) {
    HANDLE ready = (HANDLE)arg;
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "psprecomp_window";
    RegisterClassA(&wc);

    SetProcessDPIAware();
    if (g_scale <= 0) {
        RECT wa;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        g_scale = (int)((wa.bottom - wa.top) * 7 / 10 / H);
        if (g_scale < 1) g_scale = 1;
    }
    RECT r = { 0, 0, W * g_scale, H * g_scale };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND h = CreateWindowA(wc.lpszClassName, g_title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                           NULL, NULL, wc.hInstance, NULL);
    if (!h) { InterlockedExchange(&g_closed, 1); SetEvent(ready); return 1; }

    /* Initialize Hardware-Accelerated OpenGL Context */
    g_gl_dc = GetDC(h);
    PIXELFORMATDESCRIPTOR pfd;
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(g_gl_dc, &pfd);
    if (pf && SetPixelFormat(g_gl_dc, pf, &pfd)) {
        g_gl_rc = wglCreateContext(g_gl_dc);
        if (g_gl_rc && wglMakeCurrent(g_gl_dc, g_gl_rc)) {
            glGenTextures(1, &g_gl_tex);
            glBindTexture(GL_TEXTURE_2D, g_gl_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            g_gl_ready = 1;
            printf("[WINDOW] OpenGL 1.1+ hardware context initialized successfully!\n");
        }
    }

    if (!g_gl_ready) {
        printf("[WINDOW] OpenGL initialization failed, using GDI software fallback.\n");
    }

    psp_overlay_init();
    if (g_initial_fullscreen) {
        toggle_fullscreen(h);
    }
    update_title(h);
    SetTimer(h, 1, 16, NULL);
    SetEvent(ready);

    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    InterlockedExchange(&g_closed, 1);
    return 0;
}

int psp_window_start(const char *title, int scale) {
    if (title) g_title = title;
    g_scale = scale;
    HANDLE ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, window_main, ready, 0, NULL);
    if (!t) { CloseHandle(ready); return -1; }
    WaitForSingleObject(ready, INFINITE);       /* the window exists, or failed */
    CloseHandle(ready);
    CloseHandle(t);
    return g_closed ? -1 : 0;
}

int psp_window_closed(void) { return g_closed != 0; }

#else

int psp_window_start(const char *title, int scale) { (void)title; (void)scale; return -1; }
int psp_window_closed(void) { return 0; }

#endif
