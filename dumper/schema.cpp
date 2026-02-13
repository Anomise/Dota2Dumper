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

static bool DetectFindTypeScopeIndex(void* schemaSystem) {
    printf("[*] Auto-detecting FindTypeScopeForModule vfunc index...\n");

    const char* moduleNames[] = { "client.dll", "!GlobalTypes", nullptr };

    for (int idx = 10; idx <= 25; idx++) {
        for (int mn = 0; moduleNames[mn]; mn++) {
            void* result = SEH_VCall2(schemaSystem, idx, moduleNames[mn], nullptr);
            if (result && SEH_IsReadable(result, 0x10)) {
                void* vtable = nullptr;
                SEH_ReadPtr(result, 0, &vtable);
                if (vtable && SEH_IsReadable(vtable, 0x40)) {
                    printf("    [+] Found at vfunc %d (module=%s) -> 0x%p\n",
                        idx, moduleNames[mn], result);
                    g_VFuncIdx.findTypeScopeForModule = idx;
                    return true;
                }
            }

            result = SEH_VCall1(schemaSystem, idx, moduleNames[mn]);
            if (result && SEH_IsReadable(result, 0x10)) {
                void* vtable = nullptr;
                SEH_ReadPtr(result, 0, &vtable);
                if (vtable && SEH_IsReadable(vtable, 0x40)) {
                    printf("    [+] Found at vfunc %d (module=%s, 1 arg) -> 0x%p\n",
                        idx, moduleNames[mn], result);
                    g_VFuncIdx.findTypeScopeForModule = idx;
                    return true;
                }
            }
        }
    }

    printf("    [-] Could not detect FindTypeScopeForModule\n");
    return false;
}

static bool DetectFindDeclaredClassIndex(void* typeScope) {
    printf("[*] Auto-detecting FindDeclaredClass vfunc index...\n");

    const char* testClasses[] = {
        "C_BaseEntity", "CBaseEntity", "CEntityInstance",
        "C_BaseModelEntity", "CGameSceneNode", nullptr
    };

    for (int idx = 0; idx <= 15; idx++) {
        for (int tc = 0; testClasses[tc]; tc++) {
            void* result = SEH_VCall1(typeScope, idx, testClasses[tc]);
            if (!result) continue;
            if (!SEH_IsReadable(result, 0x30)) continue;
            for (int nameOff = 0x00; nameOff <= 0x10; nameOff += 0x08) {
                const char* name = SEH_ReadStr(result, nameOff);
                if (name && strcmp(name, testClasses[tc]) == 0) {
                    printf("    [+] Found at vfunc %d (class=%s, name@0x%X) -> 0x%p\n",
                        idx, testClasses[tc], nameOff, result);
                    g_VFuncIdx.findDeclaredClass = idx;
                    return true;
                }
            }
        }
    }

    printf("    [-] Could not detect FindDeclaredClass\n");
    return false;
}

static bool DetectBindingLayout(void* typeScope, const char* testClass) {
    printf("[*] Auto-detecting CSchemaClassBinding layout...\n");

    void* binding = SEH_VCall1(typeScope, g_VFuncIdx.findDeclaredClass, testClass);
    if (!binding || !SEH_IsReadable(binding, 0x80)) {
        printf("    [-] Test binding not readable\n");
        return false;
    }

    int nameOffset = -1;
    for (int off = 0x00; off <= 0x18; off += 0x08) {
        const char* n = SEH_ReadStr(binding, off);
        if (n && strcmp(n, testClass) == 0) {
            nameOffset = off;
            break;
        }
    }

    if (nameOffset < 0) {
        printf("    [-] Could not find name offset\n");
        return false;
    }

    printf("    name offset = 0x%X\n", nameOffset);
    g_BindingLayout.name_offset = nameOffset;

    bool found = false;

    for (int fcOff = 0x10; fcOff <= 0x40; fcOff += 0x02) {
        int16_t fc = 0;
        if (!SEH_ReadI16(binding, fcOff, &fc)) continue;
        if (fc <= 0 || fc > 500) continue;

        for (int fOff = (fcOff + 4) & ~7; fOff <= 0x60; fOff += 0x08) {
            void* fieldsPtr = nullptr;
            if (!SEH_ReadPtr(binding, fOff, &fieldsPtr)) continue;
            if (!fieldsPtr) continue;
            if (!SEH_IsReadable(fieldsPtr, sizeof(SchemaField_t))) continue;
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

            found = true;
            goto done_fields;
        }
    }

done_fields:
    if (!found) {
        printf("    [-] Could not find field_count/fields offsets\n");
        return false;
    }

    for (int sOff = nameOffset + 0x08; sOff <= 0x30; sOff += 0x04) {
        if (sOff == g_BindingLayout.field_count_offset) continue;

        int32_t sz = 0;
        if (!SEH_ReadI32(binding, sOff, &sz)) continue;
        if (sz > 0 && sz < 0x100000) {
            g_BindingLayout.size_offset = sOff;
            printf("    size offset        = 0x%X (size=%d/0x%X)\n", sOff, sz, sz);
            break;
        }
    }

    for (int pOff = 0x00; pOff <= 0x30; pOff += 0x08) {
        if (pOff == nameOffset) continue;
        if (pOff == g_BindingLayout.fields_offset) continue;

        void* parentPtr = nullptr;
        if (!SEH_ReadPtr(binding, pOff, &parentPtr)) continue;
        if (!parentPtr) continue;
        if (!SEH_IsReadable(parentPtr, 0x20)) continue;

        const char* parentName = SEH_ReadStr(parentPtr, nameOffset);
        if (parentName && SEH_ValidateString(parentName, 256)) {
            g_BindingLayout.parent_offset = pOff;
            printf("    parent offset      = 0x%X (parent=%s)\n", pOff, parentName);
            break;
        }
    }

    g_BindingLayout.detected = true;
    printf("[+] Binding layout detected successfully!\n\n");
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
    return SEH_VCall1(scope, g_VFuncIdx.findDeclaredClass, className);
}

bool SchemaInit() {
    g_pSchema = GetInterface("schemasystem.dll", "SchemaSystem_001");
    if (!g_pSchema) {
        printf("[-] SchemaSystem_001 not found\n");
        return false;
    }
    printf("[+] SchemaSystem @ 0x%p\n", g_pSchema);

    if (!DetectFindTypeScopeIndex(g_pSchema)) return false;

    void* testScope = nullptr;
    const char* modules[] = { "client.dll", "!GlobalTypes", nullptr };
    for (int i = 0; modules[i]; i++) {
        testScope = SEH_VCall2(g_pSchema, g_VFuncIdx.findTypeScopeForModule, modules[i], nullptr);
        if (!testScope)
            testScope = SEH_VCall1(g_pSchema, g_VFuncIdx.findTypeScopeForModule, modules[i]);
        if (testScope) break;
    }

    if (!testScope) {
        printf("[-] No valid type scope found\n");
        return false;
    }

    if (!DetectFindDeclaredClassIndex(testScope)) return false;

    g_VFuncIdx.detected = true;

    const char* layoutTestClasses[] = {
        "C_BaseEntity", "CBaseEntity", "CEntityInstance", nullptr
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
