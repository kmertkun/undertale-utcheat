// Sanal tuslar: auto-dodge'un oyuna gercek oyuncu gibi tus basabilmesi icin.
// Oyun yon tuslarini iki yoldan okur: keyboard_check (pencere mesajlari) ve keyboard_check_direct
// (GetAsyncKeyState). Ikisi de karsilanir: tus degisince pencereye WM_KEYDOWN/UP gonderilir, GetAsyncKeyState /
// GetKeyState kancalari da sanal tusu basili dondurur.
#pragma once
#include <windows.h>

namespace vinput {

bool installHooks();          // MH_Initialize'dan sonra, oyun thread'lerinden bagimsiz cagrilabilir
void setWindow(HWND wnd);     // mesajlarin gonderilecegi oyun penceresi
void set(int vk, bool down);  // degisiklik varsa mesaj gonderir
void releaseAll();
bool anyDown();

} // namespace vinput
