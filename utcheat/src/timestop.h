// Zamani durdurma ("ZA WARUDO"): oyuncu (kalp / Frisk) disindaki her sey donar. Mermiler, dusmanlar, NPC'ler ve
// oyuncunun kendi silahlari havada kalir; zaman akinca kaldiklari yerden devam ederler.
#pragma once

struct ImDrawList;

namespace timestop {

bool install();        // motorun olay dagiticisina kanca (MH_Initialize'dan sonra)
void toggle();         // WndProc / menu: bir sonraki karede uygulanir
void tick(int objectIndexVar);  // ana thread, her kare (oyun hazirken)
bool active();
float seconds();       // durdurulali kac saniye oldu
void drawOverlay(ImDrawList* dl, float w, float h);

} // namespace timestop
