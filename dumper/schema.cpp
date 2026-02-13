#include "schema.hpp"
#include <cstring>

void*              g_pSchema       = nullptr;
BindingLayout      g_BindingLayout = {};
SchemaVFuncIndices g_VFuncIdx      = {};

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

static void DumpHex(const char* label, void* addr, int size) {
    printf("    %s @ 0x%p:\n    ", label, addr);
    for (int i = 0; i < size; i++) {
        if (!SEH_IsReadable((void*)((uintptr_t)addr + i), 1)) {
            printf("?? ");
        } else {
            printf("%02X ", *(unsigned char*)((uintptr_t)addr + i));
        }
        if ((i + 1) % 16 == 0 && i + 1 < size) printf("\n    ");
    }
    printf("\n");
}

static const char* FindScopeName(void* scope) {
    for (int off = 0x08; off <= 0x20; off++) {
        const char* s = (const char*)((uintptr_t)scope + off);
        if (SEH_IsReadable(s, 32) && SEH_ValidateString(s, 256)) {
            if (strstr(s, "client") || strstr(s, "server") ||
                strstr(s, "engine") || strstr(s, "Global") ||
                strstr(s, ".dll") || strstr(s, "panorama") ||
                strstr(s, "particles") || strstr(s, "animat")) {
                return s;
            }
        }
    }
    return nullptr;
}

static bool DetectFindTypeScopeIndex(void* schemaSystem) {
    printf("[*] Detecting FindTypeScopeForModule...\n");

    const char* tryModules[] = {
        "client.dll", "server.dll", "engine2.dll",
        "!GlobalTypes", "client", "server",
        "schemasystem.dll",
        nullptr
    };

    for (int idx = 8; idx <= 30; idx++) {
        for (int mn = 0; tryModules[mn]; mn++) {
            void* result = SEH_VCall2(schemaSystem, idx, tryModules[mn], nullptr);
            if (!result) result = SEH_VCall1(schemaSystem, idx, tryModules[mn]);
            if (!result || !SEH_IsReadable(result, 0x20)) continue;

            void* vt = nullptr;
            if (!SEH_ReadPtr(result, 0, &vt)) continue;
            if (!vt || !SEH_IsReadable(vt, 0x40)) continue;

            const char* scopeName = FindScopeName(result);
            if (scopeName) {
                printf("    [+] vfunc %d (module=%s) -> 0x%p (scope=%s)\n",
                    idx, tryModules[mn], result, scopeName);
                g_VFuncIdx.findTypeScopeForModule = idx;
                return true;
            }
        }
    }

    for (int idx = 8; idx <= 30; idx++) {
        void* result = SEH_VCall2(schemaSystem, idx, "client.dll", nullptr);
        if (!result) result = SEH_VCall1(schemaSystem, idx, "client.dll");
        if (!result || !SEH_IsReadable(result, 0x20)) continue;

        void* vt = nullptr;
        if (!SEH_ReadPtr(result, 0, &vt)) continue;
        if (!vt || !SEH_IsReadable(vt, 0x40)) continue;

        printf("    [+] vfunc %d (fallback, no name check) -> 0x%p\n", idx, result);
        g_VFuncIdx.findTypeScopeForModule = idx;
        return true;
    }

    printf("    [-] Could not detect FindTypeScopeForModule\n");
    return false;
}

static bool CheckNameInResult(void* r, const char* className, int* outNameOff, int* outIndirectOff) {
    for (int no = 0x00; no <= 0x100; no += 0x08) {
        const char* name = SEH_ReadStr(r, no);
        if (name && strcmp(name, className) == 0) {
            *outNameOff = no;
            *outIndirectOff = -1;
            return true;
        }
    }

    for (int off = 0x00; off <= 0x60; off += 0x08) {
        void* inner = nullptr;
        if (!SEH_ReadPtr(r, off, &inner)) continue;
        if (!inner || inner == r) continue;
        if (!SEH_IsReadable(inner, 0x40)) continue;

        for (int no = 0x00; no <= 0x80; no += 0x08) {
            const char* name = SEH_ReadStr(inner, no);
            if (name && strcmp(name, className) == 0) {
                *outNameOff = no;
                *outIndirectOff = off;
                return true;
            }
        }
    }

    return false;
}

static bool DetectFindDeclaredClassIndex(void* typeScope) {
    printf("[*] Detecting FindDeclaredClass...\n");

    DumpHex("TypeScope first 64 bytes", typeScope, 64);

    const char* scopeName = FindScopeName(typeScope);
    if (scopeName)
        printf("    Scope name: %s\n", scopeName);
    else
        printf("    Scope name: NOT FOUND (might be wrong pointer)\n");

    const char* testClasses[] = {
        "C_BaseEntity",
        "CBaseEntity",
        "CEntityInstance",
        "CGameSceneNode",
        "C_DOTA_BaseNPC",
        "CDOTA_BaseNPC",
        "C_BaseModelEntity",
        "CBaseModelEntity",
        "C_DOTA_BaseNPC_Hero",
        "CBodyComponent",
        nullptr
    };

    int totalNonNull = 0;
    int nameOff = -1;
    int indirectOff = -1;

    for (int idx = 1; idx <= 40; idx++) {
        for (int tc = 0; testClasses[tc]; tc++) {
            void* r = SEH_VCall1(typeScope, idx, testClasses[tc]);
            if (r && SEH_IsReadable(r, 0x30)) {
                totalNonNull++;
                if (CheckNameInResult(r, testClasses[tc], &nameOff, &indirectOff)) {
                    printf("    [+] vfunc %d, 1 arg, class=%s", idx, testClasses[tc]);
                    if (indirectOff >= 0)
                        printf(", indirect@0x%X", indirectOff);
                    printf(", name@0x%X -> 0x%p\n", nameOff, r);
                    g_VFuncIdx.findDeclaredClass = idx;
                    g_VFuncIdx.findDeclaredClassNArgs = 1;
                    g_VFuncIdx.classNameOffset = nameOff;
                    g_VFuncIdx.classInfoPtrOffset = indirectOff;
                    return true;
                }
            }

            r = SEH_VCall2(typeScope, idx, testClasses[tc], nullptr);
            if (r && SEH_IsReadable(r, 0x30)) {
                totalNonNull++;
                if (CheckNameInResult(r, testClasses[tc], &nameOff, &indirectOff)) {
                    printf("    [+] vfunc %d, 2 args, class=%s", idx, testClasses[tc]);
                    if (indirectOff >= 0)
                        printf(", indirect@0x%X", indirectOff);
                    printf(", name@0x%X -> 0x%p\n", nameOff, r);
                    g_VFuncIdx.findDeclaredClass = idx;
                    g_VFuncIdx.findDeclaredClassNArgs = 2;
                    g_VFuncIdx.classNameOffset = nameOff;
                    g_VFuncIdx.classInfoPtrOffset = indirectOff;
                    return true;
                }
            }
        }
    }

    printf("    [-] Not found. Non-null results: %d\n", totalNonNull);
    printf("    [-] Printing first 10 non-null results for analysis:\n");

    int printed = 0;
    for (int idx = 1; idx <= 40 && printed < 10; idx++) {
        void* r = SEH_VCall1(typeScope, idx, "C_BaseEntity");
        if (r && SEH_IsReadable(r, 0x10)) {
            printf("        vfunc %d -> 0x%p\n", idx, r);
            DumpHex("          ", r, 48);
            printed++;
        }
    }

    return false;
}

static bool DetectBindingLayout(void* typeScope, const char* testClass) {
    printf("[*] Detecting CSchemaClassBinding layout...\n");

    void* raw = nullptr;
    if (g_VFuncIdx.findDeclaredClassNArgs == 2)
        raw = SEH_VCall2(typeScope, g_VFuncIdx.findDeclaredClass, testClass, nullptr);
    else
        raw = SEH_VCall1(typeScope, g_VFuncIdx.findDeclaredClass, testClass);

    if (!raw || !SEH_IsReadable(raw, 0x60)) {
        printf("    [-] Raw result not readable\n");
        return false;
    }

    void* binding = raw;
    if (g_VFuncIdx.classInfoPtrOffset >= 0) {
        void* inner = nullptr;
        if (!SEH_ReadPtr(raw, g_VFuncIdx.classInfoPtrOffset, &inner)) {
            printf("    [-] Cannot follow indirect ptr\n");
            return false;
        }
        if (!inner || !SEH_IsReadable(inner, 0x60)) {
            printf("    [-] Inner ptr not readable\n");
            return false;
        }
        binding = inner;
        printf("    Using indirect binding @ 0x%p (via raw 0x%p + 0x%X)\n",
            binding, raw, g_VFuncIdx.classInfoPtrOffset);
    }

    DumpHex("Binding", binding, 80);

    int nameOffset = g_VFuncIdx.classNameOffset;

    const char* verifyName = SEH_ReadStr(binding, nameOffset);
    if (!verifyName || strcmp(verifyName, testClass) != 0) {
        printf("    [-] Name verification failed at offset 0x%X\n", nameOffset);
        for (int no = 0x00; no <= 0x80; no += 0x08) {
            const char* n = SEH_ReadStr(binding, no);
            if (n && strcmp(n, testClass) == 0) {
                nameOffset = no;
                printf("    [+] Found name at new offset 0x%X\n", nameOffset);
                break;
            }
        }
    }

    printf("    name offset = 0x%X\n", nameOffset);
    g_BindingLayout.name_offset = nameOffset;

    bool foundFields = false;

    for (int fcOff = 0x10; fcOff <= 0x50; fcOff += 0x02) {
        int16_t fc = 0;
        if (!SEH_ReadI16(binding, fcOff, &fc)) continue;
        if (fc <= 0 || fc > 500) continue;

        for (int fOff = ((fcOff + 6) & ~7); fOff <= 0x70; fOff += 0x08) {
            void* fieldsPtr = nullptr;
            if (!SEH_ReadPtr(binding, fOff, &fieldsPtr)) continue;
            if (!fieldsPtr) continue;
            if (!SEH_IsReadable(fieldsPtr, (int)sizeof(SchemaField_t))) continue;

            const char* firstName = SEH_ReadStr(fieldsPtr, 0x00);
            if (!firstName) continue;

            int32_t firstOffset = 0;
            if (!SEH_ReadI32(fieldsPtr, 0x10, &firstOffset)) continue;
            if (firstOffset < 0 || firstOffset > 0x100000) continue;

            g_BindingLayout.field_count_offset = fcOff;
            g_BindingLayout.fields_offset = fOff;
            printf("    field_count offset = 0x%X (count=%d)\n", fcOff, fc);
            printf("    fields offset      = 0x%X\n", fOff);
            printf("    first field: \"%s\" @ 0x%X\n", firstName, firstOffset);
            foundFields = true;
            goto done_fields;
        }
    }

done_fields:
    if (!foundFields) {
        printf("    [-] Could not find field_count/fields\n");
        return false;
    }

    g_BindingLayout.size_offset = -1;
    for (int sOff = nameOffset + 0x08; sOff <= 0x30; sOff += 0x04) {
        if (sOff == g_BindingLayout.field_count_offset) continue;
        int32_t sz = 0;
        if (!SEH_ReadI32(binding, sOff, &sz)) continue;
        if (sz > 0 && sz < 0x100000) {
            g_BindingLayout.size_offset = sOff;
            printf("    size offset        = 0x%X (size=0x%X)\n", sOff, sz);
            break;
        }
    }

    g_BindingLayout.parent_offset = -1;
    for (int pOff = 0x00; pOff <= 0x30; pOff += 0x08) {
        if (pOff == nameOffset) continue;
        if (pOff == g_BindingLayout.fields_offset) continue;
        void* parentPtr = nullptr;
        if (!SEH_ReadPtr(binding, pOff, &parentPtr)) continue;
        if (!parentPtr) continue;
        if (parentPtr == binding) continue;
        if (!SEH_IsReadable(parentPtr, 0x20)) continue;
        const char* parentName = SEH_ReadStr(parentPtr, nameOffset);
        if (parentName && SEH_ValidateString(parentName, 256)) {
            g_BindingLayout.parent_offset = pOff;
            printf("    parent offset      = 0x%X (parent=%s)\n", pOff, parentName);
            break;
        }
    }

    g_BindingLayout.detected = true;
    printf("[+] Layout detected!\n\n");
    return true;
}

void* Schema_FindTypeScope(const char* moduleName) {
    if (!g_pSchema || !g_VFuncIdx.detected) return nullptr;
    void* result = SEH_VCall2(g_pSchema, g_VFuncIdx.findTypeScopeForModule, moduleName, nullptr);
    if (!result)
        result = SEH_VCall1(g_pSchema, g_VFuncIdx.findTypeScopeForModule, moduleName);
    return result;
}

void* Schema_FindClass(void* scope, const char* className) {
    if (!scope || !g_VFuncIdx.detected) return nullptr;

    void* raw = nullptr;
    if (g_VFuncIdx.findDeclaredClassNArgs == 2)
        raw = SEH_VCall2(scope, g_VFuncIdx.findDeclaredClass, className, nullptr);
    else
        raw = SEH_VCall1(scope, g_VFuncIdx.findDeclaredClass, className);

    if (!raw) return nullptr;

    if (g_VFuncIdx.classInfoPtrOffset >= 0) {
        void* inner = nullptr;
        if (!SEH_ReadPtr(raw, g_VFuncIdx.classInfoPtrOffset, &inner)) return nullptr;
        return inner;
    }

    return raw;
}

bool SchemaInit() {
    g_pSchema = GetInterface("schemasystem.dll", "SchemaSystem_001");
    if (!g_pSchema) {
        printf("[-] SchemaSystem_001 not found\n");
        return false;
    }
    printf("[+] SchemaSystem @ 0x%p\n\n", g_pSchema);

    if (!DetectFindTypeScopeIndex(g_pSchema)) return false;

    void* testScope = nullptr;
    const char* scopeModule = nullptr;
    const char* tryMods[] = { "client.dll", "!GlobalTypes", "server.dll", "engine2.dll", nullptr };
    for (int i = 0; tryMods[i]; i++) {
        testScope = Schema_FindTypeScope(tryMods[i]);
        if (testScope && SEH_IsReadable(testScope, 0x20)) {
            scopeModule = tryMods[i];
            break;
        }
    }

    if (!testScope) {
        printf("[-] No valid TypeScope found\n");
        return false;
    }

    printf("[+] Using TypeScope for '%s' @ 0x%p\n\n", scopeModule, testScope);

    if (!DetectFindDeclaredClassIndex(testScope)) {
        printf("\n[!] FindDeclaredClass detection failed.\n");
        printf("[!] This Dota 2 build may have changed the Schema System layout.\n");
        printf("[!] Check the hex dumps above for clues.\n");
        return false;
    }

    g_VFuncIdx.detected = true;
    printf("\n");

    const char* layoutTestClasses[] = {
        "C_BaseEntity", "CBaseEntity", "CEntityInstance",
        "CGameSceneNode", "C_BaseModelEntity",
        "C_DOTA_BaseNPC", "CDOTA_BaseNPC",
        nullptr
    };

    for (int i = 0; layoutTestClasses[i]; i++) {
        if (DetectBindingLayout(testScope, layoutTestClasses[i]))
            break;
    }

    if (!g_BindingLayout.detected) {
        printf("[-] Could not detect binding layout\n");
        return false;
    }

    return true;
}
