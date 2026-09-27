// Silahlar: Sans'in Gaster Blaster'i ve Undyne'in mizragi. Oyun ekranina tiklanan yere ateslenir; isabet eden her
// hedef (dusmanlar, savasta kalp, haritada Frisk) 1 hasar alir.
#pragma once

namespace weapons {

enum Kind { OFF = 0, BLASTER = 1, SPEAR = 2 };

// Tiklanan nokta, oyunun 640x480 goruntusu icinde (pencere olceginden bagimsiz). Ana thread'den cagrilir.
void queueShot(int kind, float portX, float portY);

// Her oyun karesinde ana thread'den (oyun hazirken) cagrilir.
void tick(int objectIndexVar);

// Auto-dodge bu instance'lari mermi saymasin (hedef kalp olabilir).
bool isOurs(void* inst);

// Sans'a isabet etti: oyunun kendi olum sahnesi baslatilmali (tek vurus kodundaki yol). Okuyunca sifirlanir.
bool takeSansKill();

// Bu dusmana silah vurdu ve oyunun kendi hasar akisi suruyor: tek vurus hilesi hasari 999999999'a cevirmesin.
bool hitInProgress(int monster);
bool anyHitInProgress();

} // namespace weapons
