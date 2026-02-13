#include "dump.hpp"
#include <algorithm>
#include <iomanip>
#include <filesystem>

static const char* g_Classes[] = {
    "C_BaseEntity", "CBaseEntity",
    "C_BaseModelEntity", "CBaseModelEntity",
    "CBodyComponent", "CBodyComponentPoint",
    "CGameSceneNode", "CSkeletonInstance",
    "CEntityIdentity", "CEntityInstance",

    "CBasePlayerController", "C_BasePlayerController",
    "C_BasePlayerPawn", "CBasePlayerPawn",
    "C_DOTAPlayerController", "CDOTAPlayerController",
    "CDOTA_PlayerController",

    "C_DOTA_BaseNPC", "CDOTA_BaseNPC",
    "C_DOTA_BaseNPC_Hero", "CDOTA_BaseNPC_Hero",
    "C_DOTA_BaseNPC_Creep",
    "C_DOTA_BaseNPC_Creep_Lane",
    "C_DOTA_BaseNPC_Creep_Neutral",
    "C_DOTA_BaseNPC_Creep_Siege",

    "C_DOTA_BaseNPC_Tower", "C_DOTA_BaseNPC_Building",
    "C_DOTA_BaseNPC_Barracks", "C_DOTA_BaseNPC_Fort",
    "CDOTA_BaseNPC_Tower", "CDOTA_BaseNPC_Building",

    "C_DOTABaseAbility", "CDOTABaseAbility",
    "C_DOTA_Ability_AttributeBonus",

    "C_DOTA_Item", "CDOTA_Item", "C_DOTA_Item_Physical",

    "CDOTA_Buff", "CDOTA_Modifier_Lua", "CDOTA_ModifierManager",

    "C_DOTA_UnitInventory", "CDOTA_UnitInventory",

    "C_DOTAGameRules", "CDOTAGameRules",
    "C_DOTAGamerules", "CDOTAGamerulesProxy", "C_DOTAGamerulesProxy",

    "C_DOTATeam", "CDOTATeam",
    "C_DOTA_DataRadiant", "C_DOTA_DataDire",
    "C_DOTA_DataCustomTeam", "C_DOTA_DataSpectator",

    "C_DOTA_Unit_Courier", "C_DOTA_Unit_Roshan",
    "CDOTA_Unit_Courier", "CDOTA_Unit_Roshan",

    "C_DOTA_TempTree", "C_DOTA_Item_Rune",
    "C_DOTA_BaseNPC_Projectile",
    "C_DOTA_MapTree", "C_DOTA_MinimapBoundary",
    "CDOTA_CameraManager",

    nullptr
};

bool Dumper::TryDumpClass(void* scope, const char* name) {
    if (!scope || !name) return false;
    if (!g_BindingLayout.detected) return false;

    void* binding = Schema_FindClass(scope, name);
    if (!binding) return false;
    if (!SEH_IsReadable(binding, 0x60)) return false;

    const char* className = SEH_ReadStr(binding, g_BindingLayout.name_offset);
    if (!className) return false;

    int16_t fieldCount = 0;
    if (!SEH_ReadI16(binding, g_BindingLayout.field_count_offset, &fieldCount)) return false;
    if (fieldCount <= 0 || fieldCount > 2000) return false;

    void* fieldsPtr = nullptr;
    if (!SEH_ReadPtr(binding, g_BindingLayout.fields_offset, &fieldsPtr)) return false;
    if (!fieldsPtr) return false;
    if (!SEH_IsReadable(fieldsPtr, fieldCount * (int)sizeof(SchemaField_t))) return false;

    int32_t classSize = 0;
    SEH_ReadI32(binding, g_BindingLayout.size_offset, &classSize);

    std::string parentName;
    if (g_BindingLayout.parent_offset >= 0) {
        void* parentBinding = nullptr;
        if (SEH_ReadPtr(binding, g_BindingLayout.parent_offset, &parentBinding)) {
            if (parentBinding && SEH_IsReadable(parentBinding, 0x20)) {
                const char* pn = SEH_ReadStr(parentBinding, g_BindingLayout.name_offset);
                if (pn) parentName = pn;
            }
        }
    }

    // Read each field
    DumpClass dc;
    dc.name   = className;
    dc.size   = classSize;
    dc.parent = parentName;

    SchemaField_t* fields = reinterpret_cast<SchemaField_t*>(fieldsPtr);

    for (int i = 0; i < fieldCount; i++) {
        void* fieldAddr = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(fields) + i * sizeof(SchemaField_t)
        );
        if (!SEH_IsReadable(fieldAddr, sizeof(SchemaField_t))) continue;

        const char* fn = nullptr;
        if (!SEH_ReadPtr(fieldAddr, 0x00, (void**)&fn)) continue;
        if (!fn || !SEH_ValidateString(fn, 256)) continue;

        int32_t fOffset = 0;
        if (!SEH_ReadI32(fieldAddr, 0x10, &fOffset)) continue;
        if (fOffset < 0 || fOffset > 0x100000) continue;

        std::string typeName = "unknown";
        void* typePtr = nullptr;
        if (SEH_ReadPtr(fieldAddr, 0x08, &typePtr)) {
            if (typePtr && SEH_IsReadable(typePtr, 0x10)) {
                const char* tn = SEH_ReadStr(typePtr, 0x08);
                if (tn) typeName = tn;
            }
        }

        DumpField df;
        df.name   = fn;
        df.offset = fOffset;
        df.type   = typeName;
        dc.fields.push_back(df);
    }

    if (dc.fields.empty()) return false;

    std::sort(dc.fields.begin(), dc.fields.end(),
        [](const DumpField& a, const DumpField& b) {
            return a.offset < b.offset;
        });

    m_classes.push_back(dc);
    printf("  [+] %-45s %3d fields  size=0x%X\n",
        dc.name.c_str(), (int)dc.fields.size(), dc.size);

    return true;
}

void Dumper::DumpModule(const char* moduleName) {
    void* scope = Schema_FindTypeScope(moduleName);
    if (!scope) {
        printf("[-] Scope not found: %s\n", moduleName);
        return;
    }

    printf("[*] Dumping %s (scope @ 0x%p)...\n", moduleName, scope);

    int found = 0;
    for (int i = 0; g_Classes[i]; i++) {
        if (TryDumpClass(scope, g_Classes[i]))
            found++;
    }

    printf("[+] %s: %d classes dumped\n\n", moduleName, found);
}

void Dumper::SaveHpp(const std::string& path) {
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir);

    std::ofstream f(path);
    if (!f.is_open()) { printf("[-] Cannot write %s\n", path.c_str()); return; }

    f << "// ==========================================================\n";
    f << "// Dota 2 Offsets — Auto Generated\n";
    f << "// Date:    " << __DATE__ << " " << __TIME__ << "\n";
    f << "// Classes: " << m_classes.size() << "\n";
    f << "// ==========================================================\n\n";
    f << "#pragma once\n";
    f << "#include <cstdint>\n\n";
    f << "namespace dota2 {\n\n";

    for (const auto& c : m_classes) {
        f << "    // ";
        if (!c.parent.empty()) f << "extends " << c.parent << " | ";
        f << "size 0x" << std::hex << std::uppercase << c.size << std::dec << "\n";
        f << "    namespace " << c.name << " {\n";

        for (const auto& fld : c.fields) {
            std::string padded = fld.name;
            if (padded.size() < 48) padded.resize(48, ' ');

            f << "        constexpr uint32_t " << padded
              << "= 0x" << std::hex << std::uppercase
              << std::setw(4) << std::setfill('0') << fld.offset
              << std::dec << "; // " << fld.type << "\n";
        }
        f << "    }\n\n";
    }

    f << "} // namespace dota2\n";
    f.close();
    printf("[+] Saved: %s\n", path.c_str());
}

void Dumper::SaveJson(const std::string& path) {
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir);

    std::ofstream f(path);
    if (!f.is_open()) { printf("[-] Cannot write %s\n", path.c_str()); return; }

    f << "{\n";
    f << "  \"timestamp\": \"" << __DATE__ << " " << __TIME__ << "\",\n";
    f << "  \"total_classes\": " << m_classes.size() << ",\n";
    f << "  \"classes\": {\n";

    for (size_t ci = 0; ci < m_classes.size(); ci++) {
        const auto& c = m_classes[ci];
        f << "    \"" << c.name << "\": {\n";
        f << "      \"parent\": \"" << c.parent << "\",\n";
        f << "      \"size\": " << c.size << ",\n";
        f << "      \"fields\": {\n";

        for (size_t fi = 0; fi < c.fields.size(); fi++) {
            const auto& fld = c.fields[fi];
            f << "        \"" << fld.name << "\": { "
              << "\"offset\": " << fld.offset << ", "
              << "\"type\": \"" << fld.type << "\" }";
            if (fi + 1 < c.fields.size()) f << ",";
            f << "\n";
        }
        f << "      }\n";
        f << "    }";
        if (ci + 1 < m_classes.size()) f << ",";
        f << "\n";
    }

    f << "  }\n}\n";
    f.close();
    printf("[+] Saved: %s\n", path.c_str());
}
