#include "timestop.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <unordered_map>

#include "MinHook.h"
#include "imgui.h"
#include "gm.h"
#include "log.h"

namespace timestop {
namespace {

// Perform_Event(self, other, object, evType, evNum): her olay (Create, Step, Alarm, Collision, Draw...) buradan gecer.
// 0x41CA40 filtreleyip buraya atlar (jmp); bazi donguler dogrudan cagirir.
constexpr uintptr_t ADDR_PERFORM_EVENT = 0x0041C980;
constexpr int INST_OBJECT_INDEX = 0x64;  // CInstance icinde object_index (motor Perform_Event'e bunu gecirir)
constexpr uintptr_t ADDR_DRAW_SPRITE_EXT = 0x004FA440;
constexpr uintptr_t ADDR_DRAW_LINE_WIDTH = 0x004F8240;
constexpr uintptr_t ADDR_DRAW_SET_COLOR  = 0x004F7C70;
constexpr uintptr_t ADDR_DRAW_SET_ALPHA  = 0x004F7C90;
constexpr int SND_STOP = 229;    // mus_cymbal
constexpr int SND_RESUME = 28;   // snd_bell

enum { EV_CREATE = 0, EV_DESTROY = 1, EV_ALARM = 2, EV_STEP = 3, EV_COLLISION = 4, EV_KEYBOARD = 5, EV_OTHER = 7,
       EV_DRAW = 8, EV_KEYPRESS = 9, EV_KEYRELEASE = 10 };

using PerformEvent = void(__cdecl*)(void* self, void* other, int obj, int type, int num);
PerformEvent oPerform = nullptr;

volatile bool gActive = false;
bool gPending = false;
ULONGLONG gStart = 0, gChanged = 0;
int gVarObj = -1;

// Zaman durunca da calisanlar: kalp, Frisk, mor ruh kalbi, Undyne kalkani (oyuncunun), obj_time (klavye girdisi).
bool keeps(int obj) {
    return obj == gm::OBJ_HEART || obj == gm::OBJ_MAINCHARA || obj == 1575 || obj == 264 || obj == 364;
}

struct Saved {
    double speed, direction, gravity, friction, imageSpeed, pathSpeed;
    double alarm[12];
};
std::unordered_map<int, Saved> gSaved;  // instance id -> durdurulmadan onceki hareket

int vId, vSpeed, vDir, vGrav, vFric, vImgSpd, vPathSpd, vAlarm, vX, vY, vSprite, vImg, vXS, vYS, vAngle, vAlpha;

void initVars() {
    static bool done = false;
    if (done) return;
    done = true;
    vId = gm::findBuiltinVar("id"); vSpeed = gm::findBuiltinVar("speed"); vDir = gm::findBuiltinVar("direction");
    vGrav = gm::findBuiltinVar("gravity"); vFric = gm::findBuiltinVar("friction");
    vImgSpd = gm::findBuiltinVar("image_speed"); vPathSpd = gm::findBuiltinVar("path_speed");
    vAlarm = gm::findBuiltinVar("alarm"); vX = gm::findBuiltinVar("x"); vY = gm::findBuiltinVar("y");
    vSprite = gm::findBuiltinVar("sprite_index"); vImg = gm::findBuiltinVar("image_index");
    vXS = gm::findBuiltinVar("image_xscale"); vYS = gm::findBuiltinVar("image_yscale");
    vAngle = gm::findBuiltinVar("image_angle"); vAlpha = gm::findBuiltinVar("image_alpha");
}

double rd(void* inst, int var) { double v = 0; gm::readInstance(inst, var, v); return v; }
double rdv(void* inst, const char* name) { double v = 0; gm::readInstanceVar(inst, name, v); return v; }

void callDraw(uintptr_t fn, std::initializer_list<double> vals) {
    gm::RValue a[9]{};
    int n = 0;
    for (double v : vals) a[n++].real = v;
    gm::call(fn, n, a);
}

// Donmus Gaster Blaster: kendi Draw olayi (hareketi ve isini orada hesaplar) yerine son haliyle cizilir.
void drawFrozenBlaster(void* self) {
    callDraw(ADDR_DRAW_SPRITE_EXT, { rd(self, vSprite), rd(self, vImg), rd(self, vX), rd(self, vY), rd(self, vXS),
                                     rd(self, vYS), rd(self, vAngle), 16777215, rd(self, vAlpha) });
    if (rdv(self, "con") != 7) return;
    const double x = rd(self, vX), y = rd(self, vY), bt = rdv(self, "bt") + rdv(self, "bb");
    callDraw(ADDR_DRAW_SET_ALPHA, { std::fmax(0.0, std::fmin(1.0, rdv(self, "fade"))) });
    callDraw(ADDR_DRAW_SET_COLOR, { 16777215 });
    callDraw(ADDR_DRAW_LINE_WIDTH, { x + rdv(self, "xx"), y + rdv(self, "yy"), x + rdv(self, "xxx"), y + rdv(self, "yyy"), bt });
    callDraw(ADDR_DRAW_SET_ALPHA, { 1 });
}

void __cdecl hkPerform(void* self, void* other, int obj, int type, int num) {
    if (gActive && self) {
        const int real = *(int*)((char*)self + INST_OBJECT_INDEX);
        if (!keeps(real)) {
            switch (type) {
            case EV_ALARM: case EV_STEP: case EV_COLLISION: case EV_KEYBOARD: case EV_KEYPRESS: case EV_KEYRELEASE:
                return;
            case EV_OTHER:  // oda disi / sinir / animasyon sonu / yol sonu (kendiliginden olanlar)
                if (num == 0 || num == 1 || num == 7 || num == 8) return;
                break;
            case EV_DRAW:
                // Blaster hareketini Draw'da yapar: ucup sarj olana kadar (con < 4) izin ver, sonra dondur.
                if (num == 0 && real == gm::OBJ_GASTERBLASTER && rdv(self, "con") >= 4) { drawFrozenBlaster(self); return; }
                break;
            }
        }
    }
    oPerform(self, other, obj, type, num);
}

void sound(int snd) {
    gm::RValue a[3]{};
    a[0].real = snd; a[1].real = 80; a[2].real = 0;
    gm::call(gm::ADDR_AUDIO_PLAY_SOUND, 3, a);
}

// Durmus zamanda: yeni gorulen her instance'in motor hareketi (speed, gravity, animasyon, yol) sifirlanir; alarm
// sayaclari motorun her adimdaki azaltmasi geri alinarak yerinde tutulur.
void holdAll() {
    gm::forEachInstance([](void* inst) {
        const int obj = *(int*)((char*)inst + INST_OBJECT_INDEX);
        if (keeps(obj)) return;
        const int id = (int)rd(inst, vId);
        auto it = gSaved.find(id);
        if (it == gSaved.end()) {
            Saved s{ rd(inst, vSpeed), rd(inst, vDir), rd(inst, vGrav), rd(inst, vFric), rd(inst, vImgSpd), rd(inst, vPathSpd), {} };
            for (int i = 0; i < 12; ++i) gm::readInstanceArr(inst, vAlarm, i, s.alarm[i]);
            gSaved.emplace(id, s);
            gm::writeInstance(inst, vSpeed, 0);
            gm::writeInstance(inst, vGrav, 0);
            gm::writeInstance(inst, vFric, 0);
            gm::writeInstance(inst, vImgSpd, 0);
            gm::writeInstance(inst, vPathSpd, 0);
            return;
        }
        Saved& s = it->second;
        for (int i = 0; i < 12; ++i) {
            double cur = -1;
            if (!gm::readInstanceArr(inst, vAlarm, i, cur)) continue;
            const double last = s.alarm[i];
            if (last > 0 && (cur == last - 1 || cur <= 0)) gm::writeInstance(inst, vAlarm, last, i);  // azalmayi geri al
            else s.alarm[i] = cur;  // kod yeni deger atadi (orn. Blaster sarj sayaci)
        }
    });
}

void releaseAll() {
    for (auto& [id, s] : gSaved)
        if (void* inst = gm::instanceById(id)) {
            gm::writeInstance(inst, vDir, s.direction);
            gm::writeInstance(inst, vSpeed, s.speed);
            gm::writeInstance(inst, vGrav, s.gravity);
            gm::writeInstance(inst, vFric, s.friction);
            gm::writeInstance(inst, vImgSpd, s.imageSpeed);
            gm::writeInstance(inst, vPathSpd, s.pathSpeed);
        }
    gSaved.clear();
}

} // namespace

bool install() {
    return MH_CreateHook((void*)ADDR_PERFORM_EVENT, (void*)hkPerform, (void**)&oPerform) == MH_OK &&
           MH_EnableHook((void*)ADDR_PERFORM_EVENT) == MH_OK;
}

void toggle() { if (oPerform) gPending = true; }

void tick(int objectIndexVar) {
    gVarObj = objectIndexVar;
    initVars();
    if (gPending) {
        gPending = false;
        gChanged = GetTickCount64();
        if (!gActive) {
            gSaved.clear();
            gActive = true;
            gStart = gChanged;
            holdAll();
            sound(SND_STOP);
            ulog::write("ZAMAN DURDU (%zu nesne dondu)", gSaved.size());
        } else {
            gActive = false;
            releaseAll();
            sound(SND_RESUME);
            ulog::write("ZAMAN AKIYOR (%.1f sn durdu)", (gChanged - gStart) / 1000.0);
        }
        return;
    }
    if (gActive) holdAll();
}

bool active() { return gActive; }
float seconds() { return gActive ? (GetTickCount64() - gStart) / 1000.0f : 0; }

void drawOverlay(ImDrawList* dl, float w, float h) {
    const float since = (GetTickCount64() - gChanged) / 1000.0f;
    const ImVec2 c(w * 0.5f, h * 0.5f);
    const float maxR = std::hypot(w, h) * 0.5f;
    if (gActive) {
        // Durma ani: merkezden yayilan beyaz halka, ardindan ekran mor-gri tonda kalir.
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(70, 40, 120, 90));
        if (since < 0.6f) {
            const float t = since / 0.6f, r = maxR * t;
            dl->AddCircleFilled(c, r, IM_COL32(255, 255, 255, (int)(120 * (1 - t))), 64);
            dl->AddCircle(c, r, IM_COL32(255, 255, 255, (int)(255 * (1 - t))), 64, 6);
        }
        char buf[48];
        snprintf(buf, sizeof buf, "ZAMAN DURDU  %.1f sn", seconds());
        const float fs = std::fmax(16.0f, h / 30.0f);
        ImFont* f = ImGui::GetFont();
        const ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, 0, buf);
        const ImVec2 p((w - sz.x) * 0.5f, h * 0.03f);
        dl->AddText(f, fs, ImVec2(p.x + 2, p.y + 2), IM_COL32(0, 0, 0, 200), buf);
        dl->AddText(f, fs, p, IM_COL32(255, 220, 60, 255), buf);
    } else if (since < 0.4f && gStart) {
        // Akis ani: halka merkeze kapanir.
        const float t = since / 0.4f;
        dl->AddCircle(c, maxR * (1 - t), IM_COL32(255, 255, 255, (int)(220 * (1 - t))), 64, 6);
    }
}

} // namespace timestop
