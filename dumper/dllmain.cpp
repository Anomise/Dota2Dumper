#include <Windows.h>
#include <cstdio>
#include <string>
#include <filesystem>

#include "schema.hpp"
#include "dump.hpp"

#define OUT_DIR "C:\\dota2_dump"

static bool WaitModules(DWORD timeoutMs = 60000) {
    static const char* mods[] = {
        "client.dll", "engine2.dll", "schemasystem.dll", "tier0.dll"
    };

    printf("[*] Waiting for modules...\n");
    DWORD t0 = GetTickCount();

    while (true) {
        bool ok = true;
        for (auto& m : mods)
            if (!GetModuleHandleA(m)) { ok = false; break; }
        if (ok) { printf("[+] All modules loaded\n"); return true; }
        if (GetTickCount() - t0 > timeoutMs) return false;
        Sleep(500);
    }
}

static void PrintModules() {
    static const char* list[] = {
        "client.dll","server.dll","engine2.dll","schemasystem.dll",
        "tier0.dll","inputsystem.dll","rendersystemdx11.dll",
        "particles.dll","animationsystem.dll","materialsystem2.dll",nullptr
    };
    printf("\n  %-35s %s\n", "Module", "Address");
    printf("  %-35s %s\n", "------", "-------");
    for (int i = 0; list[i]; i++) {
        HMODULE h = GetModuleHandleA(list[i]);
        if (h) printf("  %-35s 0x%p\n", list[i], h);
    }
    printf("\n");
}

static void MainThread(HMODULE hSelf) {
    AllocConsole();
    SetConsoleTitleA("Dota 2 Schema Dumper");
    FILE* co = nullptr;
    FILE* ci = nullptr;
    freopen_s(&co, "CONOUT$", "w", stdout);
    freopen_s(&ci, "CONIN$",  "r", stdin);

    printf("==============================================\n");
    printf("  Dota 2 Schema Dumper (Safe Mode)\n");
    printf("  %s %s\n", __DATE__, __TIME__);
    printf("==============================================\n\n");

    if (!WaitModules()) {
        printf("[-] Timeout waiting for modules\n");
        goto done;
    }

    printf("[*] Extra wait 5 s for initialization...\n");
    Sleep(5000);

    PrintModules();

    if (!SchemaInit()) {
        printf("\n[-] Schema init failed.\n");
        printf("    This means the Schema System interface or\n");
        printf("    vfunc layout has changed in this Dota 2 build.\n");
        goto done;
    }

    {
        std::filesystem::create_directories(OUT_DIR);

        Dumper d;

        d.DumpModule("client.dll");

        if (GetModuleHandleA("server.dll"))
            d.DumpModule("server.dll");

        if (d.ClassCount() == 0) {
            printf("\n[-] No classes found.\n");
        } else {
            d.SaveHpp(std::string(OUT_DIR) + "\\offsets.hpp");
            d.SaveJson(std::string(OUT_DIR) + "\\offsets.json");

            printf("\n==============================================\n");
            printf("  DONE — %zu classes dumped\n", d.ClassCount());
            printf("  Output: %s\n", OUT_DIR);
            printf("==============================================\n");
        }
    }

done:
    printf("\nPress ENTER to unload...\n");
    if (ci) { getchar(); fclose(ci); }
    if (co) fclose(co);
    FreeConsole();
    FreeLibraryAndExitThread(hSelf, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CloseHandle(CreateThread(nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread),
            hModule, 0, nullptr));
    }
    return TRUE;
}
