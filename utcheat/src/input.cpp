#include "input.h"

#include "MinHook.h"

namespace vinput {
namespace {

HWND gWnd = nullptr;
volatile bool gDown[256];

using GAKS_t = SHORT(WINAPI*)(int);
GAKS_t oGetAsyncKeyState, oGetKeyState;

SHORT WINAPI hkGetAsyncKeyState(int vk) {
    SHORT r = oGetAsyncKeyState(vk);
    if (vk >= 0 && vk < 256 && gDown[vk]) r |= (SHORT)0x8000;
    return r;
}

SHORT WINAPI hkGetKeyState(int vk) {
    SHORT r = oGetKeyState(vk);
    if (vk >= 0 && vk < 256 && gDown[vk]) r |= (SHORT)0x8000;
    return r;
}

LPARAM keyLParam(int vk, bool up) {
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM l = 1 | (scan << 16);
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT) l |= 1 << 24;  // genisletilmis tus
    if (up) l |= (1u << 30) | (1u << 31);
    return l;
}

} // namespace

bool installHooks() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) u32 = LoadLibraryW(L"user32.dll");
    void* a = (void*)GetProcAddress(u32, "GetAsyncKeyState");
    void* k = (void*)GetProcAddress(u32, "GetKeyState");
    bool ok = MH_CreateHook(a, (void*)hkGetAsyncKeyState, (void**)&oGetAsyncKeyState) == MH_OK && MH_EnableHook(a) == MH_OK;
    ok = ok && MH_CreateHook(k, (void*)hkGetKeyState, (void**)&oGetKeyState) == MH_OK && MH_EnableHook(k) == MH_OK;
    return ok;
}

void setWindow(HWND wnd) { gWnd = wnd; }

void set(int vk, bool down) {
    if (vk <= 0 || vk >= 256 || gDown[vk] == down) return;
    gDown[vk] = down;
    if (gWnd) PostMessageW(gWnd, down ? WM_KEYDOWN : WM_KEYUP, vk, keyLParam(vk, !down));
}

void releaseAll() {
    for (int vk = 0; vk < 256; ++vk)
        if (gDown[vk]) set(vk, false);
}

bool anyDown() {
    for (int vk = 0; vk < 256; ++vk)
        if (gDown[vk]) return true;
    return false;
}

} // namespace vinput
