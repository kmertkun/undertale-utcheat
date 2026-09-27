// Auto-dodge: mermi kutularini okuyup hizlarini tahmin eder, kalp tehlikedeyse savas kutusu icindeki
// en yakin guvenli noktaya tasir.
#include "autododge.h"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <string>
#include <queue>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "gm.h"
#include "hazards.h"
#include "log.h"
#include "input.h"

namespace dodge {
namespace {

struct Box { float l, t, r, b; };
struct Hazard {
    Box box; float vx, vy; int obj;
    // Gaster Blaster isini: (bx,by)'den (dx,dy) yonunde uzanan, yari genisligi hw olan kalin isin.
    bool beam = false; float bx = 0, by = 0, dx = 0, dy = 0, hw = 0;
    int from = 0;  // mermi/isin kac kare sonra tehlikeli olacak (0 = su an)
};

// Carpismasi bbox ile degil, Draw/Step kodunda ozel dikdortgen/cizgiyle yapilan mermiler (obje indeksleri data.win'den).
constexpr int OBJ_GASTERBLASTER = 499, OBJ_SANS_BONEBUL = 500, OBJ_COOLBUS = 639, OBJ_SIZEBONE = 653,
              OBJ_TOPBONE = 654, OBJ_BLUELASER_B = 679, OBJ_BONESTAB = 503;

enum class Mode { Red, Blue, Purple };
struct Pt { float x, y; int line; };  // line: mor ruhta iplik numarasi (yno)

const char* modeName(Mode m) { return m == Mode::Red ? "kirmizi" : m == Mode::Blue ? "mavi" : "mor"; }

bool gInit = false;
bool gIsHazard[gm::OBJECT_COUNT];
bool gIsSpear[gm::OBJECT_COUNT];
int vX, vY, vObj, vBL, vBR, vBT, vBB, vHSpd, vVSpd, vAngle, vXScale, vAlarm;
double gIb[4];  // global.idealborder: sol, sag, ust, alt
std::vector<Pt> gCands;
int gNx = 0, gNy = 0;       // aday izgarasi: cands[iy * gNx + ix]
float gCellH = 2;           // izgara satir araligi (mor ruhta iplikler arasi)

std::vector<Hazard> gHazards;
std::vector<Hazard> gSpears;   // yesil ruhta kalkanla engellenen mizraklar
std::unordered_map<void*, Box> gPrev, gCur;
Box gHeart{}, gArena{};
bool gHaveHeart = false, gHaveArena = false;

// Log icin tur/savas takibi
enum class Phase { None, Menu, Enemy };
Phase gPhase = Phase::None;
unsigned gFrame = 0;
double gLastHp = -1;
int gTurn = 0, gTurnDodges = 0, gTurnHits = 0, gTurnDamage = 0, gTurnMaxHazards = 0;
int gTotalDodges = 0, gTotalHits = 0, gTotalDamage = 0;

void endTurn() {
    if (gPhase == Phase::Enemy)
        ulog::write("TUR %d BITTI: kacis=%d, hasar alinan vurus=%d (-%d HP), en fazla mermi=%d",
                    gTurn, gTurnDodges, gTurnHits, gTurnDamage, gTurnMaxHazards);
}

void setPhase(Phase p, double hp) {
    if (p == gPhase) return;
    endTurn();
    if (p == Phase::Enemy) {
        ++gTurn;
        gTurnDodges = gTurnHits = gTurnDamage = gTurnMaxHazards = 0;
        ulog::write("TUR %d: dusman saldiriyor (HP %.0f)", gTurn, hp);
    } else if (p == Phase::None && gPhase != Phase::None) {
        ulog::write("SAVAS BITTI: toplam kacis=%d, vurus=%d, toplam hasar=%d", gTotalDodges, gTotalHits, gTotalDamage);
    } else if (p != Phase::None && gPhase == Phase::None) {
        gTurn = 0; gTotalDodges = gTotalHits = gTotalDamage = 0;
        double bg = -1; gm::readGlobal("battlegroup", bg);
        ulog::write("SAVAS BASLADI: battlegroup=%.0f, HP %.0f", bg, hp);
    }
    gPhase = p;
}

// Kalbin sol ust kosesi (x, y) icin kabul edilen aralik ve kalbin bbox ofsetleri.
float gMinX, gMaxX, gMinY, gMaxY;
float gOffL, gOffT, gOffR, gOffB;

void init() {
    for (short o : gm::HAZARD_OBJECTS) gIsHazard[o] = true;
    for (short o : gm::SPEAR_OBJECTS) gIsSpear[o] = true;
    vHSpd = gm::findBuiltinVar("hspeed");
    vVSpd = gm::findBuiltinVar("vspeed");
    vAngle = gm::findBuiltinVar("image_angle");
    vXScale = gm::findBuiltinVar("image_xscale");
    vAlarm = gm::findBuiltinVar("alarm");
    vX = gm::findBuiltinVar("x");
    vY = gm::findBuiltinVar("y");
    vObj = gm::findBuiltinVar("object_index");
    vBL = gm::findBuiltinVar("bbox_left");
    vBR = gm::findBuiltinVar("bbox_right");
    vBT = gm::findBuiltinVar("bbox_top");
    vBB = gm::findBuiltinVar("bbox_bottom");
    gInit = true;
}

bool readBox(void* inst, Box& out) {
    double l, t, r, b;
    if (!gm::readInstance(inst, vBL, l) || !gm::readInstance(inst, vBR, r) ||
        !gm::readInstance(inst, vBT, t) || !gm::readInstance(inst, vBB, b))
        return false;
    if (r < l || b < t) return false;
    out = { (float)l, (float)t, (float)r + 1, (float)b + 1 };  // GameMaker bbox'i kapsayici; sag/alt +1
    return true;
}

bool overlaps(const Box& a, const Box& b) {
    return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b;
}

// Kutu ile kalin isin arasindaki bosluk (< 0 ise degiyor). Isin boyunca kutunun isin normaline izdusumu kullanilir.
float beamDistance(const Hazard& z, const Box& h) {
    float cx = (h.l + h.r) * 0.5f, cy = (h.t + h.b) * 0.5f;
    float ox = cx - z.bx, oy = cy - z.by;
    float t = ox * z.dx + oy * z.dy;
    if (t < 0) {  // isinin arkasi: baslangic noktasina uzaklik (kutu cevreleyen daireyle)
        float r = 0.5f * std::sqrt((h.r - h.l) * (h.r - h.l) + (h.b - h.t) * (h.b - h.t));
        return std::sqrt(ox * ox + oy * oy) - z.hw - r;
    }
    float r = 0.5f * ((h.r - h.l) * std::fabs(z.dy) + (h.b - h.t) * std::fabs(z.dx));
    return std::fabs(ox * z.dy - oy * z.dx) - z.hw - r;
}

// (px, py) kalbin sol ust kosesi olursa onumuzdeki `frames` karede kac mermiye degiyor.
int hits(float px, float py, float margin, int frames, bool stopAtFirst) {
    Box h{ px + gOffL - margin, py + gOffT - margin, px + gOffR + margin, py + gOffB + margin };
    int n = 0;
    for (const Hazard& z : gHazards) {
        if (z.beam) {
            if (z.from <= frames && beamDistance(z, h) < 0) { ++n; if (stopAtFirst) return n; }
            continue;
        }
        for (int t = z.from; t <= frames; ++t) {
            Box m{ z.box.l + z.vx * t, z.box.t + z.vy * t, z.box.r + z.vx * t, z.box.b + z.vy * t };
            if (overlaps(h, m)) {
                ++n;
                if (stopAtFirst) return n;
                break;
            }
        }
    }
    return n;
}

bool (*gIgnore)(void*) = nullptr;

void collectHazards() {
    gHazards.clear();
    gSpears.clear();
    gCur.clear();
    for (int i = 0; i < 4; ++i) gm::readGlobal("idealborder", gIb[i], i);
    gm::forEachInstance([](void* inst) {
        double obj;
        if (!gm::readInstance(inst, vObj, obj)) return;
        int o = (int)obj;
        if (o < 0 || o >= gm::OBJECT_COUNT || !(gIsHazard[o] || gIsSpear[o])) return;
        if (gIgnore && gIgnore(inst)) return;
        Box b;
        if (!readBox(inst, b)) return;
        double ix = 0, iy = 0;
        gm::readInstance(inst, vX, ix);
        gm::readInstance(inst, vY, iy);
        const float x = (float)ix, y = (float)iy, ibl = (float)gIb[0], ibr = (float)gIb[1], ibt = (float)gIb[2], ibb = (float)gIb[3];
        switch (o) {
        case OBJ_SIZEBONE:  b = { x + 3, y + 2, x + 10, ibb - 2 }; break;              // yerden y'ye uzanan kemik
        case OBJ_TOPBONE:   b = { x + 3, std::fmin(y, ibt + 10), x + 10, std::fmax(y, ibt + 10) }; break;
        case OBJ_BLUELASER_B: b = { x, ibt + 2, x + 6, ibb }; break;
        case OBJ_COOLBUS:   b = { x + 5, y + 10, x + 56, ibb - 10 }; break;
        case OBJ_SANS_BONEBUL: {
            double type = 0;
            gm::readInstanceVar(inst, "type", type);
            if ((int)type == 2) b = { x + 2, std::fmin(ibt + 11, y), x + 9, std::fmax(ibt + 11, y) };
            else if ((int)type == 0 || (int)type == 1) b = { x + 2, y + 5, x + 9, ibb - 6 };
            break;
        }
        case OBJ_BONESTAB: {
            // Sans'in yerden/tavandan cikan kemik duvari: once `warning` kare kirmizi uyari dikdortgeni cizer,
            // sonra kemik tam o bolgede yukselir. Uyari suresince bolgeyi `warning` kare sonra tehlikeli say.
            double active = 0, warning = 0, dir = 0, height = 25;
            gm::readInstanceVar(inst, "active", active);
            gm::readInstanceVar(inst, "warning", warning);
            gm::readInstanceVar(inst, "dir", dir);
            gm::readInstanceVar(inst, "height", height);
            if (active < 1) return;
            const float h = (float)height;
            Box w{};
            switch ((int)dir) {
            case 0: w = { ibl + 8, ibb - h, ibr - 3, ibb - 3 }; break;
            case 1: w = { ibr - h, ibt + 8, ibr - 3, ibb - 3 }; break;
            case 2: w = { ibl + 8, ibt + 6, ibr - 3, ibt + 5 + h }; break;
            default: w = { ibl + 8, ibt + 8, ibl + 5 + h, ibb - 3 }; break;
            }
            Hazard z{ w, 0, 0, o };
            z.from = warning > 0 ? (int)warning : 0;
            gHazards.push_back(z);
            return;
        }
        case OBJ_GASTERBLASTER: {
            // Blaster once (idealx, idealy)'ye ucup idealrot'a doner (con 1/3), sonra alarm[4] (= pause) kare bekler
            // (con 4), con 5 -> con 6'da alarm[4] = 4 kare agzini acar ve con 7'de ates eder; ates ederken kendi isin
            // ekseninde geri kactigi icin isinin cizgisi bastan bellidir. Hasar penceresi: con 7 ve fade >= 0.8.
            // `from` = ates etmesine kalan kare; hits() isini sadece bakilan zaman araligi o kareye ulasiyorsa sayar,
            // boylece sonra ates edecek isinlar simdiki yolu tikamaz ama hedef seciminde hesaba katilir.
            double con = 0, fade = 1, xs = 1, ix2 = x, iy2 = y, rot = 0;
            gm::readInstanceVar(inst, "con", con);
            gm::readInstanceVar(inst, "fade", fade);
            gm::readInstanceVar(inst, "idealx", ix2);
            gm::readInstanceVar(inst, "idealy", iy2);
            gm::readInstanceVar(inst, "idealrot", rot);
            gm::readInstance(inst, vXScale, xs);
            if (con < 4 || fade < 0.8) return;
            gm::RValue al{}; al.kind = 5;
            gm::getInstanceVar(inst, vAlarm, 4, &al);
            double alarm4 = 0;
            gm::toDouble(al, alarm4);
            if (alarm4 < 0) alarm4 = 0;
            float a = (float)((rot - 90) * 3.14159265358979 / 180.0);
            Hazard z{ b, 0, 0, o };
            z.beam = true;
            z.dx = std::cos(a); z.dy = -std::sin(a);                 // GameMaker lengthdir: y ekseni asagi
            z.bx = (float)ix2; z.by = (float)iy2;
            // Oyun isini merkez cizgiden +-bt/8, bt/4, 3bt/8 uzakliktaki cizgilerle kontrol eder -> etkin yari genislik 3bt/8.
            z.hw = 0.375f * (float)(4 * std::floor(35 * xs / 4)) + 1;
            z.from = con < 5 ? (int)alarm4 + 5 : con < 6 ? 5 : con < 7 ? (int)alarm4 + 1 : 0;
            gHazards.push_back(z);
            return;
        }
        default:
            // Tum savas alanini kaplayan dev kutular (gorunmez kontrolculer) kacisi imkansiz kilar, atla.
            if (b.r - b.l > 400 && b.b - b.t > 300) return;
        }
        float vx = 0, vy = 0;
        auto it = gPrev.find(inst);
        if (it != gPrev.end()) {
            vx = b.l - it->second.l;
            vy = b.t - it->second.t;
            if (std::fabs(vx) > 40 || std::fabs(vy) > 40) vx = vy = 0;  // isinlanan mermi: hiz tahmini anlamsiz
        }
        gCur[inst] = b;
        (gIsSpear[o] ? gSpears : gHazards).push_back({ b, vx, vy, o });
    });
    gPrev.swap(gCur);
}

void logHit(double before, double hp, double hx, double hy) {
    int dmg = (int)(before - hp);
    ++gTurnHits; ++gTotalHits; gTurnDamage += dmg; gTotalDamage += dmg;
    ulog::write("  HASAR -%d (HP %.0f->%.0f) kalp=(%.0f,%.0f) kutusu=[%.0f,%.0f - %.0f,%.0f], mermi=%d",
                dmg, before, hp, hx, hy, gHeart.l, gHeart.t, gHeart.r, gHeart.b, (int)gHazards.size());
    // En yakin 3 mermi (kutular arasi bosluk; 0 = degiyor)
    std::vector<std::pair<float, const Hazard*>> closest;
    for (const Hazard& z : gHazards) {
        if (z.beam) { closest.push_back({ std::fmax(0.f, beamDistance(z, gHeart)), &z }); continue; }
        float dx = std::fmax(0.f, std::fmax(z.box.l - gHeart.r, gHeart.l - z.box.r));
        float dy = std::fmax(0.f, std::fmax(z.box.t - gHeart.b, gHeart.t - z.box.b));
        closest.push_back({ std::sqrt(dx * dx + dy * dy), &z });
    }
    std::sort(closest.begin(), closest.end(), [](auto& a, auto& b) { return a.first < b.first; });
    if (closest.empty() || closest[0].first > 2)
        ulog::write("    temas yok: KR (karma) zehri ya da listede olmayan bir mermi");
    for (size_t i = 0; i < closest.size() && i < 3; ++i) {
        const Hazard& z = *closest[i].second;
        ulog::write("    obj%d kutu=[%.0f,%.0f - %.0f,%.0f] hiz=(%.1f,%.1f) uzaklik=%.1f",
                    z.obj, z.box.l, z.box.t, z.box.r, z.box.b, z.vx, z.vy, closest[i].first);
    }
}

// Yesil ruh: kalkan (obj_spearblocker) Draw eventinde idealdir yonundeki 30 px'lik cizgiyle carpisan
// mizraklari engeller. idealdir: 270 ust, 90 alt, 0 sol, 180 sag. En yakin mizragin bir sonraki karedeki
// konumuna gore kalkani cevir.
void autoBlock(void* blocker, Status& st) {
    double bx, by;
    if (!gm::readInstance(blocker, vX, bx) || !gm::readInstance(blocker, vY, by)) { st.state = "Kalkan okunamadı"; return; }
    static int lastTurnLogged = -1;
    if (lastTurnLogged != gTurn) { ulog::write("  auto-block aktif (yesil ruh), kalkan=(%.0f,%.0f)", bx, by); lastTurnLogged = gTurn; }
    st.hazards = (int)gSpears.size();
    const Hazard* best = nullptr;
    float bestD = 1e30f, bdx = 0, bdy = 0;
    for (const Hazard& z : gSpears) {
        float cx = (z.box.l + z.box.r) * 0.5f + z.vx, cy = (z.box.t + z.box.b) * 0.5f + z.vy;
        float dx = cx - (float)bx, dy = cy - (float)by, d = std::sqrt(dx * dx + dy * dy);
        if (d < bestD) { bestD = d; best = &z; bdx = dx; bdy = dy; }
    }
    if (!best) { st.state = "Kalkan: mızrak yok"; return; }
    int dir = std::fabs(bdx) > std::fabs(bdy) ? (bdx < 0 ? 0 : 180) : (bdy < 0 ? 270 : 90);
    double cur = -1;
    gm::readInstanceVar(blocker, "idealdir", cur);
    if ((int)cur != dir) {
        gm::writeInstanceVar(blocker, "idealdir", dir);
        ++gTurnDodges; ++gTotalDodges; ++st.dodges;
        auto name = [](int d) { return d == 0 ? "sol" : d == 90 ? "alt" : d == 180 ? "sag" : d == 270 ? "ust" : "?"; };
        ulog::write("  KALKAN f%u: %s -> %s (mizrak obj%d uzaklik=%.0f)", gFrame, name((int)cur), name(dir), best->obj, bestD);
    }
    st.state = "Kalkan otomatik";
}

void applyMove(Mode mode, void* player, void* heart, const Pt& to, double hx, double hy) {
    gm::writeInstance(player, vX, to.x);
    gm::writeInstance(player, vY, to.y);
    if (mode == Mode::Purple) {
        gm::writeInstanceVar(player, "yno", to.line);
        gm::writeInstanceVar(player, "space", 0);
    } else if (mode == Mode::Blue) {
        // Yercekimi ekseninde yer degistiyse kalbi "havada, hizsiz" yap: yere dogal sekilde duser.
        double mv = 2; gm::readInstanceVar(heart, "movement", mv);
        bool vertical = (int)mv == 2 || (int)mv == 12;
        bool moved = vertical ? std::fabs(to.y - hy) > 0.5 : std::fabs(to.x - hx) > 0.5;
        if (moved) {
            gm::writeInstanceVar(heart, "jumpstage", 2);
            gm::writeInstance(heart, vertical ? vVSpd : vHSpd, 0);
        }
    }
}

// Dogal hareket: kalp mermilerin icinden gecmeden, her karede en fazla `speed` piksel yurur.
// Izgara uzerinde BFS: su an ya da bir sonraki karede mermiye degen hucreler duvar sayilir. Hedef, T kare boyunca
// guvenli olan en yakin (yol uzunlugu + kenar/rahatlik cezasi en dusuk) hucre. Donus: bu karede gidilecek hucre.
const Pt* planStep(Mode mode, float hx, float hy, float m, int T, int speed,
                   const Pt*& target, int& pathLen, bool& stuck) {
    const int nx = gNx, ny = gNy, n = nx * ny;
    if (n <= 0) return nullptr;
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    int sx = clampi((int)std::lround((hx - gMinX) / 2), 0, nx - 1);
    int sy = clampi((int)std::lround((hy - gMinY) / gCellH), 0, ny - 1);
    int start = sy * nx + sx;

    static std::vector<int> dist, parent;
    static std::vector<signed char> blocked;  // -1 bilinmiyor, 0 acik, 1 mermili
    static std::vector<char> done;
    dist.assign(n, -1); parent.assign(n, -1); blocked.assign(n, -1); done.assign(n, 0);
    constexpr int BLOCKED_COST = 40;  // mermili hucreden gecmek: sadece baska cikis yoksa (zaten icindeyken)
    auto isBlocked = [&](int i) {
        if (blocked[i] < 0) blocked[i] = hits(gCands[i].x, gCands[i].y, 1, 1, true) ? 1 : 0;
        return blocked[i] == 1;
    };

    constexpr float EDGE_PENALTY = 15, NOT_ROOMY_PENALTY = 25, EDGE = 6;
    int bestT = -1, firstSafeDepth = -1;
    float bestCost = 1e30f;
    using QE = std::pair<int, int>;  // (maliyet, hucre)
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    dist[start] = 0; q.push({ 0, start });
    while (!q.empty()) {
        int c = q.top().second; q.pop();
        if (done[c]) continue;
        done[c] = 1;
        if (firstSafeDepth >= 0 && dist[c] > firstSafeDepth + 6) break;  // yeterince aradik
        const Pt& pc = gCands[c];
        if (!hits(pc.x, pc.y, m, T, true)) {
            if (firstSafeDepth < 0) firstSafeDepth = dist[c];
            float cost = (float)dist[c] * 2;
            if (blocked[c] == 1) cost += 1000;  // mermili hucrede durmak hedef olamaz
            bool edgeX = pc.x - gMinX < EDGE || gMaxX - pc.x < EDGE;
            bool edgeY = mode != Mode::Purple && (pc.y - gMinY < EDGE || gMaxY - pc.y < EDGE);
            if (edgeX || edgeY) cost += EDGE_PENALTY;
            if (cost < bestCost && hits(pc.x, pc.y, m + 6, T + 4, true)) cost += NOT_ROOMY_PENALTY;
            if (cost < bestCost) { bestCost = cost; bestT = c; }
        }
        int cx = c % nx, cy = c / nx;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                if (mode == Mode::Purple && dy && dx) continue;  // mor ruh: iplik degistirirken x sabit
                int x2 = cx + dx, y2 = cy + dy;
                if (x2 < 0 || y2 < 0 || x2 >= nx || y2 >= ny) continue;
                int j = y2 * nx + x2;
                if (done[j]) continue;
                int nd = dist[c] + (isBlocked(j) ? BLOCKED_COST : 1);
                if (dist[j] >= 0 && dist[j] <= nd) continue;
                dist[j] = nd; parent[j] = c; q.push({ nd, j });
            }
    }
    stuck = bestT < 0;
    if (stuck) {
        // Guvenli hedefe yol yok: ulasilabilen hucreler arasindan en az mermiye degeni sec.
        int bestN = 1 << 30;
        for (int i = 0; i < n; ++i) {
            if (dist[i] < 0) continue;
            int k = hits(gCands[i].x, gCands[i].y, 1, 1, false);
            if (bestT < 0 || k < bestN || (k == bestN && dist[i] < dist[bestT])) { bestN = k; bestT = i; }
        }
        if (bestT < 0) return nullptr;
    }
    target = &gCands[bestT];

    // Yolu geri izle (baslangic -> hedef) ve bu karede gidilebilecek kadar ilerle.
    static std::vector<int> path;
    path.clear();
    for (int c = bestT; c >= 0; c = parent[c]) path.push_back(c);
    std::reverse(path.begin(), path.end());
    pathLen = (int)path.size() - 1;
    int steps = std::max(1, speed / 2);
    int k = std::min(pathLen, steps);
    if (mode == Mode::Purple)  // bir karede en fazla bir iplik degisimi
        for (int i = 1; i <= k; ++i)
            if (gCands[path[i]].line != gCands[path[i - 1]].line) { k = i; break; }
    return &gCands[path[k]];
}


// ---------------------------------------------------------------- mavi ruh: tus planlayici
// Mavi ruhta kalp yercekimine tabi; oyuncu sadece yana yuruyebilir ve yerdeyken ziplayabilir. Kalbin konumuna
// yazmak yerine oyunun fizigi (obj_heart Step / Keyboard eventleri) birebir simule edilir, ~200 tus plani H kare
// ileriye denenir ve en iyi planin bu karedeki tuslari sanal olarak basilir. Boylece kalp asla ucmaz.
struct BlueCfg { float gx, gy, px, py; int jumpKey, negKey, posKey; };

bool blueCfg(int mv, BlueCfg& c) {
    switch (mv) {
    case 2:  c = { 0, 1, 1, 0, VK_UP, VK_LEFT, VK_RIGHT }; return true;    // yercekimi asagi
    case 12: c = { 0, -1, 1, 0, VK_DOWN, VK_LEFT, VK_RIGHT }; return true; // yukari
    case 11: c = { 1, 0, 0, 1, VK_LEFT, VK_UP, VK_DOWN }; return true;     // saga
    case 13: c = { -1, 0, 0, 1, VK_RIGHT, VK_UP, VK_DOWN }; return true;   // sola
    default: return false;
    }
}

struct BlueSim { float x, y, a; int js; };  // a: yercekimi yonundeki hiz (ziplama -6)

void blueStep(BlueSim& s, const BlueCfg& c, int perp, bool jump, float sp) {
    s.x += c.px * perp * sp;
    s.y += c.py * perp * sp;
    if (jump && s.js == 1 && s.a == 0) { s.js = 2; s.a = -6; }
    if (s.js == 2) {
        if (!jump && s.a <= -1) s.a = -1;                      // tus birakilinca ziplama kisalir
        if (s.a > 0.5f && s.a < 8) s.a += 0.6f;
        else if (s.a > -1 && s.a <= 0.5f) s.a += 0.2f;
        else if (s.a > -4 && s.a <= -1) s.a += 0.5f;
        else if (s.a <= -4) s.a += 0.2f;
    }
    s.x += c.gx * s.a;
    s.y += c.gy * s.a;
    // Kutu sinirlari (obj_heart Step clamp'i): yercekimi yonundeki duvara degince yere oturur.
    const float xmin = (float)gIb[0] + 4, xmax = (float)gIb[1] - 16, ymin = (float)gIb[2] + 4, ymax = (float)gIb[3] - 16;
    auto land = [&](bool floorSide) { if (floorSide) { if (s.a > 0) s.a = 0; s.js = 1; } else if (s.a < 0) s.a = 0; };
    if (s.x < xmin) { s.x = xmin; land(c.gx < 0); }
    if (s.x > xmax) { s.x = xmax; land(c.gx > 0); }
    if (s.y < ymin) { s.y = ymin; land(c.gy < 0); }
    if (s.y > ymax) { s.y = ymax; land(c.gy > 0); }
}

// (x, y) konumundaki kalp t. karede bir mermiye degiyor mu; degmiyorsa en yakin mermiye bosluk (clear).
bool blueHit(float x, float y, int t, float margin, float& clear) {
    Box h{ x + gOffL - margin, y + gOffT - margin, x + gOffR + margin, y + gOffB + margin };
    for (const Hazard& z : gHazards) {
        if (t < z.from) continue;
        float gap;
        if (z.beam) gap = beamDistance(z, h);
        else {
            Box m{ z.box.l + z.vx * t, z.box.t + z.vy * t, z.box.r + z.vx * t, z.box.b + z.vy * t };
            float dx = std::fmax(m.l - h.r, h.l - m.r), dy = std::fmax(m.t - h.b, h.t - m.b);
            gap = std::fmax(dx, dy);
        }
        if (gap < 0) return true;
        if (gap < clear) clear = gap;
    }
    return false;
}

struct BluePlan { int perp1, perp2, switchAt, wait, hold; };

// Plani H kare simule eder: ilk carpisma karesi (H+1 = yok), en kucuk bosluk ve tus maliyeti.
int bluePlay(const BluePlan& p, BlueSim s, const BlueCfg& c, float sp, float margin, int H, float& clear, int& keys) {
    clear = 60; keys = 0;
    for (int t = 1; t <= H; ++t) {
        int perp = t <= p.switchAt ? p.perp1 : p.perp2;
        bool jump = p.hold > 0 && t > p.wait && t <= p.wait + p.hold;
        keys += (perp != 0) + jump;
        blueStep(s, c, perp, jump, sp);
        if (blueHit(s.x, s.y, t, margin, clear)) return t;
    }
    return H + 1;
}

int gBlueLastPerp = 0; bool gBlueLastJump = false;

void blueAI(void* heart, int mv, const Settings& set, Status& st) {
    BlueCfg c;
    if (!blueCfg(mv, c)) return;
    double hx, hy, hs = 0, vs = 0, js = 1, sp = 4;
    gm::readInstance(heart, vX, hx); gm::readInstance(heart, vY, hy);
    gm::readInstance(heart, vHSpd, hs); gm::readInstance(heart, vVSpd, vs);
    gm::readInstanceVar(heart, "jumpstage", js);
    gm::readGlobal("sp", sp);
    BlueSim s0{ (float)hx, (float)hy, (float)(c.gx != 0 ? hs * c.gx : vs * c.gy), (int)js };

    const int H = 32;
    const float margin = (float)std::min(set.margin, 2);
    static const int waits[] = { 0, 2, 4, 7, 11 };
    static const int holds[] = { 1, 4, 8, 14, 24 };  // 24 = tam ziplama (yukselis ~16 kare surer)
    BluePlan best{ 0, 0, 99, 0, 0 };
    float bestClear = 0; int bestKeys = 0;
    int bestHit = bluePlay(best, s0, c, (float)sp, margin, H, bestClear, bestKeys);  // bosta durmak
    const bool idleSafe = bestHit > H;
    if (!idleSafe) {
        float bestScore = 1e30f;
        for (int p1 = -1; p1 <= 1; ++p1)
            for (int p2 = -1; p2 <= 1; ++p2)
                for (int sw : { 6, 12, 99 }) {
                    if (sw == 99 && p2 != p1) continue;
                    for (int j = -1; j < 25; ++j) {
                        BluePlan p{ p1, p2, sw, j < 0 ? 0 : waits[j / 5], j < 0 ? 0 : holds[j % 5] };
                        float clear; int keys;
                        int hit = bluePlay(p, s0, c, (float)sp, margin, H, clear, keys);
                        // Once en gec carpisma; sonra genis bosluk ve az tus.
                        float score = (float)(H + 1 - hit) * 1000 - std::fmin(clear, 30) * 2 + keys * 0.5f;
                        if (score < bestScore) { bestScore = score; best = p; bestHit = hit; bestClear = clear; }
                    }
                }
    }
    int perp = best.perp1;
    bool jump = best.hold > 0 && best.wait == 0;
    vinput::set(c.negKey, perp < 0);
    vinput::set(c.posKey, perp > 0);
    vinput::set(c.jumpKey, jump);
    if (!idleSafe) {
        ++gTurnDodges; ++gTotalDodges; ++st.dodges;
        st.state = bestHit > H ? "Kaçıyor (tuşla)" : "Sıkıştı, en geç çarpışan plan";
        if (perp != gBlueLastPerp || jump != gBlueLastJump)
            ulog::write("  %s f%u [mavi mv=%d]: kalp=(%.0f,%.0f) a=%.1f js=%d -> tus: %s%s | plan yon=%d/%d@%d zipla=%d+%d carpisma=%s",
                        bestHit > H ? "KACIS" : "SIKISTI", gFrame, mv, hx, hy, s0.a, s0.js,
                        perp < 0 ? "geri " : perp > 0 ? "ileri " : "", jump ? "ZIPLA" : "",
                        best.perp1, best.perp2, best.switchAt, best.wait, best.hold,
                        bestHit > H ? "yok" : std::to_string(bestHit).c_str());
    } else {
        st.state = "Güvende";
    }
    gBlueLastPerp = perp; gBlueLastJump = jump;
}

} // namespace

void setIgnore(bool (*ignore)(void* inst)) { gIgnore = ignore; }

void tick(const Settings& s, Status& st) {
    if (!gInit) init();
    gHaveHeart = gHaveArena = false;
    st.hazards = 0;
    if (!s.enabled) {
        vinput::releaseAll();
        if (gPhase != Phase::None) { setPhase(Phase::None, gLastHp); ulog::write("auto-dodge kapatildi"); }
        gHazards.clear();
        gPrev.clear();
        st.state = "Kapalı";
        return;
    }

    ++gFrame;
    // Sanal tuslar sadece mavi ruh planlayicisi calisirken basili kalir; her kare basta birak, planlayici
    // gerekirse tekrar basar (degisiklik yoksa mesaj gonderilmez).
    const bool keysWereDown = vinput::anyDown();
    struct KeyGuard { bool used = false; ~KeyGuard() { if (!used) vinput::releaseAll(); } } keyGuard;
    (void)keysWereDown;
    void* heart = gm::findInstance(gm::OBJ_HEART, vObj);
    double mnfight = 0, movement = 0, hp = -1;
    gm::readGlobal("hp", hp);
    if (!heart) { setPhase(Phase::None, hp); st.state = "Savaşta değil"; gPrev.clear(); gHazards.clear(); gLastHp = hp; return; }
    if (!gm::readGlobal("mnfight", mnfight) || (int)mnfight != 2) {
        setPhase(Phase::Menu, hp);
        st.state = "Sıra sende (mermi yok)"; gPrev.clear(); gHazards.clear(); gLastHp = hp; return;
    }
    setPhase(Phase::Enemy, hp);

    collectHazards();
    st.hazards = (int)gHazards.size();
    if (st.hazards > gTurnMaxHazards) gTurnMaxHazards = st.hazards;

    // Hangi ruh modu? Yesil: kalkan objesi var. Mor: obj_purpleheart var (asil carpisan o). Digerleri obj_heart.
    void* blocker = gm::findInstance(gm::OBJ_SPEARBLOCKER, vObj);
    void* purple = blocker ? nullptr : gm::findInstance(gm::OBJ_PURPLEHEART, vObj);
    void* player = purple ? purple : heart;

    double hx, hy;
    if (!gm::readInstance(player, vX, hx) || !gm::readInstance(player, vY, hy) || !readBox(player, gHeart)) {
        st.state = "Kalp okunamadı"; return;
    }
    gHaveHeart = true;
    if (gLastHp >= 0 && hp >= 0 && hp < gLastHp) logHit(gLastHp, hp, hx, hy);
    gLastHp = hp;
    gOffL = gHeart.l - (float)hx; gOffT = gHeart.t - (float)hy;
    gOffR = gHeart.r - (float)hx; gOffB = gHeart.b - (float)hy;

    double ib[4];
    for (int i = 0; i < 4; ++i)
        if (!gm::readGlobal("idealborder", ib[i], i)) { st.state = "Savaş kutusu okunamadı"; return; }
    gArena = { (float)ib[0], (float)ib[2], (float)ib[1], (float)ib[3] };
    gHaveArena = true;

    if (blocker) { autoBlock(blocker, st); return; }

    Mode mode;
    std::vector<Pt>& cands = gCands;
    cands.clear();
    if (purple) {
        // Mor: kalp 1..yamt numarali ipliklerde, x ekseninde xmid +- xlen araliginda.
        double xmid, xlen, yzero, yspace, yamt, yoff, moving, type;
        if (!gm::readInstanceVar(purple, "xmid", xmid) || !gm::readInstanceVar(purple, "xlen", xlen) ||
            !gm::readInstanceVar(purple, "yzero", yzero) || !gm::readInstanceVar(purple, "yspace", yspace) ||
            !gm::readInstanceVar(purple, "yamt", yamt) || !gm::readInstanceVar(purple, "yoff", yoff) ||
            !gm::readInstanceVar(purple, "moving", moving) || !gm::readInstanceVar(purple, "type", type)) {
            st.state = "Mor ruh okunamadı"; return;
        }
        if ((int)type != 0) { st.state = "Mor ruh: bu saldırı desteklenmiyor"; return; }
        if ((int)moving != 0) { st.state = "Mor ruh: iplik değiştiriyor"; return; }
        mode = Mode::Purple;
        gMinX = (float)(xmid - xlen); gMaxX = (float)(xmid + xlen);
        gMinY = (float)(yzero + yoff); gMaxY = (float)(yzero + (yamt - 1) * yspace + yoff);
        for (int n = 1; n <= (int)yamt; ++n) {
            float y = (float)(yzero + (n - 1) * yspace + yoff);
            for (float x = gMinX; x <= gMaxX; x += 2) cands.push_back({ x, y, n });
        }
        gCellH = (float)yspace;
    } else {
        double movement = 0;
        gm::readInstanceVar(heart, "movement", movement);
        int mv = (int)movement;
        if (mv == 1) mode = Mode::Red;
        else if (mv == 2 || mv == 11 || mv == 12 || mv == 13) mode = Mode::Blue;
        else {
            static int lastMove = -999;
            if (mv != lastMove) { ulog::write("  ruh modu movement=%d, auto-dodge pasif", mv); lastMove = mv; }
            st.state = "Bu ruh modu desteklenmiyor";
            return;
        }
        if (mode == Mode::Blue && s.natural && s.blueJumpOnly) {
            static int lastBlueTurn = -1;
            if (lastBlueTurn != gTurn) { ulog::write("  auto-dodge aktif (mavi ruh, tusla oynuyor) movement=%d", mv); lastBlueTurn = gTurn; }
            blueAI(heart, mv, s, st);
            keyGuard.used = true;
            return;
        }
        // Kalbin savas kutusundaki sinirlari (obj_heart Step'teki clamp: [ib0+4, ib1-16] x [ib2+4, ib3-16])
        gMinX = (float)ib[0] + 5; gMaxX = (float)ib[1] - 17;
        gMinY = (float)ib[2] + 5; gMaxY = (float)ib[3] - 17;
        for (float y = gMinY; y <= gMaxY; y += 2)
            for (float x = gMinX; x <= gMaxX; x += 2) cands.push_back({ x, y, 0 });
        gCellH = 2;
    }
    gNx = 0;
    while (gNx < (int)cands.size() && cands[gNx].y == cands[0].y) ++gNx;
    gNy = gNx ? (int)cands.size() / gNx : 0;

    static int lastActiveTurn = -1;
    static Mode lastMode = Mode::Red;
    if (lastActiveTurn != gTurn || lastMode != mode) {
        ulog::write("  auto-dodge aktif (%s ruh), alan x[%.0f..%.0f] y[%.0f..%.0f]", modeName(mode), gMinX, gMaxX, gMinY, gMaxY);
        lastActiveTurn = gTurn; lastMode = mode;
    }
    if (cands.empty() || gMaxX < gMinX || gMaxY < gMinY) { st.state = "Kutu çok küçük"; return; }

    const float m = (float)s.margin;
    // Dogal harekette kalp sinirli hizla yurudugu icin tehlikeyi daha erken gormesi gerekir.
    int T = s.natural ? s.lookahead + 6 : s.lookahead;
    bool emergencyTp = false;
    if (!hits((float)hx, (float)hy, m, T, true)) { st.state = "Güvende"; return; }

    constexpr float NOT_ROOMY_PENALTY = 25, EDGE_PENALTY = 15, EDGE = 6;
    const Pt* best = nullptr;
    if (s.natural) {
        const Pt* target = nullptr;
        int pathLen = 0;
        bool stuck = false;
        best = planStep(mode, (float)hx, (float)hy, m, T, s.speed, target, pathLen, stuck);
        // No-hit: yuruyerek kurtulamayacaksa (yol yok ya da bir sonraki karede vurulacak) bu kare isinlan.
        bool danger = !best || stuck || hits(best->x, best->y, 0, 1, true);
        if (danger && s.emergency) {
            best = nullptr;
            T = s.lookahead;
            emergencyTp = true;
        }
        if (!emergencyTp) {
            if (!best) { st.state = "Kapalı kaldı, yol yok"; return; }
            st.state = stuck ? "Sıkıştı, en az hasarlı yere yürüyor" : "Kaçıyor";
            ulog::write("  %s f%u [%s]: (%.0f,%.0f)->(%.0f,%.0f) hedef=(%.0f,%.0f) yol=%d adim mermi=%d",
                        stuck ? "SIKISTI" : "KACIS", gFrame, modeName(mode), hx, hy, best->x, best->y,
                        target->x, target->y, pathLen, st.hazards);
            ++gTurnDodges; ++gTotalDodges; ++st.dodges;
            applyMove(mode, player, heart, *best, hx, hy);
            return;
        }
    }
    // Isinlanma: guvenli noktalar arasindan maliyeti en dusuk olan: mesafe + (rahat degilse ceza) + (kenara yapisiksa ceza).
    // "Rahat" = daha genis pay ve daha uzun tahminle de guvenli.
    float bestD = 1e30f, bestCost = 1e30f;
    for (const Pt& c : cands) {
        float d = std::sqrt((c.x - (float)hx) * (c.x - (float)hx) + (c.y - (float)hy) * (c.y - (float)hy));
        if (d >= bestCost) continue;  // cezalar >= 0, daha iyisi olamaz
        if (hits(c.x, c.y, m, T, true)) continue;
        float cost = d;
        bool edgeX = c.x - gMinX < EDGE || gMaxX - c.x < EDGE;
        bool edgeY = mode != Mode::Purple && (c.y - gMinY < EDGE || gMaxY - c.y < EDGE);
        if (edgeX || edgeY) cost += EDGE_PENALTY;
        if (cost < bestCost && hits(c.x, c.y, m + 6, T + 4, true)) cost += NOT_ROOMY_PENALTY;
        if (cost < bestCost) { bestCost = cost; bestD = d; best = &c; }
    }
    if (!best) {
        // Tamamen guvenli yer yok: payi dusurup en az mermiye degen noktayi sec.
        int bestN = 1 << 30;
        for (size_t i = 0; i < cands.size(); i += 2) {
            const Pt& c = cands[i];
            int n = hits(c.x, c.y, 1, 1, false);
            float d = std::sqrt((c.x - (float)hx) * (c.x - (float)hx) + (c.y - (float)hy) * (c.y - (float)hy));
            if (n < bestN || (n == bestN && d < bestD)) { bestN = n; bestD = d; best = &c; }
        }
        st.state = "Sıkıştı, en az hasarlı yere kaçtı";
        ulog::write("  SIKISTI f%u [%s]: (%.0f,%.0f)->(%.0f,%.0f) mermi=%d, degen=%d", gFrame, modeName(mode), hx, hy, best->x, best->y, st.hazards, bestN);
    } else {
        st.state = emergencyTp ? "Acil ışınlandı" : "Kaçtı!";
        ulog::write(emergencyTp ? "  KACIS-ACIL f%u [%s]: isinlanma " : "  KACIS f%u [%s]: (%.0f,%.0f)->(%.0f,%.0f) mesafe=%.0f mermi=%d", gFrame, modeName(mode), hx, hy, best->x, best->y, bestD, st.hazards);
    }
    ++gTurnDodges; ++gTotalDodges; ++st.dodges;
    applyMove(mode, player, heart, *best, hx, hy);
}

void drawDebug(ImDrawList* dl, float sc, float ox, float oy) {
    auto P = [&](float x, float y) { return ImVec2(x * sc + ox, y * sc + oy); };
    auto rect = [&](const Box& b, ImU32 col) { dl->AddRect(P(b.l, b.t), P(b.r, b.b), col, 0, 0, 1.5f); };
    if (gHaveArena) rect(gArena, IM_COL32(255, 220, 60, 160));
    for (const Hazard& z : gHazards) {
        if (z.beam) {
            dl->AddLine(P(z.bx, z.by), P(z.bx + z.dx * 1000, z.by + z.dy * 1000), IM_COL32(255, 60, 60, 90), 2 * z.hw * sc);
            continue;
        }
        rect(z.box, IM_COL32(255, 60, 60, 220));
        if (z.vx != 0 || z.vy != 0) {
            float cx = (z.box.l + z.box.r) * 0.5f, cy = (z.box.t + z.box.b) * 0.5f;
            dl->AddLine(P(cx, cy), P(cx + z.vx * 4, cy + z.vy * 4), IM_COL32(255, 140, 60, 220));
        }
    }
    if (gHaveHeart) rect(gHeart, IM_COL32(60, 255, 120, 230));
}

} // namespace dodge
