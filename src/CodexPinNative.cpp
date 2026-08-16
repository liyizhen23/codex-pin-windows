#define UNICODE
#define _UNICODE
#include <windows.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {

constexpr wchar_t kWindowClass[] = L"CodexPin.NativeButton";
constexpr wchar_t kMutexName[] = L"Local\\CodexPin.Native.SingleInstance";
constexpr int kHotkeyId = 0xC0DE;
constexpr UINT kExitMenuId = 1;
constexpr UINT kRenderMessage = WM_APP + 42;

HWND g_button = nullptr;
HWND g_target = nullptr;
bool g_hovered = false;
bool g_mutatingTarget = false;
UINT g_dpi = 96;
RECT g_buttonRect{};
COLORREF g_sampledBackground = RGB(246, 246, 246);
ULONG_PTR g_gdiplusToken = 0;
std::vector<HWINEVENTHOOK> g_eventHooks;

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}

bool IsCodexWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
        return false;
    }
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return false;
    }

    wchar_t className[128]{};
    GetClassNameW(hwnd, className, ARRAYSIZE(className));
    if (wcscmp(className, L"Chrome_WidgetWin_1") != 0) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (!processId) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return false;
    }

    wchar_t path[32768]{};
    DWORD pathLength = ARRAYSIZE(path);
    const bool queried = QueryFullProcessImageNameW(process, 0, path, &pathLength) != FALSE;
    CloseHandle(process);
    if (!queried) {
        return false;
    }

    const std::wstring normalized = Lowercase(std::wstring(path, pathLength));
    return normalized.find(L"\\windowsapps\\openai.codex_") != std::wstring::npos &&
           normalized.ends_with(L"\\chatgpt.exe");
}

struct FindState {
    HWND best = nullptr;
    long long bestArea = 0;
};

BOOL CALLBACK FindCodexWindowProc(HWND hwnd, LPARAM param) {
    auto* state = reinterpret_cast<FindState*>(param);
    if (!IsCodexWindow(hwnd)) {
        return TRUE;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return TRUE;
    }
    const long long width = max(0L, rect.right - rect.left);
    const long long height = max(0L, rect.bottom - rect.top);
    const long long area = width * height;
    if (area > state->bestArea) {
        state->best = hwnd;
        state->bestArea = area;
    }
    return TRUE;
}

HWND FindCodexWindow() {
    const HWND foreground = GetForegroundWindow();
    if (IsCodexWindow(foreground)) {
        return foreground;
    }

    FindState state;
    EnumWindows(FindCodexWindowProc, reinterpret_cast<LPARAM>(&state));
    return state.best;
}

bool IsPinned() {
    return g_target && IsWindow(g_target) &&
           (GetWindowLongPtrW(g_target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

bool GetTargetBounds(RECT& rect) {
    if (!g_target || !IsWindow(g_target)) {
        return false;
    }
    if (SUCCEEDED(DwmGetWindowAttribute(
            g_target, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        return true;
    }
    return GetWindowRect(g_target, &rect) != FALSE;
}

COLORREF FallbackBackgroundColor() {
    BOOL dark = FALSE;
    constexpr DWORD kDwmUseImmersiveDarkMode = 20;
    if (g_target && SUCCEEDED(DwmGetWindowAttribute(
            g_target, kDwmUseImmersiveDarkMode, &dark, sizeof(dark))) && dark) {
        return RGB(32, 32, 32);
    }
    return RGB(246, 246, 246);
}

void SampleTitlebarBackground() {
    const int sampleX = g_buttonRect.left - max(4, MulDiv(6, static_cast<int>(g_dpi), 96));
    const int sampleY = g_buttonRect.top + (g_buttonRect.bottom - g_buttonRect.top) / 2;
    HDC screen = GetDC(nullptr);
    if (!screen) {
        g_sampledBackground = FallbackBackgroundColor();
        return;
    }
    const COLORREF pixel = GetPixel(screen, sampleX, sampleY);
    ReleaseDC(nullptr, screen);
    g_sampledBackground = pixel == CLR_INVALID ? FallbackBackgroundColor() : pixel;
}

void AddRoundedRectanglePath(GraphicsPath& path, float width, float height, float radius) {
    const float diameter = radius * 2.0f;
    path.AddArc(0.5f, 0.5f, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(width - diameter - 0.5f, 0.5f, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(width - diameter - 0.5f, height - diameter - 0.5f, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(0.5f, height - diameter - 0.5f, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void DrawPin(Graphics& graphics, float width, float height, const Color& color, float scale) {
    Pen pen(color, max(1.45f, 1.55f * scale));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    const GraphicsState state = graphics.Save();
    graphics.TranslateTransform(width / 2.0f, height / 2.0f - 0.3f * scale);
    graphics.RotateTransform(-35.0f);
    graphics.DrawLine(&pen, 0.0f, -7.0f * scale, 0.0f, 6.2f * scale);
    graphics.DrawLine(&pen, -4.4f * scale, -4.7f * scale, 4.4f * scale, -4.7f * scale);
    graphics.DrawLine(&pen, -3.4f * scale, -4.6f * scale, -2.7f * scale, 1.5f * scale);
    graphics.DrawLine(&pen, 3.4f * scale, -4.6f * scale, 2.7f * scale, 1.5f * scale);
    graphics.DrawLine(&pen, -4.5f * scale, 1.6f * scale, 4.5f * scale, 1.6f * scale);
    graphics.DrawLine(&pen, 0.0f, 6.0f * scale, 0.0f, 9.4f * scale);
    graphics.Restore(state);
}

void RenderButton() {
    if (!g_button || !IsWindow(g_button)) {
        return;
    }

    const int width = max(1L, g_buttonRect.right - g_buttonRect.left);
    const int height = max(1L, g_buttonRect.bottom - g_buttonRect.top);
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    ZeroMemory(pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

    Graphics graphics(memory);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    const float scale = static_cast<float>(g_dpi) / 96.0f;
    const BYTE backgroundRed = GetRValue(g_sampledBackground);
    const BYTE backgroundGreen = GetGValue(g_sampledBackground);
    const BYTE backgroundBlue = GetBValue(g_sampledBackground);
    const double luminance =
        (0.2126 * backgroundRed + 0.7152 * backgroundGreen + 0.0722 * backgroundBlue) / 255.0;
    const BYTE foreground = luminance > 0.56 ? 28 : 238;
    const bool pinned = IsPinned();

    GraphicsPath rounded;
    AddRoundedRectanglePath(
        rounded, static_cast<float>(width), static_cast<float>(height), 6.0f * scale);
    SolidBrush hitArea(Color(1, foreground, foreground, foreground));
    graphics.FillPath(&hitArea, &rounded);

    const BYTE surfaceAlpha = pinned ? (g_hovered ? 43 : 30) : (g_hovered ? 22 : 1);
    SolidBrush surface(Color(surfaceAlpha, foreground, foreground, foreground));
    graphics.FillPath(&surface, &rounded);

    const BYTE iconAlpha = pinned ? 248 : (g_hovered ? 220 : 178);
    DrawPin(
        graphics,
        static_cast<float>(width),
        static_cast<float>(height),
        Color(iconAlpha, foreground, foreground, foreground),
        scale);

    POINT destination{g_buttonRect.left, g_buttonRect.top};
    SIZE size{width, height};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(
        g_button, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

void HideButton() {
    if (g_button && IsWindow(g_button)) {
        ShowWindow(g_button, SW_HIDE);
    }
}

void PositionButton() {
    if (!g_target || !IsWindow(g_target) || !IsWindowVisible(g_target) || IsIconic(g_target)) {
        HideButton();
        return;
    }

    RECT targetBounds{};
    if (!GetTargetBounds(targetBounds)) {
        HideButton();
        return;
    }

    g_dpi = GetDpiForWindow(g_target);
    if (!g_dpi) {
        g_dpi = 96;
    }
    const int width = MulDiv(34, static_cast<int>(g_dpi), 96);
    const int height = MulDiv(30, static_cast<int>(g_dpi), 96);
    const int nativeCaptionWidth = GetSystemMetricsForDpi(SM_CXSIZE, g_dpi);
    const int captionWidth = max(nativeCaptionWidth, MulDiv(46, static_cast<int>(g_dpi), 96));
    const int rightMargin = MulDiv(8, static_cast<int>(g_dpi), 96);
    const int topMargin = MulDiv(5, static_cast<int>(g_dpi), 96);
    const int x = targetBounds.right - (3 * captionWidth) - width - rightMargin;
    const int y = targetBounds.top + topMargin;
    g_buttonRect = {x, y, x + width, y + height};

    SampleTitlebarBackground();
    RenderButton();
    SetWindowPos(
        g_button,
        IsPinned() ? HWND_TOPMOST : HWND_NOTOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void UnbindTarget() {
    HideButton();
    if (g_button && IsWindow(g_button)) {
        SetWindowLongPtrW(g_button, GWLP_HWNDPARENT, 0);
    }
    g_target = nullptr;
    g_hovered = false;
}

void BindTarget(HWND hwnd) {
    if (!IsCodexWindow(hwnd)) {
        return;
    }
    if (g_target != hwnd) {
        UnbindTarget();
        g_target = hwnd;
        SetWindowLongPtrW(g_button, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(g_target));
    }
    PositionButton();
}

void TogglePin() {
    if (!g_target || !IsWindow(g_target)) {
        const HWND found = FindCodexWindow();
        if (found) {
            BindTarget(found);
        }
    }
    if (!g_target || !IsWindow(g_target)) {
        return;
    }

    const bool shouldPin = !IsPinned();
    g_mutatingTarget = true;
    SetWindowPos(
        g_target,
        shouldPin ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_mutatingTarget = false;
    SetWindowPos(
        g_button,
        shouldPin ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    PostMessageW(g_button, kRenderMessage, 0, 0);
}

void CALLBACK WinEventCallback(
    HWINEVENTHOOK,
    DWORD event,
    HWND hwnd,
    LONG objectId,
    LONG,
    DWORD,
    DWORD) {
    if (!hwnd || hwnd == g_button) {
        return;
    }

    if (g_mutatingTarget) {
        return;
    }

    if (event == EVENT_SYSTEM_FOREGROUND) {
        const HWND root = GetAncestor(hwnd, GA_ROOT);
        if (g_target && root == g_target) {
            PositionButton();
        } else if (!g_target && IsCodexWindow(root)) {
            BindTarget(root);
        }
        return;
    }

    if (objectId != OBJID_WINDOW) {
        return;
    }

    const HWND root = GetAncestor(hwnd, GA_ROOT);
    if (g_target && (hwnd == g_target || root == g_target)) {
        if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE ||
            event == EVENT_SYSTEM_MINIMIZESTART) {
            UnbindTarget();
        } else if (event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_LOCATIONCHANGE ||
                   event == EVENT_SYSTEM_MINIMIZEEND) {
            PositionButton();
        }
        return;
    }

    if (!g_target && (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW ||
                      event == EVENT_SYSTEM_MINIMIZEEND)) {
        if (IsCodexWindow(root)) {
            BindTarget(root);
        }
    }
}

void AddEventHook(DWORD minEvent, DWORD maxEvent) {
    HWINEVENTHOOK hook = SetWinEventHook(
        minEvent,
        maxEvent,
        nullptr,
        WinEventCallback,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (hook) {
        g_eventHooks.push_back(hook);
    }
}

void ShowExitMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    AppendMenuW(menu, MF_STRING, kExitMenuId, L"退出 Codex 置顶按钮");
    POINT point{};
    GetCursorPos(&point);
    const UINT selected = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        point.x,
        point.y,
        hwnd,
        nullptr);
    DestroyMenu(menu);
    if (selected == kExitMenuId) {
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_MOUSEMOVE: {
            if (!g_hovered) {
                g_hovered = true;
                TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tracking);
                RenderButton();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_hovered = false;
            RenderButton();
            return 0;
        case WM_LBUTTONUP:
            TogglePin();
            return 0;
        case WM_RBUTTONUP:
            ShowExitMenu(hwnd);
            return 0;
        case WM_HOTKEY:
            if (wParam == kHotkeyId) {
                TogglePin();
                return 0;
            }
            break;
        case kRenderMessage:
            RenderButton();
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
        case WM_DPICHANGED:
            PositionButton();
            return 0;
        case WM_DESTROY:
            UnregisterHotKey(hwnd, kHotkeyId);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) {
            CloseHandle(mutex);
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        CloseHandle(mutex);
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    if (!RegisterClassExW(&windowClass)) {
        GdiplusShutdown(g_gdiplusToken);
        CloseHandle(mutex);
        return 1;
    }

    g_button = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kWindowClass,
        L"Codex Pin",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!g_button) {
        GdiplusShutdown(g_gdiplusToken);
        CloseHandle(mutex);
        return 1;
    }

    RegisterHotKey(g_button, kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P');
    AddEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND);
    AddEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND);
    AddEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE);
    AddEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE);

    if (const HWND existing = FindCodexWindow()) {
        BindTarget(existing);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    for (const HWINEVENTHOOK hook : g_eventHooks) {
        UnhookWinEvent(hook);
    }
    g_eventHooks.clear();
    GdiplusShutdown(g_gdiplusToken);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
