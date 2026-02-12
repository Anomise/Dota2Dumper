#include "dump.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <filesystem>

// Все интересные классы
static const char* g_Classes[] = {
    // Base entity
    "C_BaseEntity",
    "CBaseEntity",
    "C_BaseModelEntity",
    "CBaseModelEntity",
    "CBodyComponent",
    "CBodyComponentPoint",
    "CGameSceneNode",
    "CSkeletonInstance",
    "CEntityIdentity",
    "CEntityInstance",

    // Player
    "CBasePlayerController",
    "C_BasePlayerController",
    "C_BasePlayerPawn",
    "CBasePlayerPawn",
    "C_DOTAPlayerController",
    "CDOTAPlayerController",
    "CDOTA_PlayerController",

    // NPC base
    "C_DOTA_BaseNPC",
    "CDOTA_BaseNPC",

    // Heroes
    "C_DOTA_BaseNPC_Hero",
    "CDOTA_BaseNPC_Hero",

    // Creeps
    "C_DOTA_BaseNPC_Creep",
    "C_DOTA_BaseNPC_Creep_Lane",
    "C_DOTA_BaseNPC_Creep_Neutral",
    "C_DOTA_BaseNPC_Creep_Siege",

    // Buildings
    "C_DOTA_BaseNPC_Tower",
    "C_DOTA_BaseNPC_Building",
    "C_DOTA_BaseNPC_Barracks",
    "C_DOTA_BaseNPC_Fort",
    "CDOTA_BaseNPC_Tower",
    "CDOTA_BaseNPC_Building",

    // Abilities
    "C_DOTABaseAbility",
    "CDOTABaseAbility",
    "C_DOTA_Ability_AttributeBonus",

    // Items
    "C_DOTA_Item",
    "CDOTA_Item",
    "C_DOTA_Item_Physical",

    // Modifiers
    "CDOTA_Buff",
    "CDOTA_Modifier_Lua",
    "CDOTA_ModifierManager",

    // Inventory
    "C_DOTA_UnitInventory",
    "CDOTA_UnitInventory",

    // Game rules
    "C_DOTAGameRules",
    "CDOTAGameRules",
    "C_DOTAGamerules",
    "CDOTAGamerulesProxy",
    "C_DOTAGamerulesProxy",

    // Teams
    "C_DOTATeam",
    "CDOTATeam",
    "C_DOTA_DataRadiant",
    "C_DOTA_DataDire",
    "C_DOTA_DataCustomTeam",
    "C_DOTA_DataSpectator",

    // Units
    "C_DOTA_Unit_Courier",
    "C_DOTA_Unit_Roshan",
    "CDOTA_Unit_Courier",
    "CDOTA_Unit_Roshan",

    // Misc
    "C_DOTA_TempTree",
    "C_DOTA_Item_Rune",
    "C_DOTA_BaseNPC_Projectile",
    "C_DOTA_MapTree",
    "C_DOTA_MinimapBoundary",
    "CDOTA_CameraManager",

    nullptr
};


std::string Dumper::ReadTypeName(void* schemaType) {
    if (!schemaType) return "unknown";
    __try {
        const char* n = *reinterpret_cast<const char**>(
            reinterpret_cast<uintptr_t>(schemaType) + 0x8
        );
        if (n && strlen(n) > 0 && strlen(n) < 256)
            return std::string(n);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return "unknown";
}


bool Dumper::TryDumpClass(CSchemaSystemTypeScope* scope, const char* name) {
    __try {
        auto* binding = scope->FindDeclaredClass(name);
        if (!binding) return false;

        const char* bname = binding->GetName();
        if (!bname || strlen(bname) == 0) return false;

        int16_t fc = binding->GetFieldCount();
        auto* fields = binding->GetFields();
        if (!fields || fc <= 0 || fc > 5000) return false;

        DumpClass dc;
        dc.name   = bname;
        dc.size   = binding->m_size;

        auto* pn = binding->GetParentName();
        dc.parent = pn ? pn : "";

        for (int i = 0; i < fc; i++) {
            __try {
                if (!fields[i].m_name) continue;
                size_t len = strlen(fields[i].m_name);
                if (len == 0 || len > 256) continue;

                DumpField f;
                f.name   = fields[i].m_name;
                f.offset = fields[i].m_offset;
                f.type   = ReadTypeName(fields[i].m_type);
                dc.fields.push_back(f);
            } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        }

        std::sort(dc.fields.begin(), dc.fields.end(),
            [](const DumpField& a, const DumpField& b){ return a.offset < b.offset; });

        if (!dc.fields.empty()) {
            m_classes.push_back(dc);
            printf("  [+] %-45s %3d fields  size=0x%X\n",
                dc.name.c_str(), (int)dc.fields.size(), dc.size);
            return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}


void Dumper::DumpModule(const char* moduleName) {
    if (!g_pSchema) return;

    auto* scope = g_pSchema->FindTypeScopeForModule(moduleName);
    if (!scope) {
        printf("[-] Scope not found: %s\n", moduleName);
        return;
    }

    printf("[*] Dumping %s ...\n", moduleName);

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

    for (auto& c : m_classes) {
        f << "    // ";
        if (!c.parent.empty()) f << "extends " << c.parent << " | ";
        f << "size 0x" << std::hex << std::uppercase << c.size << std::dec << "\n";
        f << "    namespace " << c.name << " {\n";

        for (auto& fld : c.fields) {
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
        auto& c = m_classes[ci];
        f << "    \"" << c.name << "\": {\n";
        f << "      \"parent\": \"" << c.parent << "\",\n";
        f << "      \"size\": " << c.size << ",\n";
        f << "      \"fields\": {\n";

        for (size_t fi = 0; fi < c.fields.size(); fi++) {
            auto& fld = c.fields[fi];
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
