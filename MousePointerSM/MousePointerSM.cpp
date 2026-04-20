#include <windows.h>
#include <string>
#include <cmath>
#include <sstream>
#include <iomanip>

#include "SoundDriver.h"
static SoundDriver g_sound;

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

// Globais WinAPI
static HWND g_btnDebug = nullptr;
static bool g_showDebug = false;
static HWND g_btnMute = nullptr;
static HWND g_sliderAttack = nullptr;
static HWND g_sliderDecay = nullptr;
static HWND g_sliderSpeed = nullptr;
static HWND g_sliderLfo = nullptr;
static HWND g_lblAttack = nullptr;
static HWND g_lblDecay = nullptr;
static HWND g_lblSpeed = nullptr;
static HWND g_lblLfo = nullptr;
static HWND g_sliderVolume = nullptr;
static HWND g_lblVolume = nullptr;
static HWND g_btnLoadSample = nullptr;

float g_attackSpeed = 0.050f;
float g_decaySpeed = 0.030f;
float g_sampleSpeed = 1.0f;

// ─── Estado do mouse ──────────────────────────────────────────────────────────
struct MouseState {
    int   x = 0;
    int   y = 0;
    bool  pressed = false;
    float velocity = 0.0f;
};

static MouseState g_mouse;
static DWORD      g_lastTime = 0;
static HWND       g_hwnd = nullptr;
static HHOOK      g_hook = nullptr; 

float calcVelocity(int x, int y, int prevX, int prevY, DWORD now) {
    DWORD elapsed = now - g_lastTime;
    if (elapsed == 0) return g_mouse.velocity;

    float dx = static_cast<float>(x - prevX);
    float dy = static_cast<float>(y - prevY);
    float dist = std::sqrt(dx * dx + dy * dy);
    float rawVel = dist / (elapsed / 1000.0f);

    const float alpha = 0.2f;
    return alpha * rawVel + (1.0f - alpha) * g_mouse.velocity;
}

// Hook global do mouse.
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        int   newX = info->pt.x;
        int   newY = info->pt.y;
        DWORD now = info->time;

        if (wParam == WM_MOUSEMOVE) {
            g_mouse.velocity = calcVelocity(newX, newY, g_mouse.x, g_mouse.y, now);
            g_sound.setVelocity(g_mouse.velocity, g_mouse.pressed);
            g_mouse.x = newX;
            g_mouse.y = newY;
            g_lastTime = now;
        }
        else if (wParam == WM_LBUTTONDOWN) {
            g_mouse.pressed = true;
            g_sound.setVelocity(g_mouse.velocity, g_mouse.pressed);
        }
		else if (wParam == WM_RBUTTONDOWN) {
            g_mouse.pressed = true;
            g_sound.setVelocity(g_mouse.velocity, g_mouse.pressed);
        }
        else if (wParam == WM_LBUTTONUP) {
            g_mouse.pressed = false;
            g_mouse.velocity = 0.0f;
            g_sound.setVelocity(g_mouse.velocity, g_mouse.pressed);
        }
        else if (wParam == WM_RBUTTONUP) {
            g_mouse.pressed = false;
            g_mouse.velocity = 0.0f;
            g_sound.setVelocity(g_mouse.velocity, g_mouse.pressed);
		}
    }

    // PASSE O HOOK ADIANTE!! Ou ele fica "instalado" no sistema.
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// WinProc
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdcScreen = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        // Double buffering just in case.
        HDC     hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
        HBITMAP hOldBmp = static_cast<HBITMAP>(SelectObject(hdcMem, hBmp));
        FillRect(hdcMem, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        BitBlt(hdcScreen, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);

        break;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == 1) {
            g_showDebug = !g_showDebug;
            SetWindowTextA(g_btnDebug, g_showDebug ? "Debug: ON" : "Debug: OFF");
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        else if (LOWORD(wParam) == 2) {
            bool nowMuted = !g_sound.isMuted();
            g_sound.setMuted(nowMuted);
            SetWindowTextA(g_btnMute, nowMuted ? "Som: OFF" : "Som: ON");
        }
        else if (LOWORD(wParam) == 3) {
            char filePath[MAX_PATH] = {};

            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Arquivos WAV\0*.wav\0Todos os arquivos\0*.*\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = "Selecione um sample .wav";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameA(&ofn)) {
                if (g_sound.loadSample(filePath)) {
                    g_sound.useSample(true);
                    SetWindowTextA(g_btnLoadSample, "Sample: OK");
                }
                else {
                    SetWindowTextA(g_btnLoadSample, "Formato invalido");
                    MessageBoxA(hwnd,
                        "O arquivo deve ser .wav ou .riff, mono, e em 44100hz 16 bits.\n"
                        "Converta com Audacity se necessario.",
                        "Formato incompativel", MB_OK | MB_ICONWARNING);
                }
            }
        }
        break;
    }

    case WM_HSCROLL: {
        HWND src = reinterpret_cast<HWND>(lParam);
        auto pos = static_cast<int>(SendMessage(src, TBM_GETPOS, 0, 0));

        char buf[64];

        if (src == g_sliderAttack) {
            g_attackSpeed = pos / 1000.0f;
            g_sound.attackSpeed = g_attackSpeed;
            sprintf_s(buf, "Attack: %.3f", g_attackSpeed);
            SetWindowTextA(g_lblAttack, buf);
        }
        else if (src == g_sliderDecay) {
            g_decaySpeed = pos / 1000.0f;
            g_sound.decaySpeed = g_decaySpeed;
            sprintf_s(buf, "Decay:  %.3f", g_decaySpeed);
            SetWindowTextA(g_lblDecay, buf);
        }
        else if (src == g_sliderSpeed) {
            g_sampleSpeed = pos / 100.0f;
            g_sound.sampleSpeed = g_sampleSpeed;
            sprintf_s(buf, "Speed:  %.2f", g_sampleSpeed);
            SetWindowTextA(g_lblSpeed, buf);
        }
        else if (src == g_sliderVolume) {
            float vol = pos / 100.0f;
            g_sound.volume = vol;
            sprintf_s(buf, "Volume: %.2f", vol);
            SetWindowTextA(g_lblVolume, buf);
        }
        break;
    }

    case WM_TIMER: {
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    }

    case WM_DESTROY:
        // Remove o hook antes de fechar (just in case windows don't do it).
        KillTimer(hwnd, 1);
        if (g_hook) {
            UnhookWindowsHookEx(g_hook);
            g_hook = nullptr;
        }
        g_sound.close();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const char* CLASS_NAME = "PencilSoundWindow";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);

    SetTimer(g_hwnd, 1, 33, nullptr);

    g_hwnd = CreateWindowExA(
        0, CLASS_NAME, "MousePointerSM",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 380, 320,
        nullptr, nullptr, hInstance, nullptr
    );

    /* 
    Remova essa linha para ativar o botão, mas ele não esta funcional.
	É preciso implementat o cout de debug e atualizar o texto do botão de acordo.
    g_btnDebug = CreateWindowExA(
        0, "BUTTON", "Debug: OFF",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 10, 100, 28,        // x, y, largura, altura
        g_hwnd,
        reinterpret_cast<HMENU>(1), // ID do botão — usado no WM_COMMAND
        hInstance, nullptr
    );
    */

    g_btnMute = CreateWindowExA(
        0, "BUTTON", "Som: OFF",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        150, 10, 100, 28,
        g_hwnd,
        reinterpret_cast<HMENU>(2),
        hInstance, nullptr
    );

    g_btnLoadSample = CreateWindowExA(
        0, "BUTTON", "Carregar Sample",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 10, 130, 28,
        g_hwnd,
        reinterpret_cast<HMENU>(3),
        hInstance, nullptr
    );

    // Inicializa commctrl
    INITCOMMONCONTROLSEX icx = { sizeof(icx), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icx);

    auto makeSlider = [&](const char* name, int x, int y,
        HWND& lblOut, HWND& sliderOut,
        int id, int minVal, int maxVal, int initVal)
        {
            lblOut = CreateWindowExA(0, "STATIC", name,
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                x, y, 200, 18, g_hwnd, nullptr, hInstance, nullptr);

            sliderOut = CreateWindowExA(0, TRACKBAR_CLASSA, "",
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                x, y + 20, 200, 24, g_hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                hInstance, nullptr);

            SendMessage(sliderOut, TBM_SETRANGE, TRUE, MAKELPARAM(minVal, maxVal));
            SendMessage(sliderOut, TBM_SETPOS, TRUE, initVal);
        };

    makeSlider("Attack: 0.050", 10, 50,
        g_lblAttack, g_sliderAttack, 10, 1, 300, 150);

    makeSlider("Decay: 0.030", 10, 100,
        g_lblDecay, g_sliderDecay, 11, 1, 300, 100);

    makeSlider("Speed:  1.00", 10, 150,
        g_lblSpeed, g_sliderSpeed, 12, 10, 200, 50);

    makeSlider("Volume: 5.00", 10, 200,
        g_lblVolume, g_sliderVolume, 14, 0, 1000, 100);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    g_hook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);
    g_sound.open();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}