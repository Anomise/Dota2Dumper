#include "schema.hpp"

CSchemaSystem* g_pSchema = nullptr;

void* GetInterface(const char* moduleName, const char* ifaceName) {
    HMODULE hMod = GetModuleHandleA(moduleName);
    if (!hMod) {
        printf("[-] Module not loaded: %s\n", moduleName);
        return nullptr;
    }

    using Fn = void*(*)(const char*, int*);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(hMod, "CreateInterface"));
    if (!fn) {
        printf("[-] CreateInterface missing in %s\n", moduleName);
        return nullptr;
    }

    int rc = 0;
    void* p = fn(ifaceName, &rc);
    if (!p) printf("[-] Interface %s not found\n", ifaceName);
    return p;
}

bool SchemaInit() {
    g_pSchema = reinterpret_cast<CSchemaSystem*>(
        GetInterface("schemasystem.dll", "SchemaSystem_001")
    );
    if (!g_pSchema) return false;
    printf("[+] SchemaSystem @ 0x%p\n", g_pSchema);
    return true;
}
