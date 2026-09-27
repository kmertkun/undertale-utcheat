#pragma once

struct ImDrawList;

namespace dodge {

struct Settings {
    bool enabled = true;   // enjekte edilince acik: her savasta kendiliginden calisir
    bool showBoxes = false;
    int margin = 5;      // kalbin etrafinda birakilan guvenlik payi (piksel)
    int lookahead = 3;   // kac kare ilerisi tahmin edilsin
    bool natural = false; // false = Rage (isinlan, no-hit); true = Legit (mermilerin icinden gecmeden, hiz sinirli yuru)
    int speed = 6;       // dogal harekette kare basina en fazla piksel
    bool emergency = true;     // no-hit: dogal hareket yetmezse o kare isinlan
    bool blueJumpOnly = false; // mavi ruhta ucma, sadece tusla yuru/zipla
};

struct Status {
    const char* state = "";
    int hazards = 0;
    int dodges = 0;
};

// Her oyun karesinde ana thread'den cagrilir.
void tick(const Settings& s, Status& st);

// Mermi / kalp / kutu sinirlarini cizer. Oda koordinati -> ekran: p * scale + offset.
void drawDebug(ImDrawList* dl, float scale, float offX, float offY);

} // namespace dodge
