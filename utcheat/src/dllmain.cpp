// UTCheat - Undertale icin oyun ici hile menusu (32-bit, D3D9 + Dear ImGui + MinHook)
// INSERT: menuyu ac/kapa
#include <windows.h>
#include <d3d9.h>
#include <cmath>
#include <cstdio>
#include <string>

#include "MinHook.h"
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include "gm.h"
#include "autododge.h"
#include "log.h"
#include "input.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

// ---------------------------------------------------------------- durum
struct Cheats {
    bool menuOpen = true;
    bool god = false;
    bool oneHit = true;
    float speed = 1.0f;
    int setGold = 1000;
    int setMaxHp = 99;
    dodge::Settings dodge;
    int battleChoice = 0;
    int pendingBattle = 0;  // ana thread'de baslatilacak battlegroup
    int pendingEnding = 0;  // 1 notr, 2 pasifist, 3 soykirim
    bool pendingRestore = false;
    bool skipCredits = true;
    int character = 0;  // kCharacters indeksi, 0 = Frisk
} g;

enum { END_NEUTRAL = 1, END_PACIFIST = 2, END_GENOCIDE = 3 };
const char* const kEndingNames[] = { "", "Notr", "Pasifist", "Soykirim" };

struct Encounter { int group; const char* name; };
const Encounter kEncounters[] = {
    { 56, "Muffet (mor ruh)" }, { 47, "Undyne (yeşil ruh)" }, { 27, "Papyrus (mavi ruh)" }, { 95, "Sans (mavi ruh)" },
    { 4, "Froggit" }, { 6, "Froggit + Whimsun" }, { 8, "3x Moldsmal" }, { 15, "Loox + Vegetoid + Migosp" },
    { 30, "Chilldrake + Snowdrake" }, { 36, "Icecap + Jerry + Chilldrake + Snowdrake" }, { 46, "Aaron + Woshua" },
    { 77, "Tsunderplane + Vulkin" }, { 78, "2x Pyrope" }, { 67, "Final Froggit + Astigmatism + Whimsalot" },
    { 68, "Knight Knight + Madjick" },
};
dodge::Status gDodge;

struct GameState {
    bool hasHp = false, hasGold = false, hasLv = false;
    double hp = 0, maxhp = 0, gold = 0, lv = 0;
    bool soul = false, frisk = false;
    double soulX = 0, soulY = 0, friskX = 0, friskY = 0;
} st;

HWND gWnd = nullptr;
WNDPROC gOrigWndProc = nullptr;
bool gImguiReady = false;
ImFont* gSplashFont = nullptr;
UINT gBbW = 0, gBbH = 0;
int gVarX = -1, gVarY = -1, gVarObj = -1;

// ---------------------------------------------------------------- hiz hilesi (zaman fonksiyonlarini olcekler)
CRITICAL_SECTION gTimeLock;
double gAppliedSpeed = 1.0;

using QPC_t = BOOL(WINAPI*)(LARGE_INTEGER*);
using GTC_t = DWORD(WINAPI*)();
using GTC64_t = ULONGLONG(WINAPI*)();
QPC_t oQPC; GTC_t oGetTickCount; GTC_t oTimeGetTime; GTC64_t oGetTickCount64;

struct Clock { bool init = false; long long realBase = 0, fakeBase = 0; };
Clock cQpc, cTick, cTime, cTick64;

long long scaleClock(Clock& c, long long real) {
    EnterCriticalSection(&gTimeLock);
    if (!c.init) { c.init = true; c.realBase = c.fakeBase = real; }
    long long fake = c.fakeBase + (long long)((double)(real - c.realBase) * gAppliedSpeed);
    LeaveCriticalSection(&gTimeLock);
    return fake;
}

void setSpeed(double s) {
    // Hiz degisirken saatler ziplamasin diye her saatin tabanini simdiki sahte degere tasi.
    LARGE_INTEGER q; oQPC(&q);
    long long now[4] = { q.QuadPart, (long long)oGetTickCount(), (long long)oTimeGetTime(), (long long)oGetTickCount64() };
    Clock* cs[4] = { &cQpc, &cTick, &cTime, &cTick64 };
    EnterCriticalSection(&gTimeLock);
    for (int i = 0; i < 4; ++i) {
        Clock& c = *cs[i];
        if (c.init) {
            c.fakeBase = c.fakeBase + (long long)((double)(now[i] - c.realBase) * gAppliedSpeed);
            c.realBase = now[i];
        }
    }
    gAppliedSpeed = s;
    LeaveCriticalSection(&gTimeLock);
}

BOOL WINAPI hkQPC(LARGE_INTEGER* p) {
    BOOL r = oQPC(p);
    if (r && p) p->QuadPart = scaleClock(cQpc, p->QuadPart);
    return r;
}
DWORD WINAPI hkGetTickCount() { return (DWORD)scaleClock(cTick, oGetTickCount()); }
DWORD WINAPI hkTimeGetTime() { return (DWORD)scaleClock(cTime, oTimeGetTime()); }
ULONGLONG WINAPI hkGetTickCount64() { return (ULONGLONG)scaleClock(cTick64, (long long)oGetTickCount64()); }

// ---------------------------------------------------------------- oyun bitti engeli
gm::Routine oScriptExecute;

void __cdecl hkScriptExecute(gm::RValue* result, void* self, void* other, int argc, gm::RValue* args) {
    if (g.god && argc > 0) {
        double id;
        if (gm::toDouble(args[0], id) && (int)id == gm::SCR_GAMEOVERB) {
            double maxhp;
            if (gm::readGlobal("maxhp", maxhp)) gm::writeGlobal("hp", maxhp);
            result->real = 0; result->kind = gm::KIND_REAL;
            return;
        }
    }
    oScriptExecute(result, self, other, argc, args);
}

// Ikinci emniyet: menuden soykirim sonu baslatildiysa obj_gameshake hic yaratilamaz (tickGenocide'in con kontrolu
// kacirilsa bile kayitlar silinmez, Steam Cloud'a yazilmaz).
gm::Routine oInstanceCreate;
bool gGenoGuard = false, gInstCreateHooked = false;

void __cdecl hkInstanceCreate(gm::RValue* result, void* self, void* other, int argc, gm::RValue* args) {
    double obj;
    if (gGenoGuard && argc >= 3 && gm::toDouble(args[2], obj) && (int)obj == gm::OBJ_GAMESHAKE) {
        ulog::write("SON: obj_gameshake engellendi (kayit silme / Steam Cloud yazimi yok)");
        result->real = -4; result->kind = gm::KIND_REAL;
        return;
    }
    oInstanceCreate(result, self, other, argc, args);
}

// ---------------------------------------------------------------- kayit yedegi
// Sonlar undertale.ini'ye / kayitlara yazar (Won, EndF, FFFFF...). Bir sona isinlanmadan once kayit klasoru
// %LOCALAPPDATA%\UNDERTALE_utcheat_yedek'e kopyalanir. Yedek geri yuklenene kadar uzerine yazilmaz, boylece
// art arda birkac son denense bile ilk (temiz) hali korunur.
std::wstring saveDir() {
    wchar_t b[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", b, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(b) + L"\\UNDERTALE" : std::wstring();
}
std::wstring backupDir() { std::wstring d = saveDir(); return d.empty() ? d : d + L"_utcheat_yedek"; }
bool dirExists(const std::wstring& d) {
    DWORD a = GetFileAttributesW(d.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// src'deki dosyalari dst'ye kopyalar (alt klasorsuz). purgeDst: src'de olmayan dst dosyalarini siler.
bool copyFiles(const std::wstring& src, const std::wstring& dst, bool purgeDst) {
    if (!dirExists(src) || (!dirExists(dst) && !CreateDirectoryW(dst.c_str(), nullptr))) return false;
    WIN32_FIND_DATAW fd;
    if (purgeDst) {
        HANDLE f = FindFirstFileW((dst + L"\\*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (GetFileAttributesW((src + L"\\" + fd.cFileName).c_str()) == INVALID_FILE_ATTRIBUTES)
                    DeleteFileW((dst + L"\\" + fd.cFileName).c_str());
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    HANDLE f = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ok &= CopyFileW((src + L"\\" + fd.cFileName).c_str(), (dst + L"\\" + fd.cFileName).c_str(), FALSE) != 0;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return ok;
}

bool backupSaves() {
    std::wstring s = saveDir(), b = backupDir();
    if (s.empty()) return false;
    if (dirExists(b)) { ulog::write("Kayit yedegi zaten var (geri yuklenmemis), uzerine yazilmadi"); return true; }
    if (!copyFiles(s, b, false)) { ulog::write("HATA: kayit yedegi alinamadi"); return false; }
    ulog::write("Kayitlar yedeklendi: %%LOCALAPPDATA%%\\UNDERTALE_utcheat_yedek");
    return true;
}

bool restoreSaves() {
    std::wstring s = saveDir(), b = backupDir();
    if (s.empty() || !dirExists(b)) return false;
    if (!copyFiles(b, s, true)) { ulog::write("HATA: kayitlar geri yuklenemedi"); return false; }
    // Yedek klasorunu sil: bir sonraki son yeniden temiz yedek alir.
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((b + L"\\*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((b + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    RemoveDirectoryW(b.c_str());
    ulog::write("Kayitlar yedekten geri yuklendi");
    return true;
}

// ---------------------------------------------------------------- sonlar
int gGenoStage = 0;  // 1: bos odaya gidiliyor, 2: Chara sahnesi suruyor
int gEndingActive = 0;  // jenerigi atlanacak son

void callRoutine(uintptr_t addr, int argc, gm::RValue* args) {
    gm::RValue result{};
    ((gm::Routine)addr)(&result, nullptr, nullptr, argc, args);
}
void roomGoto(int room) {
    gm::RValue a{}; a.real = room; a.kind = gm::KIND_REAL;
    callRoutine(gm::ADDR_ROOM_GOTO, 1, &a);
}

void startEnding(int which) {
    if (!backupSaves()) return;  // yedeksiz asla
    gm::writeGlobal("entrance", 0);
    gm::writeGlobal("interact", 0);
    const int from = gm::currentRoom();
    switch (which) {
    case END_NEUTRAL:  roomGoto(gm::ROOM_UNDERTALE_END); break;
    case END_PACIFIST: roomGoto(gm::ROOM_OUTSIDEWORLD); break;
    case END_GENOCIDE: gGenoGuard = true; roomGoto(gm::ROOM_EMPTY); gGenoStage = 1; break;
    }
    gEndingActive = which;
    ulog::write("SON: %s sonunun son sahnesine isinlaniyor (oda %d)", kEndingNames[which], from);
}

// Soykirim: bos odada obj_truechara'yi (oyunda obj_floweygonk'un yarattigi Chara sahnesi) yarat. Sahnenin sonunda
// oyun obj_gameshake ile TUM kayitlari siler, exe'yi silmeye calisir ve "system_information_962"yi Steam Cloud'a
// yazar (kalici ruhsuz isareti). Yerel yedek bunu geri alamaz; bu yuzden vurus animasyonu bitince (con 62, 40 kare
// bekleme) sahne kesilir ve oyun o adim calismadan yeniden baslatilir.
void tickGenocide() {
    if (gGenoStage == 1 && gm::currentRoom() == gm::ROOM_EMPTY) {
        if (void* mc = gm::findInstance(gm::OBJ_MAINCHARA, gVarObj))
            gm::writeInstance(mc, gm::findBuiltinVar("visible"), 0);
        gm::writeGlobal("interact", 1);
        gm::RValue args[3]{};
        args[2].real = gm::OBJ_TRUECHARA;
        callRoutine(gm::ADDR_INSTANCE_CREATE, 3, args);
        ulog::write("SON: Chara sahnesi basladi");
        gGenoStage = 2;
    } else if (gGenoStage == 2) {
        void* ch = gm::findInstance(gm::OBJ_TRUECHARA, gVarObj);
        double con = 0;
        if (!ch) { gGenoStage = 0; return; }
        if (gm::readInstanceVar(ch, "con", con) && con >= 62) {
            gm::writeInstanceVar(ch, "con", 900);
            gm::writeInstance(ch, gm::findBuiltinVar("alarm"), -1, 4);
            if (gWnd) SetWindowTextW(gWnd, L"UNDERTALE");
            roomGoto(gm::ROOM_EMPTYBLACK);
            ulog::write("SON: Chara sahnesi bitti; kayit silme / Steam Cloud adimi engellendi, oyun yeniden basliyor");
            gGenoStage = 0;
        }
    }
}

// Jenerik (tesekkurler) atlama.
// Notr: obj_credits_short sayfalari alarm[5] ile doner, bitince alarm[6] telefonu caldirir -> obj_mainend (Sans'in
// telefonu). Sayfalar atlanip dogrudan telefona gecilir.
// Pasifist: gun batimi sahnesi bitince (do_room_goto) cast roll + jenerik odalarina (278..284) gider; bu istek
// yakalanip jenerik sonrasi odaya (flag[512]'ye gore myroom ya da "THE END") gidilir.
void tickSkipCredits() {
    if (!g.skipCredits || !gEndingActive) return;
    if (gEndingActive == END_NEUTRAL) {
        if (void* cr = gm::findInstance(gm::OBJ_CREDITS_SHORT, gVarObj)) {
            const int alarm = gm::findBuiltinVar("alarm");
            gm::writeInstanceVar(cr, "number", 9);  // 9: hicbir sayfa cizilmez
            gm::writeInstance(cr, alarm, -1, 5);
            gm::writeInstance(cr, alarm, 1, 6);
            ulog::write("SON: jenerik atlandi, Sans'in telefonuna geciliyor");
            gEndingActive = 0;
        }
    } else if (gEndingActive == END_PACIFIST) {
        const int room = gm::currentRoom();
        void* ev = gm::findInstance(gm::OBJ_OUTSIDEWORLD_EVENT, gVarObj);
        double go = 0;
        const bool leaving = ev && gm::readInstanceVar(ev, "do_room_goto", go) && go != 0;
        if (leaving || (room >= gm::ROOM_END_CASTROLL && room <= gm::ROOM_CREDITSDODGER)) {
            if (ev) gm::writeInstanceVar(ev, "do_room_goto", 0);
            double f512 = 0;
            gm::readGlobal("flag", f512, 512);
            roomGoto(f512 == 0 ? gm::ROOM_END_MYROOM : gm::ROOM_END_THEEND);
            ulog::write("SON: jenerik atlandi (oda %d -> %s)", room, f512 == 0 ? "room_end_myroom" : "room_end_theend");
            gEndingActive = 0;
        }
    }
}

// ---------------------------------------------------------------- karakter degistirme
// obj_mainchara her adimda sprite_index'i yone gore dsprite/usprite/lsprite/rsprite'tan secer; bunlara baska bir
// karakterin yurume sprite'lari yazilir. Karakterlerin boyu farkli oldugu icin:
// - carpisma maskesi Frisk'te kalir (mask_index = spr_maincharad), duvarlara takilmaz;
// - sprite'lar kopyalanip (sprite_duplicate) kopyanin orijini, alt-ortasi Frisk'in ayaklarina gelecek sekilde
//   kaydirilir. Orijinaller degismez, dunyadaki NPC'ler normal cizilir.
struct CharDef {
    const char* name;
    int spr[4];  // asagi, yukari, sol, sag
    int ox[4];   // sprite'in kendi orijin x'i (yonler arasi hizalama)
    int w, h;    // asagi sprite'inin boyutu
};
const CharDef kCharacters[] = {
    { "Frisk",       { 1131, 1132, 1134, 1133 }, {}, 20, 30 },
    { "Chara",       { 1108, 1114, 1112, 1110 }, {}, 20, 30 },
    { "Sans",        { 1443, 1452, 1457, 1453 }, {}, 25, 32 },
    { "Papyrus",     { 1402, 1414, 1419, 1417 }, {}, 27, 44 },
    { "Toriel",      { 1191, 1200, 1196, 1195 }, {}, 32, 58 },
    { "Undyne",      { 1494, 1502, 1504, 1506 }, { 0, 6, 0, 10 }, 26, 54 },
    { "Alphys",      { 1729, 1742, 1741, 1734 }, {}, 29, 34 },
    { "Asgore",      { 2003, 2006, 1999, 2000 }, {}, 57, 63 },
    { "Asriel",      { 2526, 2528, 2533, 2530 }, {}, 16, 28 },
    { "Monster Kid", { 1482, 1490, 1485, 1487 }, {}, 22, 29 },
    { "Napstablook", { 1216, 1218, 1213, 1221 }, {}, 17, 33 },
};
constexpr int kCharCount = (int)(sizeof kCharacters / sizeof kCharacters[0]);
int gCharSprites[kCharCount][4];  // hizalanmis kopyalar (0: henuz yok)
int gCharApplied = 0;
const char* const kDirVars[4] = { "dsprite", "usprite", "lsprite", "rsprite" };

double callRoutineResult(uintptr_t addr, int argc, gm::RValue* args) {
    gm::RValue result{};
    ((gm::Routine)addr)(&result, nullptr, nullptr, argc, args);
    double v = -1;
    gm::toDouble(result, v);
    return v;
}

// Karakterin 4 sprite'ini bir kez kopyalayip hizalar; kopyalanamazsa orijinali kullanir.
const int* characterSprites(int ci) {
    int* out = gCharSprites[ci];
    if (out[0]) return out;
    const CharDef& c = kCharacters[ci];
    for (int i = 0; i < 4; ++i) {
        gm::RValue a[3]{};
        a[0].real = c.spr[i];
        double dup = callRoutineResult(gm::ADDR_SPRITE_DUPLICATE, 1, a);
        if (dup < 0) { out[i] = c.spr[i]; continue; }
        a[0].real = dup;
        a[1].real = c.ox[i] + std::round(c.w / 2.0 - 10);  // yatayda Frisk'in ortasina
        a[2].real = c.h - 30;                              // ayaklar Frisk'in ayaklarina
        callRoutineResult(gm::ADDR_SPRITE_SET_OFFSET, 3, a);
        out[i] = (int)dup;
    }
    ulog::write("Karakter sprite'lari hazirlandi: %s (%d %d %d %d)", c.name, out[0], out[1], out[2], out[3]);
    return out;
}

void tickCharacter() {
    void* mc = gm::findInstance(gm::OBJ_MAINCHARA, gVarObj);
    if (!mc) return;
    static const int vMask = gm::findBuiltinVar("mask_index");
    if (g.character == 0) {
        if (gCharApplied != 0) {  // Frisk'e don (yeni odada oyun zaten Frisk'le yaratir)
            for (int i = 0; i < 4; ++i) gm::writeInstanceVar(mc, kDirVars[i], kCharacters[0].spr[i]);
            gm::writeInstance(mc, vMask, -1);
            gCharApplied = 0;
        }
        return;
    }
    const int* spr = characterSprites(g.character);
    for (int i = 0; i < 4; ++i) {
        double cur = -1;
        if (gm::readInstanceVar(mc, kDirVars[i], cur) && (int)cur != spr[i]) gm::writeInstanceVar(mc, kDirVars[i], spr[i]);
    }
    gm::writeInstance(mc, vMask, gm::SPR_MAINCHARAD);
    gCharApplied = g.character;
}

// ---------------------------------------------------------------- oyun durumu (ana thread'de, her kare)
void tickGame() {
    if (gVarObj < 0) {
        gVarX = gm::findBuiltinVar("x");
        gVarY = gm::findBuiltinVar("y");
        gVarObj = gm::findBuiltinVar("object_index");
    }
    if (g.god) {
        double hp, maxhp;
        if (gm::readGlobal("hp", hp) && gm::readGlobal("maxhp", maxhp) && hp < maxhp) gm::writeGlobal("hp", maxhp);
    }
    st.hasHp = gm::readGlobal("hp", st.hp) && gm::readGlobal("maxhp", st.maxhp);
    st.hasGold = gm::readGlobal("gold", st.gold);
    st.hasLv = gm::readGlobal("lv", st.lv);

    void* h = gm::findInstance(gm::OBJ_HEART, gVarObj);
    st.soul = h && gm::readInstance(h, gVarX, st.soulX) && gm::readInstance(h, gVarY, st.soulY);
    void* m = gm::findInstance(gm::OBJ_MAINCHARA, gVarObj);
    st.frisk = m && gm::readInstance(m, gVarX, st.friskX) && gm::readInstance(m, gVarY, st.friskY);
    static bool wasFrisk = false;
    if (st.frisk != wasFrisk) {
        if (st.frisk) ulog::write("HARITA: Frisk (%.0f,%.0f)", st.friskX, st.friskY);
        wasFrisk = st.frisk;
    }

    dodge::tick(g.dodge, gDodge);

    if (g.oneHit && st.soul) {
        // Tek vurus: dusmanlarin cani 1'e cekilir; ayrica oyuncunun vurusu kaydedildigi an (hurtanim 1) canavarin
        // takedamage'i ONE_HIT_DMG yapilir, boylece cubugun kenarindan vurulan zayif bir vurus bile oldurur.
        constexpr double ONE_HIT_DMG = 999999999;
        for (int i = 0; i < 3; ++i) {
            double mhp, ha = 0, mid = -4;
            if (gm::readGlobal("monsterhp", mhp, i) && mhp > 1) gm::writeGlobal("monsterhp", 1, i);
            if (gm::readGlobal("hurtanim", ha, i) && (ha == 1 || ha == 3) && gm::readGlobal("monsterinstance", mid, i))
                if (void* mon = gm::instanceById((int)mid)) {
                    double td = 0;
                    if (gm::readInstanceVar(mon, "takedamage", td) && td != ONE_HIT_DMG) {
                        gm::writeInstanceVar(mon, "takedamage", ONE_HIT_DMG);
                        gm::writeGlobal("damage", ONE_HIT_DMG);
                        ulog::write("TEK VURUS: dusman %d (obj %d) takedamage %.0f -> 999999999", i, (int)[&] {
                            double o = -1; gm::readInstance(mon, gVarObj, o); return o; }(), td);
                    }
                }
        }
        // Ekrandaki hasar sayisi (Sans'in 9999999'u dahil) de ONE_HIT_DMG gorunsun.
        gm::forEachInstance([&](void* w) {
            double o = -1, dmg = 0;
            if (gm::readInstance(w, gVarObj, o) && (int)o == gm::OBJ_DMGWRITER &&
                gm::readInstanceVar(w, "dmg", dmg) && dmg > 0 && dmg != ONE_HIT_DMG)
                gm::writeInstanceVar(w, "dmg", ONE_HIT_DMG);
        });
        // Sans vurulamaz (her saldiriya "MISS" ile kacar). Oyuncu saldirdigi an (global.damagetimer > 0) kacisi
        // iptal edip oyunun kendi final vurusunu (sahte FIGHT butonunun yaptigi gibi death_c = 1) baslat.
        if (void* body = gm::findInstance(gm::OBJ_SANSB_BODY, gVarObj)) {
            double deathC = 0, dmgTimer = -1;
            gm::readInstanceVar(body, "death_c", deathC);
            gm::readGlobal("damagetimer", dmgTimer);
            if (deathC == 0 && dmgTimer > 0) {
                gm::writeInstanceVar(body, "dodge", 0);
                gm::writeInstance(body, gm::findBuiltinVar("hspeed"), 0);
                gm::writeInstanceVar(body, "death_c", 1);

                if (void* h = gm::findInstance(gm::OBJ_HEART, gVarObj)) gm::writeInstanceVar(h, "movement", -1);
                ulog::write("TEK VURUS: Sans'in kacisi iptal, final vurusu baslatildi");
                deathC = 1;
            }
            // Olum sahnesi suresince normal tur akisini dondur: MISS yazan alarm[3]'u iptal et ve hurtanim'i
            // "vurulma suruyor" (3) tut. Savas kontrolcusu bekler, sahneyi oyunun olum zinciri yonetir.
            if (deathC >= 1) {
                if (void* sb = gm::findInstance(gm::OBJ_SANSB, gVarObj)) {
                    gm::writeInstance(sb, gm::findBuiltinVar("alarm"), -1, 3);
                    double me = 0, ha = 0;
                    gm::readInstanceVar(sb, "myself", me);
                    if (gm::readGlobal("hurtanim", ha, (int)me) && (ha == 1 || ha == 2 || ha == 5))
                        gm::writeGlobal("hurtanim", 3, (int)me);
                }
            }
        }
    }

    if (g.pendingBattle) {
        // Oyunun kendi rastgele karsilasmalari gibi: battlegroup ayarla, obj_battler yarat.
        double interact;
        if (st.frisk && !st.soul && gm::readGlobal("interact", interact) && interact == 0) {
            gm::writeGlobal("border", 0);
            gm::writeGlobal("battlegroup", g.pendingBattle);
            gm::RValue args[3]{};
            args[0].real = 0; args[1].real = 0; args[2].real = gm::OBJ_BATTLER;
            gm::RValue result{};
            ((gm::Routine)gm::ADDR_INSTANCE_CREATE)(&result, nullptr, nullptr, 3, args);
            ulog::write("Test savasi baslatildi: battlegroup=%d", g.pendingBattle);
        }
        g.pendingBattle = 0;
    }

    if (g.pendingEnding) { startEnding(g.pendingEnding); g.pendingEnding = 0; }
    tickGenocide();
    tickSkipCredits();
    tickCharacter();
    if (g.pendingRestore) {
        g.pendingRestore = false;
        if (restoreSaves()) callRoutine(gm::ADDR_GAME_RESTART, 0, nullptr);
    }
}

// ---------------------------------------------------------------- menu
void drawMenu() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Undertale Hile Menüsü", &g.menuOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("INSERT: menüyü aç/kapa");
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo("Karakter", kCharacters[g.character].name)) {
        for (int i = 0; i < kCharCount; ++i)
            if (ImGui::Selectable(kCharacters[i].name, i == g.character)) {
                g.character = i;
                ulog::write("Karakter: %s", kCharacters[i].name);
            }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Can");
    if (ImGui::Checkbox("Ölümsüzlük (God Mode)", &g.god)) ulog::write("God mode: %s", g.god ? "ACIK" : "KAPALI");
    if (ImGui::Checkbox("Tek vuruş (Sans dahil)", &g.oneHit)) ulog::write("Tek vurus: %s", g.oneHit ? "ACIK" : "KAPALI");
    if (st.hasHp) {
        ImGui::Text("HP: %.0f / %.0f", st.hp, st.maxhp);
        ImGui::SameLine();
        if (ImGui::SmallButton("Canı doldur")) gm::writeGlobal("hp", st.maxhp);
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("##maxhp", &g.setMaxHp, 1, 10);
        if (g.setMaxHp < 1) g.setMaxHp = 1;
        if (g.setMaxHp > 999) g.setMaxHp = 999;
        ImGui::SameLine();
        if (ImGui::Button("Max HP ayarla")) {
            gm::writeGlobal("maxhp", g.setMaxHp);
            gm::writeGlobal("hp", g.setMaxHp);
        }
    } else {
        ImGui::TextDisabled("Oyun henüz yüklenmedi");
    }

    ImGui::SeparatorText("Otomatik kaçış");
    if (ImGui::Checkbox("Auto-dodge", &g.dodge.enabled))
        ulog::write("Auto-dodge: %s (pay=%d, tahmin=%d)", g.dodge.enabled ? "ACIK" : "KAPALI", g.dodge.margin, g.dodge.lookahead);
    ImGui::SameLine();
    ImGui::Checkbox("Kutuları göster", &g.dodge.showBoxes);
    if (g.dodge.enabled) {
        ImGui::SetNextItemWidth(140);
        ImGui::SliderInt("Güvenlik payı", &g.dodge.margin, 0, 12, "%d px");
        ImGui::SetNextItemWidth(140);
        ImGui::SliderInt("Tahmin (kare)", &g.dodge.lookahead, 0, 8);
        int preset = g.dodge.natural ? 1 : 0;
        if (ImGui::RadioButton("Rage (ışınlan, no-hit)", preset == 0)) g.dodge.natural = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Legit (yürü)", preset == 1)) g.dodge.natural = true;
        if (g.dodge.natural) {
            ImGui::SetNextItemWidth(140);
            ImGui::SliderInt("Hız", &g.dodge.speed, 2, 20, "%d px/kare");
            ImGui::Checkbox("Acil durumda ışınlan (no-hit)", &g.dodge.emergency);
            ImGui::Checkbox("Mavi ruhta uçma, sadece zıpla", &g.dodge.blueJumpOnly);
        }
        ImGui::TextDisabled("%s  |  mermi: %d  |  kaçış: %d", gDodge.state, gDodge.hazards, gDodge.dodges);
    }

    ImGui::SeparatorText("Test savaşı");
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##enc", kEncounters[g.battleChoice].name)) {
        for (int i = 0; i < (int)(sizeof kEncounters / sizeof kEncounters[0]); ++i)
            if (ImGui::Selectable(kEncounters[i].name, i == g.battleChoice)) g.battleChoice = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!st.frisk || st.soul);
    if (ImGui::Button("Başlat")) g.pendingBattle = kEncounters[g.battleChoice].group;
    ImGui::EndDisabled();
    if (!st.frisk) ImGui::TextDisabled("Haritada yürürken kullanılabilir");

    if (ImGui::CollapsingHeader("Sonlar")) {
        ImGui::TextDisabled("Seçilen sonun son sahnesine ışınlar.\nÖnce kayıtlar otomatik yedeklenir.");
        ImGui::BeginDisabled(!st.frisk || st.soul || gGenoStage != 0);
        if (ImGui::Button("Nötr son (Sans'ın telefonu + jenerik)")) g.pendingEnding = END_NEUTRAL;
        if (ImGui::Button("Gerçek Pasifist son (gün batımı + jenerik)")) g.pendingEnding = END_PACIFIST;
        ImGui::BeginDisabled(!gInstCreateHooked);
        if (ImGui::Button("Soykırım sonu (Chara)")) g.pendingEnding = END_GENOCIDE;
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::Checkbox("Jeneriği (teşekkürler) atla", &g.skipCredits);
        ImGui::TextDisabled("Soykırım: kayıt silme ve Steam Cloud adımı\nengellenir, sahne bitince oyun yeniden başlar.");
        if (!st.frisk) ImGui::TextDisabled("Haritada yürürken kullanılabilir");
        if (dirExists(backupDir())) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Son öncesi kayıt yedeği duruyor");
            if (ImGui::Button("Kayıtları geri yükle (oyun yeniden başlar)")) g.pendingRestore = true;
        }
    }

    if (ImGui::CollapsingHeader("Altın") && st.hasGold) {
        ImGui::Text("Altın: %.0f", st.gold);
        ImGui::SameLine();
        if (ImGui::SmallButton("+100")) gm::writeGlobal("gold", st.gold + 100);
        ImGui::SameLine();
        if (ImGui::SmallButton("+1000")) gm::writeGlobal("gold", st.gold + 1000);
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("##gold", &g.setGold, 100, 1000);
        if (g.setGold < 0) g.setGold = 0;
        ImGui::SameLine();
        if (ImGui::Button("Altını ayarla")) gm::writeGlobal("gold", g.setGold);
        if (st.hasLv) ImGui::TextDisabled("LV: %.0f", st.lv);
    }

    if (ImGui::CollapsingHeader("Oyun hızı")) {
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("##speed", &g.speed, 0.1f, 3.0f, "%.2fx");
    static const float presets[] = { 0.25f, 0.5f, 1.0f, 2.0f };
    for (float preset : presets) {
        char label[16];
        snprintf(label, sizeof label, "%gx", preset);
        if (preset != 0.25f) ImGui::SameLine();
        if (ImGui::SmallButton(label)) g.speed = preset;
    }
    }

    if (ImGui::CollapsingHeader("Konum")) {
        if (st.soul) ImGui::Text("SOUL   X: %.1f   Y: %.1f", st.soulX, st.soulY);
        else ImGui::TextDisabled("SOUL: savaşta değil");
        if (st.frisk) ImGui::Text("Frisk  X: %.1f   Y: %.1f", st.friskX, st.friskY);
    }

    ImGui::End();
}

// ---------------------------------------------------------------- pencere mesajlari
LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_INSERT && !(lParam & (1 << 30))) {
        g.menuOpen = !g.menuOpen;
        return 0;
    }
    if (gImguiReady && g.menuOpen) {
        if (msg == WM_MOUSEMOVE && gBbW && gBbH) {
            // Pencere, backbuffer'dan farkli boyutta olabilir (tam ekran / olcekleme) -> koordinati cevir.
            RECT rc; GetClientRect(hWnd, &rc);
            int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
            if (cw > 0 && ch > 0) {
                int x = (short)LOWORD(lParam) * (int)gBbW / cw;
                int y = (short)HIWORD(lParam) * (int)gBbH / ch;
                lParam = MAKELPARAM(x, y);
            }
        }
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        ImGuiIO& io = ImGui::GetIO();
        bool mouseMsg = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
        bool keyMsg = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
        if ((mouseMsg && io.WantCaptureMouse) || (keyMsg && io.WantTextInput)) return 0;
    }
    return CallWindowProcW(gOrigWndProc, hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- D3D9
using Present_t = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
Present_t oPresent;
Reset_t oReset;

void initImgui(IDirect3DDevice9* dev) {
    D3DDEVICE_CREATION_PARAMETERS cp{};
    dev->GetCreationParameters(&cp);
    gWnd = cp.hFocusWindow;
    if (!gWnd) return;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0 };  // Latin + Turkce harfler
    char fontPath[MAX_PATH];
    GetWindowsDirectoryA(fontPath, MAX_PATH);
    strcat_s(fontPath, "\\Fonts\\segoeui.ttf");
    if (GetFileAttributesA(fontPath) == INVALID_FILE_ATTRIBUTES ||
        !io.Fonts->AddFontFromFileTTF(fontPath, 17.0f, nullptr, ranges))
        io.Fonts->AddFontDefault();
    // Enjeksiyon acilis yazisi icin buyuk kalin font (yoksa varsayilan font buyutulerek cizilir).
    GetWindowsDirectoryA(fontPath, MAX_PATH);
    strcat_s(fontPath, "\\Fonts\\segoeuib.ttf");
    if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES)
        gSplashFont = io.Fonts->AddFontFromFileTTF(fontPath, 44.0f, nullptr, ranges);

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6; s.FrameRounding = 4; s.GrabRounding = 4;
    s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.55f, 0.08f, 0.08f, 1);
    s.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.85f, 0.2f, 1);

    ImGui_ImplWin32_Init(gWnd);
    ImGui_ImplDX9_Init(dev);
    gOrigWndProc = (WNDPROC)SetWindowLongPtrW(gWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    vinput::setWindow(gWnd);
    gImguiReady = true;
}

// ---------------------------------------------------------------- enjeksiyon acilis yazisi
// Ilk 1 sn laf sokma, ardindan ~2.5 sn GitHub adi. Gercek saatle (hiz hilesinden etkilenmez).
void drawSplash() {
    static ULONGLONG start = 0;
    const ULONGLONG now = oGetTickCount64 ? oGetTickCount64() : GetTickCount64();
    if (!start) start = now;
    const float t = (float)(now - start);
    if (t > 3500) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImFont* font = gSplashFont ? gSplashFont : ImGui::GetFont();
    auto fade = [&](float from, float to) {  // [from, to] araliginda 150 ms giris / 250 ms cikis
        if (t < from || t > to) return 0.0f;
        return std::fmin(1.0f, std::fmin((t - from) / 150.0f, (to - t) / 250.0f));
    };
    auto centered = [&](const char* text, float size, float y, ImU32 rgb, float a) {
        ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0, text);
        ImVec2 pos((ds.x - sz.x) * 0.5f, y - sz.y * 0.5f);
        const ImU32 shadow = IM_COL32(0, 0, 0, (int)(220 * a));
        dl->AddText(font, size, ImVec2(pos.x + 3, pos.y + 3), shadow, text);
        dl->AddText(font, size, pos, (rgb & 0x00FFFFFF) | ((ImU32)(255 * a) << 24), text);
    };
    const float scale = std::fmax(0.6f, ds.y / 480.0f);
    const float dim = std::fmin(1.0f, std::fmin(t / 150.0f, (3500 - t) / 250.0f));
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, (int)(170 * dim)));

    if (float a = fade(0, 1000))
        centered("Ruhsuz adam, Undertale'e bile hile ha?", 30 * scale, ds.y * 0.5f, IM_COL32(255, 60, 60, 0), a);
    if (float a = fade(1000, 3500)) {
        centered("kmertkun", 52 * scale, ds.y * 0.46f, IM_COL32(255, 220, 60, 0), a);
        centered("github.com/kmertkun", 18 * scale, ds.y * 0.46f + 42 * scale, IM_COL32(230, 230, 230, 0), a);
    }
}

HRESULT WINAPI hkPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) {
    if (!gImguiReady) initImgui(dev);

    if (gm::ready()) tickGame();  // yukleme ekraninda oyun degiskenleri henuz yok
    if (std::fabs(g.speed - gAppliedSpeed) > 1e-4) setSpeed(g.speed);

    if (gImguiReady) {
        IDirect3DSurface9* bb = nullptr;
        IDirect3DSurface9* oldRt = nullptr;
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
            D3DSURFACE_DESC d; bb->GetDesc(&d);
            gBbW = d.Width; gBbH = d.Height;
            dev->GetRenderTarget(0, &oldRt);
            dev->SetRenderTarget(0, bb);
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        if (gBbW && gBbH) io.DisplaySize = ImVec2((float)gBbW, (float)gBbH);
        io.MouseDrawCursor = g.menuOpen;
        ImGui::NewFrame();
        if (g.dodge.showBoxes && gBbW && gBbH) {
            // Oyun 640x480 goruntuyu en-boy oranini koruyarak ortalar (tam ekranda yanlarda siyah bant).
            float sc = std::fmin(gBbW / 640.0f, gBbH / 480.0f);
            dodge::drawDebug(ImGui::GetBackgroundDrawList(), sc, (gBbW - 640 * sc) * 0.5f, (gBbH - 480 * sc) * 0.5f);
        }
        if (g.menuOpen) drawMenu();
        drawSplash();
        if (!g.menuOpen) {
            // Menu kapaliyken aktif hileleri kucuk bir etiketle goster.
            char tag[48] = "";
            if (g.god) strcat_s(tag, "GOD ");
            if (g.dodge.enabled) strcat_s(tag, "DODGE ");
            if (std::fabs(g.speed - 1.0f) > 1e-4) strcat_s(tag, "HIZ");
            if (tag[0]) ImGui::GetForegroundDrawList()->AddText(ImVec2(6, 4), IM_COL32(255, 220, 60, 230), tag);
        }
        ImGui::EndFrame();
        if (SUCCEEDED(dev->BeginScene())) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            dev->EndScene();
        }

        if (bb) {
            if (oldRt) { dev->SetRenderTarget(0, oldRt); oldRt->Release(); }
            bb->Release();
        }
    }
    return oPresent(dev, src, dst, wnd, dirty);
}

HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (gImguiReady) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT r = oReset(dev, pp);
    if (gImguiReady && SUCCEEDED(r)) ImGui_ImplDX9_CreateDeviceObjects();
    return r;
}

// Gecici bir D3D9 cihazi olusturup vtable'dan Present/Reset adreslerini alir.
bool getD3D9Functions(void*& present, void*& reset) {
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"UTCheatDummy";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = w;
    IDirect3DDevice9* dev = nullptr;
    HRESULT r = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev);
    bool ok = SUCCEEDED(r) && dev;
    if (ok) {
        void** vt = *(void***)dev;
        reset = vt[16];
        present = vt[17];
        dev->Release();
    }
    d3d->Release();
    DestroyWindow(w);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

template <class T>
bool hook(void* target, void* detour, T& original) {
    return MH_CreateHook(target, detour, (void**)&original) == MH_OK && MH_EnableHook(target) == MH_OK;
}

DWORD WINAPI initThread(LPVOID) {
    HMODULE exe = GetModuleHandleW(nullptr);
    auto* dos = (IMAGE_DOS_HEADER*)exe;
    auto* nt = (IMAGE_NT_HEADERS*)((BYTE*)exe + dos->e_lfanew);
    if ((uintptr_t)exe != 0x400000 || nt->OptionalHeader.SizeOfImage < 0x400000 || !gm::versionOk()) {
        MessageBoxW(nullptr, L"Bu UNDERTALE.exe sürümü desteklenmiyor (Steam sürümü bekleniyordu).\nHile menüsü yüklenmedi.",
            L"UTCheat", MB_ICONWARNING);
        return 0;
    }

    InitializeCriticalSection(&gTimeLock);
    if (MH_Initialize() != MH_OK) return 0;

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE winmm = LoadLibraryW(L"winmm.dll");
    // Once gercek fonksiyon isaretcilerini al, sonra kancala (setSpeed bunlari cagirir).
    oQPC = (QPC_t)GetProcAddress(k32, "QueryPerformanceCounter");
    oGetTickCount = (GTC_t)GetProcAddress(k32, "GetTickCount");
    oGetTickCount64 = (GTC64_t)GetProcAddress(k32, "GetTickCount64");
    oTimeGetTime = (GTC_t)GetProcAddress(winmm, "timeGetTime");
    hook((void*)oQPC, (void*)hkQPC, oQPC);
    hook((void*)oGetTickCount, (void*)hkGetTickCount, oGetTickCount);
    hook((void*)oGetTickCount64, (void*)hkGetTickCount64, oGetTickCount64);
    hook((void*)oTimeGetTime, (void*)hkTimeGetTime, oTimeGetTime);

    hook((void*)gm::ADDR_SCRIPT_EXECUTE, (void*)hkScriptExecute, oScriptExecute);
    gInstCreateHooked = hook((void*)gm::ADDR_INSTANCE_CREATE, (void*)hkInstanceCreate, oInstanceCreate);
    if (!gInstCreateHooked)
        ulog::write("UYARI: instance_create kancasi kurulamadi (soykirim sonu kapali)");
    if (!vinput::installHooks()) ulog::write("UYARI: klavye kancalari kurulamadi (mavi ruh auto-dodge calismaz)");

    void *present = nullptr, *reset = nullptr;
    if (!getD3D9Functions(present, reset)) {
        MessageBoxW(nullptr, L"Direct3D 9 kancası kurulamadı.", L"UTCheat", MB_ICONERROR);
        return 0;
    }
    hook(reset, (void*)hkReset, oReset);
    hook(present, (void*)hkPresent, oPresent);
    ulog::write("UTCheat yuklendi, kancalar kuruldu");
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        ulog::open(inst);
        if (HANDLE t = CreateThread(nullptr, 0, initThread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
