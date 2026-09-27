// UTInjector - utcheat.dll'i UNDERTALE.exe'ye yukler. Cift tikla, gerisini o halleder:
// oyun kapaliysa Steam uzerinden baslatir, penceresini bekler, DLL'i yukler. Konsol penceresi acmaz; basarida
// oyunda acilis yazisi cikar, sadece hata olursa mesaj kutusu gosterir.
#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>

static DWORD findProcess(const wchar_t* name) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{ sizeof(pe) };
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
    CloseHandle(snap);
    return pid;
}

static bool hasModule(DWORD pid, const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{ sizeof(me) };
    bool found = false;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
        if (_wcsicmp(me.szModule, name) == 0) { found = true; break; }
    CloseHandle(snap);
    return found;
}

struct WindowSearch { DWORD pid; bool found; };

static BOOL CALLBACK checkWindow(HWND w, LPARAM l) {
    auto* c = (WindowSearch*)l;
    DWORD p; GetWindowThreadProcessId(w, &p);
    if (p == c->pid && IsWindowVisible(w)) { c->found = true; return FALSE; }
    return TRUE;
}

static bool hasWindow(DWORD pid) {
    WindowSearch ctx{ pid, false };
    EnumWindows(checkWindow, (LPARAM)&ctx);
    return ctx.found;
}

static int fail(const wchar_t* msg) {
    MessageBoxW(nullptr, msg, L"UTInjector", MB_ICONERROR);
    return 1;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t dll[MAX_PATH];
    GetModuleFileNameW(nullptr, dll, MAX_PATH);
    wcscpy(wcsrchr(dll, L'\\') + 1, L"utcheat.dll");
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) return fail(L"utcheat.dll, UTInjector.exe ile aynı klasörde olmalı.");

    DWORD pid = findProcess(L"UNDERTALE.exe");
    if (!pid) {
        ShellExecuteW(nullptr, L"open", L"steam://rungameid/391540", nullptr, nullptr, SW_SHOWNORMAL);
        for (int i = 0; i < 120 && !pid; ++i) { Sleep(500); pid = findProcess(L"UNDERTALE.exe"); }
        if (!pid) return fail(L"Undertale 60 saniye içinde açılmadı. Oyunu kendin açıp tekrar dene.");
    }
    for (int i = 0; i < 60 && !hasWindow(pid); ++i) Sleep(500);
    Sleep(1500);  // D3D cihazinin olusmasi icin kisa bekleme

    if (hasModule(pid, L"utcheat.dll")) {
        MessageBoxW(nullptr, L"Hile zaten yüklü. Oyunda INSERT ile menüyü aç/kapat.", L"UTInjector", MB_ICONINFORMATION);
        return 0;
    }

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                              PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!proc) return fail(L"Oyun işlemine erişilemedi.");

    SIZE_T size = (wcslen(dll) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(proc, remote, dll, size, nullptr)) return fail(L"Oyun belleğine yazılamadı.");

    // Bu enjektor de 32-bit derlendigi icin buradaki LoadLibraryW adresi oyundakiyle aynidir.
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) return fail(L"Uzak thread oluşturulamadı.");
    WaitForSingleObject(th, 10000);
    DWORD exitCode = 0;
    GetExitCodeThread(th, &exitCode);
    CloseHandle(th);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    if (!exitCode) return fail(L"DLL yüklenemedi (LoadLibrary başarısız).");
    return 0;  // basarili: oyunda acilis yazisi cikar
}
