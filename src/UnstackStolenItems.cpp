#include "UnstackStolenItems.h"
#include <SimpleIni.h>
#include <MinHook.h>
#include <filesystem>

namespace UnstackStolenItems {

    // ============================================================
    // CONFIG
    // ============================================================

    struct Config {
        bool debugLogging = false;

        static Config& Get() {
            static Config instance;
            return instance;
        }

        void Load() {
            const auto dataPath = std::filesystem::current_path() / "Data";
            const auto iniPath = dataPath / "SKSE" / "Plugins" / "UnstackStolenItems.ini";

            if (!std::filesystem::exists(iniPath)) {
                return;
            }

            CSimpleIniA ini;
            ini.SetUnicode();

            if (ini.LoadFile(iniPath.string().c_str()) < 0) {
                return;
            }

            debugLogging = ini.GetBoolValue("General", "bDebugLogging", false);
        }
    };

    // ============================================================
    // HOOK TYPEDEFS AND GLOBALS
    // ============================================================

    using HasOnlyIgnorableExtraData_t = bool(*)(RE::ExtraDataList*, char);
    using IsNotEqual_t = bool(*)(RE::ExtraDataList*, RE::ExtraDataList*, char);
    using AddExtraList_t = void(*)(RE::InventoryEntryData*, RE::ExtraDataList*, char);

    HasOnlyIgnorableExtraData_t g_origHasOnlyIgnorable = nullptr;
    IsNotEqual_t g_origIsNotEqual = nullptr;
    AddExtraList_t g_origAddExtraList = nullptr;

    // ============================================================
    // HELPERS
    // ============================================================

    bool IsStolenExtraDataList(RE::ExtraDataList* xList) {
        if (!xList || !xList->HasType(RE::ExtraDataType::kOwnership))
            return false;

        auto* owner = xList->GetOwner();
        if (!owner)
            return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return false;

        return (owner != player && owner != player->GetActorBase());
    }

    // ============================================================
    // HOOK 1: HasOnlyIgnorableExtraData  (SE 11452, AE 11598)
    //
    // When the display builder asks "is this ExtraDataList
    // ignorable?" with ownership check enabled, we override
    // the answer for stolen items: return false so they get
    // their own display row.
    // ============================================================

    bool HasOnlyIgnorableExtraData_Hook(RE::ExtraDataList* a_list, char a_checkOwnership) {
        bool result = g_origHasOnlyIgnorable(a_list, a_checkOwnership);
        if (result && a_checkOwnership && IsStolenExtraDataList(a_list)) {
            return false;
        }
        return result;
    }

    // ============================================================
    // HOOK 2: ExtraDataList::IsNotEqual  (SE 11448, AE 11594)
    //
    // When comparing two ExtraDataLists and both are stolen,
    // force "equal" so AddExtraList merges their counts.
    // This prevents x1 stacks.
    // ============================================================

    bool IsNotEqual_Hook(RE::ExtraDataList* a_lhs, RE::ExtraDataList* a_rhs, char a_param3) {
        if (IsStolenExtraDataList(a_lhs) && IsStolenExtraDataList(a_rhs)) {
            return false;
        }
        return g_origIsNotEqual(a_lhs, a_rhs, a_param3);
    }

    // ============================================================
    // HOOK 3: InventoryEntryData::AddExtraList  (SE 15748, AE 15986)
    //
    // The game calls AddExtraList with merge=false for stolen
    // items, so the IsNotEqual merge path is never reached.
    // We force merge=true when adding a stolen ExtraDataList,
    // which triggers the IsNotEqual comparison and allows
    // stolen items to consolidate into one ExtraDataList.
    // ============================================================

    void AddExtraList_Hook(RE::InventoryEntryData* a_this, RE::ExtraDataList* a_extra, char a_merge) {
        if (!a_merge && a_extra && IsStolenExtraDataList(a_extra)) {
            if (Config::Get().debugLogging) {
                SKSE::log::info("AddExtraList: forcing merge=true for stolen ExtraDataList");
            }
            a_merge = 1;
        }
        g_origAddExtraList(a_this, a_extra, a_merge);
    }

    // ============================================================
    // HOOK INSTALLATION
    // ============================================================

    void Hooks::Install() {
        Config::Get().Load();
        if (Config::Get().debugLogging) {
            SKSE::log::info("Debug logging enabled");
        }

        if (MH_Initialize() != MH_OK) {
            SKSE::log::error("Failed to initialize MinHook");
            return;
        }

        // Hook 1: HasOnlyIgnorableExtraData
        auto hasOnlyIgnorableAddr = REL::RelocationID(11452, 11598).address();
        auto status = MH_CreateHook(
            reinterpret_cast<void*>(hasOnlyIgnorableAddr),
            reinterpret_cast<void*>(&HasOnlyIgnorableExtraData_Hook),
            reinterpret_cast<void**>(&g_origHasOnlyIgnorable)
        );
        if (status != MH_OK) {
            SKSE::log::error("Failed to hook HasOnlyIgnorableExtraData: {}", MH_StatusToString(status));
            return;
        }
        MH_EnableHook(reinterpret_cast<void*>(hasOnlyIgnorableAddr));

        // Hook 2: IsNotEqual
        auto isNotEqualAddr = REL::RelocationID(11448, 11594).address();
        status = MH_CreateHook(
            reinterpret_cast<void*>(isNotEqualAddr),
            reinterpret_cast<void*>(&IsNotEqual_Hook),
            reinterpret_cast<void**>(&g_origIsNotEqual)
        );
        if (status != MH_OK) {
            SKSE::log::error("Failed to hook IsNotEqual: {}", MH_StatusToString(status));
            return;
        }
        MH_EnableHook(reinterpret_cast<void*>(isNotEqualAddr));

        // Hook 3: AddExtraList
        auto addExtraListAddr = REL::RelocationID(15748, 15986).address();
        status = MH_CreateHook(
            reinterpret_cast<void*>(addExtraListAddr),
            reinterpret_cast<void*>(&AddExtraList_Hook),
            reinterpret_cast<void**>(&g_origAddExtraList)
        );
        if (status != MH_OK) {
            SKSE::log::error("Failed to hook AddExtraList: {}", MH_StatusToString(status));
            return;
        }
        MH_EnableHook(reinterpret_cast<void*>(addExtraListAddr));

        SKSE::log::info("UnstackStolenItems hooks installed (HasOnlyIgnorableExtraData + IsNotEqual + AddExtraList)");
    }

}
