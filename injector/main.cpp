#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <cstdio>
#include <string>
#include <filesystem>

// Цвета консольки

static void Color(WORD c) { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c); }
static void Green()  { Color(10); }
static void Red()    { Color(12); }
static void Yellow() { Color(14); }
static void Cyan()   { Color(11); }
static void White()  { Color(15); }
static void Reset()  { Color(7);  }


// Поиск процесса по имени


static DWORD FindPID(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
}


// Проверка модуля
static bool HasModule(DWORD pid, const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32 me{}; me.dwSize = sizeof(me);
    bool found = false;

    if (Module32First(snap, &me)) {
        do {
            if (_stricmp(me.szModule, name) == 0) { found = true; break; }
        } while (Module32Next(snap, &me));
    }

    CloseHandle(snap);
    return found;
}


// Инжектт используя LoadLibraryA
static bool Inject(DWORD pid, const char* dllPath) {
    char full[MAX_PATH];
    GetFullPathNameA(dllPath, MAX_PATH, full, nullptr);

    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        FALSE, pid);

    if (!hProc) {
        Red(); printf("[-] OpenProcess failed (run as admin?)\n"); Reset();
        return false;
    }

    size_t len = strlen(full) + 1;
    void* mem = VirtualAllocEx(hProc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) {
        Red(); printf("[-] VirtualAllocEx failed\n"); Reset();
        CloseHandle(hProc);
        return false;
    }

    WriteProcessMemory(hProc, mem, full, len, nullptr);

    auto pLL = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, pLL, mem, 0, nullptr);
    if (!hThread) {
        Red(); printf("[-] CreateRemoteThread failed\n"); Reset();
        VirtualFreeEx(hProc, mem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    WaitForSingleObject(hThread, 15000);

    VirtualFreeEx(hProc, mem, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProc);
    return true;
}


// Осн
int main(int argc, char* argv[]) {
    SetConsoleTitleA("Dota 2 Dumper — Injector");

    Cyan();
    printf("==============================================\n");
    printf("  Dota 2 Schema Dumper — Injector\n");
    printf("  %s %s\n", __DATE__, __TIME__);
    printf("==============================================\n\n");
    Reset();

    // Ищем дллку
    std::string dll;
    if (argc > 1) {
        dll = argv[1];
    } else {
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        dll = (std::filesystem::path(exe).parent_path() / "dumper.dll").string();
    }

    White(); printf("[*] DLL: %s\n", dll.c_str()); Reset();

    if (!std::filesystem::exists(dll)) {
        Red();   printf("[-] File not found!\n");
        Yellow(); printf("[*] Put dumper.dll next to injector.exe\n"); Reset();
        printf("\nENTER to exit..."); getchar();
        return 1;
    }

    // Ждем дотку
    Yellow(); printf("[*] Waiting for dota2.exe ...\n"); Reset();

    DWORD pid = 0;
    while (!(pid = FindPID("dota2.exe"))) Sleep(2000);

    Green(); printf("[+] dota2.exe PID = %u\n", pid); Reset();

    // Теперь модули
    Yellow(); printf("[*] Waiting for game modules...\n"); Reset();

    const char* need[] = { "client.dll", "engine2.dll", "schemasystem.dll" };
    for (auto& m : need) {
        int w = 0;
        while (!HasModule(pid, m)) {
            if (w++ == 0) printf("    waiting %s ...\n", m);
            Sleep(1000);
            if (w > 120) {
                Red(); printf("[-] Timeout %s\n", m); Reset();
                printf("ENTER to exit..."); getchar();
                return 1;
            }
        }
        Green(); printf("    [+] %s\n", m); Reset();
    }

    Yellow(); printf("[*] Extra wait 5 s...\n"); Reset();
    Sleep(5000);

    // сам инжект
    Cyan(); printf("[*] Injecting...\n"); Reset();

    if (Inject(pid, dll.c_str())) {
        Green();
        printf("\n==============================================\n");
        printf("  INJECTED!\n");
        printf("  Switch to Dota 2 to see dumper console.\n");
        printf("  Results -> C:\\dota2_dump\\\n");
        printf("==============================================\n");
        Reset();
    } else {
        Red(); printf("\n[-] Injection failed\n"); Reset();
    }

    printf("\nENTER to exit...");
    getchar();
    return 0;
}
