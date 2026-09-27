#include "weapons.h"

#include <windows.h>
#include <cmath>
#include <vector>

#include "gm.h"
#include "log.h"
#include "timestop.h"

namespace weapons {
namespace {

struct Shot { int kind; float px, py; };
struct Proj {
    int id;              // instance id (her kare instanceById ile cozulur; yok olduysa listeden cikar)
    int kind;
    float dx, dy;        // birim ucus / isin yonu
    float scale;         // Blaster buyuklugu
    bool fired = false;  // Blaster: isin bir kez vurdu
    int life = 0;
};
struct Rect { float l, t, r, b; };
struct View { float x, y, w, h, px, py, pw, ph; };
struct Target { bool player; int monster; void* inst; Rect box; };

std::vector<Shot> gQueue;
std::vector<Proj> gProj;
bool gSansKill = false;
int gHitFrames[3] = {};  // dusman basina: oyunun hasar akisi suruyor (kare)
int gVarObj = -1;

int vX, vY, vId, vBL, vBR, vBT, vBB, vDepth, vSpeed, vDir, vAngle, vSprite, vVisible, vImgSpeed, vXScale, vYScale,
    vAlarm, vSprW, vSprH, vSprXO, vSprYO, vViewEn, vViewX, vViewY, vViewW, vViewH, vPortX, vPortY, vPortW, vPortH;

void initVars() {
    static bool done = false;
    if (done) return;
    done = true;
    vX = gm::findBuiltinVar("x"); vY = gm::findBuiltinVar("y"); vId = gm::findBuiltinVar("id");
    vBL = gm::findBuiltinVar("bbox_left"); vBR = gm::findBuiltinVar("bbox_right");
    vBT = gm::findBuiltinVar("bbox_top"); vBB = gm::findBuiltinVar("bbox_bottom");
    vDepth = gm::findBuiltinVar("depth"); vSpeed = gm::findBuiltinVar("speed"); vDir = gm::findBuiltinVar("direction");
    vAngle = gm::findBuiltinVar("image_angle"); vSprite = gm::findBuiltinVar("sprite_index");
    vVisible = gm::findBuiltinVar("visible"); vImgSpeed = gm::findBuiltinVar("image_speed");
    vXScale = gm::findBuiltinVar("image_xscale"); vYScale = gm::findBuiltinVar("image_yscale");
    vAlarm = gm::findBuiltinVar("alarm"); vSprW = gm::findBuiltinVar("sprite_width");
    vSprH = gm::findBuiltinVar("sprite_height"); vSprXO = gm::findBuiltinVar("sprite_xoffset");
    vSprYO = gm::findBuiltinVar("sprite_yoffset");
    vViewEn = gm::findBuiltinVar("view_enabled");
    vViewX = gm::findBuiltinVar("view_xview"); vViewY = gm::findBuiltinVar("view_yview");
    vViewW = gm::findBuiltinVar("view_wview"); vViewH = gm::findBuiltinVar("view_hview");
    vPortX = gm::findBuiltinVar("view_xport"); vPortY = gm::findBuiltinVar("view_yport");
    vPortW = gm::findBuiltinVar("view_wport"); vPortH = gm::findBuiltinVar("view_hport");
}

double rd(void* inst, int var) { double v = 0; gm::readInstance(inst, var, v); return v; }

// Hedef kutusu: gorunen sprite (Frisk'in carpisma maskesi sadece ayaklari kapsar; gogsune atilan mizrak gecip
// gidiyordu). Sprite yoksa carpisma kutusu.
bool bbox(void* inst, Rect& r) {
    const double w = rd(inst, vSprW), h = rd(inst, vSprH);  // olcekli (aynalanmissa eksi)
    if (w != 0 && h != 0) {
        const double x0 = rd(inst, vX) - rd(inst, vSprXO) * rd(inst, vXScale);
        const double y0 = rd(inst, vY) - rd(inst, vSprYO) * rd(inst, vYScale);
        r = { (float)std::fmin(x0, x0 + w), (float)std::fmin(y0, y0 + h),
              (float)std::fmax(x0, x0 + w), (float)std::fmax(y0, y0 + h) };
        return true;
    }
    double l, t, rr, b;
    if (!gm::readInstance(inst, vBL, l) || !gm::readInstance(inst, vBR, rr) ||
        !gm::readInstance(inst, vBT, t) || !gm::readInstance(inst, vBB, b)) return false;
    r = { (float)l, (float)t, (float)rr + 1, (float)b + 1 };
    return r.r > r.l && r.b > r.t;
}

// Oda koordinati <-> oyunun 640x480 goruntusu. Savas odalari 640x480, harita odalari 320x240'lik bir view'u 2x gosterir.
View currentView(void* self) {
    View v{ 0, 0, 640, 480, 0, 0, 640, 480 };
    double en = 0;
    if (!gm::readInstance(self, vViewEn, en) || en == 0) return v;
    double a[8];
    const int vars[8] = { vViewX, vViewY, vViewW, vViewH, vPortX, vPortY, vPortW, vPortH };
    for (int i = 0; i < 8; ++i) if (!gm::readInstanceArr(self, vars[i], 0, a[i])) return v;
    if (a[2] <= 0 || a[3] <= 0 || a[6] <= 0 || a[7] <= 0) return v;
    return { (float)a[0], (float)a[1], (float)a[2], (float)a[3], (float)a[4], (float)a[5], (float)a[6], (float)a[7] };
}

void* create(int obj, float x, float y) {
    gm::RValue a[3]{};
    a[0].real = x; a[1].real = y; a[2].real = obj;
    double id = gm::call(gm::ADDR_INSTANCE_CREATE, 3, a);
    return id >= 0 ? gm::instanceById((int)id) : nullptr;
}

void sound(int snd) {
    gm::RValue a[3]{};
    a[0].real = snd; a[1].real = 80; a[2].real = 0;
    gm::call(gm::ADDR_AUDIO_PLAY_SOUND, 3, a);
}

// GameMaker acisi: 0 = sag, 90 = yukari (y asagi dogru arttigi icin -dy).
float gmAngle(float dx, float dy) { return (float)(std::atan2(-dy, dx) * 180.0 / 3.14159265358979); }

std::vector<Target> targets() {
    std::vector<Target> out;
    Target t{};
    if (void* h = gm::findInstance(gm::OBJ_HEART, gVarObj)) {
        if (bbox(h, t.box)) out.push_back({ true, -1, h, t.box });
    } else if (void* mc = gm::findInstance(gm::OBJ_MAINCHARA, gVarObj)) {
        if (bbox(mc, t.box)) out.push_back({ true, -1, mc, t.box });
    }
    for (int i = 0; i < 3; ++i) {
        double alive = 0, id = -4, hp = 0;
        if (!gm::readGlobal("monster", alive, i) || alive != 1) continue;
        if (!gm::readGlobal("monsterhp", hp, i) || hp <= 0) continue;
        if (!gm::readGlobal("monsterinstance", id, i)) continue;
        if (void* m = gm::instanceById((int)id))
            if (bbox(m, t.box)) out.push_back({ false, i, m, t.box });
    }
    return out;
}

void hitPlayer() {
    const bool battle = gm::findInstance(gm::OBJ_HEART, gVarObj) != nullptr;
    double hp = 0;
    if (!gm::readGlobal("hp", hp)) return;
    // Savasta oyunun scr_damagestandard'i gibi: can, ses, sarsinti, dokunulmazlik. Haritada olum yok: en az 1.
    hp = battle ? std::fmax(0, hp - 1) : std::fmax(1, hp - 1);
    gm::writeGlobal("hp", hp);
    sound(gm::SND_HURT1);
    if (battle) {
        gm::writeGlobal("hshake", 2); gm::writeGlobal("vshake", 2); gm::writeGlobal("shakespeed", 2);
        create(gm::OBJ_SHAKER, 0, 0);
        double inv = 0;
        if (gm::readGlobal("inv", inv)) gm::writeGlobal("invc", inv);
    }
    ulog::write("SILAH: %s 1 hasar aldi (HP %.0f)", battle ? "kalp" : "Frisk", hp);
}

void hitMonster(int i, void* mon) {
    double obj = -1, hp = 0;
    gm::readInstance(mon, gVarObj, obj);
    gm::readGlobal("monsterhp", hp, i);
    if ((int)obj == gm::OBJ_SANSB) {  // Sans'in cani 1 ve her vurustan kacar: oyunun kendi olum sahnesi
        gSansKill = true;
        ulog::write("SILAH: Sans vuruldu (olum sahnesi oyuncunun turunda baslar)");
        return;
    }
    gm::writeGlobal("damage", 1);
    if (hp - 1 >= 1) {
        // Olumcul degil: can dogrudan azalir, oyunun hasar yazisi (obj_dmgwriter) gosterilir. Oyunun kendi hasar
        // akisi kullanilmaz, cunku o akis sirayi dusmana gecirir (dusman turunun ortasinda akisi bozar).
        gm::writeGlobal("monsterhp", hp - 1, i);
        gm::writeGlobal("mytarget", i);
        const float x = (float)rd(mon, vX), y = (float)rd(mon, vY), w = (float)rd(mon, vSprW);
        if (void* dw = create(gm::OBJ_DMGWRITER, x + w / 2 - 48, y - 24)) gm::writeInstance(dw, vAlarm, 45, 2);
        sound(gm::SND_DAMAGE);
        ulog::write("SILAH: dusman %d (obj %d) 1 hasar aldi (HP %.0f)", i, (int)obj, hp - 1);
    } else {
        // Olumcul: dusmanin kendi hasar akisi (sallanma, sayi, olum animasyonu, savas sonu).
        // Sira oyuncudaysa (menu), FIGHT secildiginde oldugu gibi kutudaki aciklama metnini kapat (halt = 3: yazici
        // kendini siler). Kalirsa "YOU WON" ustune yazilir ve savas, butun yazicilar bitmeden kapanmaz.
        double mn = -1, my = -1;
        if (gm::readGlobal("mnfight", mn) && mn == 0 && gm::readGlobal("myfight", my) && my == 0)
            gm::forEachInstance([](void* w) {
                double o = -1;
                if (gm::readInstance(w, gVarObj, o) && (int)o == gm::OBJ_WRITER) gm::writeInstanceVar(w, "halt", 3);
            });
        gm::writeInstanceVar(mon, "takedamage", 1);
        double dt = 0;
        if (!gm::readGlobal("damagetimer", dt) || dt < 1) gm::writeGlobal("damagetimer", 10);
        gm::writeGlobal("hurtanim", 1, i);
        gHitFrames[i] = 150;
        ulog::write("SILAH: dusman %d (obj %d) son 1 hasarla oluyor", i, (int)obj);
    }
}

void hit(const Target& t) {
    if (t.player) hitPlayer();
    else hitMonster(t.monster, t.inst);
}

void spawn(const Shot& s) {
    void* self = gm::findInstance(gm::OBJ_HEART, gVarObj);
    const bool battle = self != nullptr;
    if (!self) self = gm::findInstance(gm::OBJ_MAINCHARA, gVarObj);
    if (!self) { ulog::write("SILAH: ates edilemedi (savasta ya da haritada degil)"); return; }
    const View v = currentView(self);
    const float tx = v.x + (s.px - v.px) * v.w / v.pw, ty = v.y + (s.py - v.py) * v.h / v.ph;
    const float k = v.w / 640.0f;  // haritada 0.5 (view 320 genis), savasta 1
    // Silah hedefle ekranin ortasi arasinda durur, hedefin icinden disari dogru ates eder (hep ekranda kalir).
    float ux = v.x + v.w / 2 - tx, uy = v.y + v.h / 2 - ty, len = std::hypot(ux, uy);
    if (len < 30 * k) { ux = 0; uy = -1; } else { ux /= len; uy /= len; }

    Proj p{};
    p.kind = s.kind;
    void* inst = nullptr;
    if (s.kind == BLASTER) {
        float bx = tx + ux * 170 * k, by = ty + uy * 170 * k;
        bx = std::fmin(std::fmax(bx, v.x + 30 * k), v.x + v.w - 30 * k);
        by = std::fmin(std::fmax(by, v.y + 30 * k), v.y + v.h - 30 * k);
        float dx = tx - bx, dy = ty - by, dl = std::hypot(dx, dy);
        if (dl < 1) { dx = -ux; dy = -uy; } else { dx /= dl; dy /= dl; }
        const float rot = gmAngle(dx, dy) + 90;  // obj_gasterblaster isini image_angle - 90 yonune atar
        p.scale = battle ? 2.0f : 1.0f;          // 2x: Sans'in buyuk Blaster'i (ekran sarsintisi sadece savasta guvenli)
        inst = create(gm::OBJ_GASTERBLASTER, bx + ux * 200 * k, by + uy * 200 * k);
        if (!inst) return;
        gm::writeInstanceVar(inst, "idealx", bx);
        gm::writeInstanceVar(inst, "idealy", by);
        gm::writeInstanceVar(inst, "idealrot", rot);
        gm::writeInstanceVar(inst, "pause", 10);
        gm::writeInstanceVar(inst, "col_o", 2);  // oyunun kendi kalp carpismasi kapali; hasari biz veriyoruz
        gm::writeInstance(inst, vAngle, rot + 120);
        gm::writeInstance(inst, vXScale, p.scale);
        gm::writeInstance(inst, vYScale, p.scale);
        gm::writeInstance(inst, vDepth, -100000);
        p.dx = dx; p.dy = dy;
        sound(gm::SND_SEGAPOWER);
    } else {
        const float sx = tx + ux * 300 * k, sy = ty + uy * 300 * k;
        float dx = tx - sx, dy = ty - sy, dl = std::hypot(dx, dy);
        dx /= dl; dy /= dl;
        const float ang = gmAngle(dx, dy);
        inst = create(gm::OBJ_NPC_MARKER, sx, sy);
        if (!inst) return;
        gm::writeInstance(inst, vSprite, gm::SPR_FOLLOWSPEAR);
        gm::writeInstance(inst, vVisible, 1);
        gm::writeInstance(inst, vImgSpeed, 0);
        gm::writeInstance(inst, vAngle, ang);
        gm::writeInstance(inst, vDir, ang);
        gm::writeInstance(inst, vSpeed, 13 * k);
        gm::writeInstance(inst, vDepth, -100000);
        p.dx = dx; p.dy = dy;
        sound(gm::SND_ARROW);
    }
    p.id = (int)rd(inst, vId);
    gProj.push_back(p);
    ulog::write("SILAH: %s ateslendi -> (%.0f,%.0f) %s", s.kind == BLASTER ? "Gaster Blaster" : "Mizrak", tx, ty,
                battle ? "savas" : "harita");
}

// Kalin isin (merkez cizgi +- hw) kutuya degiyor mu; isin (ox,oy)'dan (dx,dy) yonune sonsuza gider.
bool beamHits(float ox, float oy, float dx, float dy, float hw, const Rect& r) {
    const float cx[4] = { r.l, r.r, r.r, r.l }, cy[4] = { r.t, r.t, r.b, r.b };
    float dmin = 1e9f, dmax = -1e9f, tmax = -1e9f;
    for (int i = 0; i < 4; ++i) {
        const float px = cx[i] - ox, py = cy[i] - oy;
        const float along = px * dx + py * dy, normal = px * -dy + py * dx;
        dmin = std::fmin(dmin, normal); dmax = std::fmax(dmax, normal); tmax = std::fmax(tmax, along);
    }
    return dmin <= hw && dmax >= -hw && tmax >= 0;
}

void hide(void* inst) {
    gm::writeInstance(inst, vSpeed, 0);
    gm::writeInstance(inst, vVisible, 0);
    gm::writeInstance(inst, vX, -100000);
}

} // namespace

void queueShot(int kind, float portX, float portY) {
    if (kind == BLASTER || kind == SPEAR) gQueue.push_back({ kind, portX, portY });
}

void tick(int objectIndexVar) {
    gVarObj = objectIndexVar;
    initVars();
    for (int i = 0; i < 3; ++i) if (gHitFrames[i] > 0) --gHitFrames[i];
    for (const Shot& s : gQueue) spawn(s);
    gQueue.clear();
    if (gProj.empty()) return;

    const std::vector<Target> ts = targets();
    for (size_t n = 0; n < gProj.size();) {
        Proj& p = gProj[n];
        void* inst = gm::instanceById(p.id);
        if (!timestop::active()) ++p.life;  // zaman durunca havada bekler, omru islemez
        bool done = !inst || p.life > 600;
        if (!done && p.kind == BLASTER && !p.fired) {
            double con = 0;
            if (gm::readInstanceVar(inst, "con", con) && con >= 7) {
                // Isin cikti: oyunun cizdigi isinin agzindan (70 * olcek / 2) itibaren, carpisma yari genisligi
                // 3/8 * btmax (obj_gasterblaster'in collision_line'lari).
                p.fired = true;
                const float x = (float)rd(inst, vX), y = (float)rd(inst, vY);
                const float ox = x + p.dx * 35 * p.scale, oy = y + p.dy * 35 * p.scale;
                const float hw = 0.375f * 4 * std::floor(35 * p.scale / 4);
                sound(gm::SND_SEGAPOWER + 1);
                int hits = 0;
                for (const Target& t : ts)
                    if (beamHits(ox, oy, p.dx, p.dy, hw, t.box)) { hit(t); ++hits; }
                if (!hits) ulog::write("SILAH: Blaster kimseye degmedi");
            }
        } else if (!done && p.kind == SPEAR) {
            const float x = (float)rd(inst, vX), y = (float)rd(inst, vY);
            const float tipx = x + p.dx * 25, tipy = y + p.dy * 25;
            for (const Target& t : ts)
                if (tipx >= t.box.l - 3 && tipx <= t.box.r + 3 && tipy >= t.box.t - 3 && tipy <= t.box.b + 3) {
                    hit(t);
                    done = true;
                    break;
                }
            if (!done && p.life > 150) done = true;  // ekrandan cikti
            if (done) hide(inst);
        }
        if (done) gProj.erase(gProj.begin() + n);
        else ++n;
    }
}

bool isOurs(void* inst) {
    if (gProj.empty()) return false;
    double id = -1;
    if (!gm::readInstance(inst, vId, id)) return false;
    for (const Proj& p : gProj) if (p.id == (int)id) return true;
    return false;
}

bool takeSansKill() {
    bool k = gSansKill;
    gSansKill = false;
    return k;
}

bool hitInProgress(int monster) { return monster >= 0 && monster < 3 && gHitFrames[monster] > 0; }
bool anyHitInProgress() { return gHitFrames[0] > 0 || gHitFrames[1] > 0 || gHitFrames[2] > 0; }

} // namespace weapons
