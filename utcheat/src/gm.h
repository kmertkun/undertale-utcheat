// UNDERTALE.exe (Steam, GameMaker Studio 1.4 runner, ASLR yok, base 0x400000) ic fonksiyonlari.
// Adresler exe'nin kendi Function_Add / Variable_BuiltIn_Add kayit kodundan cikarildi.
#pragma once
#include <cstdint>
#include <cstring>

namespace gm {

struct RValue {
    union { double real; int32_t i32; int64_t i64; void* ptr; };
    int32_t flags;
    int32_t kind;   // alt 24 bit: 0 real, 1 string, 7 int32, 10 int64, 13 bool, 5 undefined
};
static_assert(sizeof(RValue) == 16, "RValue 16 bayt olmali");

enum : int32_t { KIND_REAL = 0, KIND_INT32 = 7, KIND_INT64 = 10, KIND_BOOL = 13 };
constexpr int ARRAY_NONE = (int)0x80000000;

// void F(RValue* result, CInstance* self, CInstance* other, int argc, RValue* args)
using Routine = void(__cdecl*)(RValue*, void*, void*, int, RValue*);

// Degisken adi -> slot. Ilk arguman global instance ya da herhangi bir instance olabilir.
using FindVarSlot      = int (__cdecl*)(void* inst, const char* name);               // yoksa -1
using GetGlobal        = void(__cdecl*)(int id, int arrayIndex, RValue* out);
using SetGlobal        = void(__cdecl*)(int id, int arrayIndex, RValue* in);
using FindBuiltinVar   = int (__cdecl*)(const char* name);                           // yoksa < 0
using GetInstanceVar   = void(__cdecl*)(void* inst, int id, int arrayIndex, RValue* out);
using SetInstanceVar   = void(__cdecl*)(void* inst, int id, int arrayIndex, RValue* in);

inline auto const findVarSlot    = (FindVarSlot)   0x0053F2B0;
inline auto const getGlobal      = (GetGlobal)     0x0040DCC0;
inline auto const setGlobal      = (SetGlobal)     0x0040DC40;
inline auto const findBuiltinVar = (FindBuiltinVar)0x0040F070;
inline auto const getInstanceVar = (GetInstanceVar)0x0040DE00;
inline auto const setInstanceVar = (SetInstanceVar)0x0040DE70;

inline void* const* const pGlobals      = (void* const*)0x0080894C;
inline uint8_t* const*    const pInstBuckets = (uint8_t* const*)0x007B54CC;  // id -> CInstance* hash map
inline const uint32_t*    const pInstMask    = (const uint32_t*)0x007B54D0;

constexpr uintptr_t ADDR_SCRIPT_EXECUTE = 0x0051C640;
constexpr uintptr_t ADDR_INSTANCE_CREATE = 0x004F2BE0;  // self/other kullanmiyor
constexpr uintptr_t ADDR_NAME_VGG       = 0x006EF280;  // "variable_global_get" string'i

// data.win (Steam 1.08+) asset indeksleri
constexpr int OBJ_HEART     = 744;
constexpr int OBJ_MAINCHARA = 1576;
constexpr int SCR_GAMEOVERB = 171;
constexpr int OBJ_BATTLER   = 143;
constexpr int OBJ_SANSB_BODY = 518;
constexpr int OBJ_SANSB      = 520;
constexpr int OBJ_DMGWRITER  = 190;  // vurus sayisini yazan obje (dmg)

// Sonlar
constexpr uintptr_t ADDR_ROOM_GOTO    = 0x004F3140;  // room_goto(room)
constexpr uintptr_t ADDR_GAME_RESTART = 0x004F32A0;  // game_restart()
constexpr int OBJ_TRUECHARA = 497;
constexpr int OBJ_GAMESHAKE = 496;  // soykirim sonu: kayitlari siler, Steam Cloud'a 962 yazar
constexpr int OBJ_CREDITS_SHORT = 138;       // notr jenerik; bitince alarm[6] -> telefon (obj_mainend)
constexpr int OBJ_OUTSIDEWORLD_EVENT = 1310; // pasifist gun batimi; do_room_goto -> room_end_castroll
constexpr int ROOM_END_CASTROLL = 278, ROOM_CREDITSDODGER = 284;  // pasifist jenerik odalari (278..284)
constexpr int ROOM_END_MYROOM = 285, ROOM_END_THEEND = 286;
constexpr int ROOM_UNDERTALE_END = 239;  // notr: Sans'in telefonu + jenerik (obj_credits_short)
constexpr int ROOM_OUTSIDEWORLD  = 241;  // pasifist: gun batimi -> jenerik -> "THE END"
constexpr int ROOM_EMPTY         = 321;
constexpr int ROOM_EMPTYBLACK    = 323;  // obj_black_ender: game_restart
// Built-in "room" degiskeninin getter'i (0x401BB0) bu int'i okur.
inline int currentRoom() { return *(volatile int*)0x00A18EA0; }

// Cagiran: ana modulun 0x400000'da ve adresleri kapsayacak kadar buyuk oldugunu dogrulamis olmali.
inline bool versionOk() {
    return std::memcmp((const void*)ADDR_NAME_VGG, "variable_global_get", 20) == 0
        && *(const uint8_t*)ADDR_SCRIPT_EXECUTE != 0xCC;
}

inline bool toDouble(const RValue& v, double& out) {
    switch (v.kind & 0xFFFFFF) {
    case KIND_REAL:  out = v.real; return true;
    case KIND_INT32:
    case KIND_BOOL:  out = v.i32; return true;
    case KIND_INT64: out = (double)v.i64; return true;
    default: return false;
    }
}

// Sadece zaten var olan global degiskenleri okur/yazar (yeni degisken yaratmaz).
inline bool readGlobal(const char* name, double& out, int arrayIndex = ARRAY_NONE) {
    int slot = findVarSlot(*pGlobals, name);
    if (slot < 0) return false;
    RValue v{}; v.kind = 5;
    getGlobal(slot + 100000, arrayIndex, &v);
    return toDouble(v, out);
}

inline bool writeGlobal(const char* name, double value, int arrayIndex = ARRAY_NONE) {
    int slot = findVarSlot(*pGlobals, name);
    if (slot < 0) return false;
    RValue v{}; v.real = value; v.kind = KIND_REAL;
    setGlobal(slot + 100000, arrayIndex, &v);
    return true;
}

inline bool readInstance(void* inst, int varId, double& out) {
    RValue v{}; v.kind = 5;
    getInstanceVar(inst, varId, ARRAY_NONE, &v);
    return toDouble(v, out);
}

inline void writeInstance(void* inst, int varId, double value, int arrayIndex = ARRAY_NONE) {
    RValue v{}; v.real = value; v.kind = KIND_REAL;
    setInstanceVar(inst, varId, arrayIndex, &v);
}

// Kullanici tanimli (builtin olmayan) instance degiskeni, orn. obj_heart.movement
inline bool readInstanceVar(void* inst, const char* name, double& out) {
    int slot = findVarSlot(inst, name);  // instance degiskenlerinin slot'u instance'in kendisiyle aranir
    return slot >= 0 && readInstance(inst, slot + 100000, out);
}

// Var olan kullanici tanimli instance degiskenine yazar.
inline bool writeInstanceVar(void* inst, const char* name, double value) {
    int slot = findVarSlot(inst, name);
    if (slot < 0) return false;
    writeInstance(inst, slot + 100000, value);
    return true;
}

// Tum aktif instance'lari gezer: fn(CInstance*)
template <class F>
inline void forEachInstance(F&& fn) {
    uint8_t* buckets = *pInstBuckets;
    if (!buckets) return;
    uint32_t mask = *pInstMask;
    for (uint32_t i = 0; i <= mask; ++i) {
        for (uint8_t* node = *(uint8_t**)(buckets + i * 8); node; node = *(uint8_t**)(node + 4)) {
            uint8_t* inst = *(uint8_t**)(node + 0xC);
            if (inst && !inst[0x4C] && !inst[0x4D]) fn((void*)inst);  // silinmek uzere / deaktif olanlari atla
        }
    }
}

// Instance id'sinden (orn. global.monsterinstance[i]) CInstance* (yoksa nullptr).
inline void* instanceById(int id) {
    uint8_t* buckets = *pInstBuckets;
    if (!buckets || id < 0) return nullptr;
    for (uint8_t* node = *(uint8_t**)(buckets + (id & *pInstMask) * 8); node; node = *(uint8_t**)(node + 4))
        if (*(int*)(node + 8) == id) return *(void**)(node + 0xC);
    return nullptr;
}

// Verilen objenin ilk aktif instance'i (yoksa nullptr).
inline void* findInstance(int objectIndex, int objectIndexVar) {
    void* found = nullptr;
    if (objectIndexVar < 0) return nullptr;
    forEachInstance([&](void* inst) {
        double obj;
        if (!found && readInstance(inst, objectIndexVar, obj) && (int)obj == objectIndex) found = inst;
    });
    return found;
}

} // namespace gm
