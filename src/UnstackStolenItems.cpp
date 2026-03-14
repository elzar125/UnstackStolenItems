#include "UnstackStolenItems.h"
#include <SimpleIni.h>
#include <MinHook.h>
#include <filesystem>

namespace UnstackStolenItems {

    struct Config {
        bool debugLogging = false;
        bool unstackStolen = true;
        bool unstackStolenIncludeIngredients = false;

        static Config& Get() {
            static Config instance;
            return instance;
        }

        void Load() {
            const auto dataPath = std::filesystem::current_path() / "Data";
            const auto iniPath = dataPath / "SKSE" / "Plugins" / "UnstackStolenItems.ini";

            if (!std::filesystem::exists(iniPath))
                return;

            CSimpleIniA ini;
            ini.SetUnicode();

            if (ini.LoadFile(iniPath.string().c_str()) < 0)
                return;

            debugLogging = ini.GetBoolValue("General", "bDebugLogging", false);
            unstackStolen = ini.GetBoolValue("Unstacking", "bUnstackStolen", true);
            unstackStolenIncludeIngredients = ini.GetBoolValue("Unstacking", "bUnstackStolenIncludeIngredients", false);
        }
    };

    using HasOnlyIgnorableExtraData_t = bool(*)(RE::ExtraDataList*, char);
    using IsNotEqual_t = bool(*)(RE::ExtraDataList*, RE::ExtraDataList*, char);
    using AddExtraList_t = void(*)(RE::InventoryEntryData*, RE::ExtraDataList*, char);

    HasOnlyIgnorableExtraData_t g_origHasOnlyIgnorable = nullptr;
    IsNotEqual_t g_origIsNotEqual = nullptr;
    AddExtraList_t g_origAddExtraList = nullptr;

    thread_local RE::TESBoundObject* g_addExtraListObject = nullptr;

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

    RE::TESBoundObject* FindOwnerObject(RE::ExtraDataList* a_target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;
        auto* changes = player->GetInventoryChanges();
        if (!changes || !changes->entryList) return nullptr;
        for (auto& entry : *changes->entryList) {
            if (!entry || !entry->extraLists) continue;
            for (auto& xList : *entry->extraLists) {
                if (xList == a_target) return entry->object;
            }
        }
        return nullptr;
    }

    bool ShouldSkipStolenUnstack(RE::TESBoundObject* a_obj) {
        if (!a_obj) return false;
        auto& cfg = Config::Get();
        if (!cfg.unstackStolenIncludeIngredients && a_obj->GetFormType() == RE::FormType::Ingredient)
            return true;
        return false;
    }

    bool HasOnlyIgnorableExtraData_Hook(RE::ExtraDataList* a_list, char a_checkOwnership) {
        bool result = g_origHasOnlyIgnorable(a_list, a_checkOwnership);
        if (!result)
            return false;

        auto& cfg = Config::Get();

        if (cfg.unstackStolen && a_checkOwnership && IsStolenExtraDataList(a_list)) {
            if (ShouldSkipStolenUnstack(FindOwnerObject(a_list)))
                return true;
            return false;
        }

        return true;
    }

    bool IsNotEqual_Hook(RE::ExtraDataList* a_lhs, RE::ExtraDataList* a_rhs, char a_param3) {
        auto& cfg = Config::Get();

        bool lhsDistinct = a_lhs && (a_lhs->HasType(RE::ExtraDataType::kTextDisplayData) ||
                                      a_lhs->HasType(RE::ExtraDataType::kEnchantment));
        bool rhsDistinct = a_rhs && (a_rhs->HasType(RE::ExtraDataType::kTextDisplayData) ||
                                      a_rhs->HasType(RE::ExtraDataType::kEnchantment));
        if (lhsDistinct || rhsDistinct)
            return g_origIsNotEqual(a_lhs, a_rhs, a_param3);

        bool skipStolen = ShouldSkipStolenUnstack(g_addExtraListObject);

        if (cfg.unstackStolen && !skipStolen && (IsStolenExtraDataList(a_lhs) != IsStolenExtraDataList(a_rhs)))
            return true;

        bool anyMatch = cfg.unstackStolen && !skipStolen && IsStolenExtraDataList(a_lhs);

        if (anyMatch)
            return false;

        return g_origIsNotEqual(a_lhs, a_rhs, a_param3);
    }

    void AddExtraList_Hook(RE::InventoryEntryData* a_this, RE::ExtraDataList* a_extra, char a_merge) {
        g_addExtraListObject = a_this ? a_this->object : nullptr;
        if (!a_merge && a_extra) {
            auto& cfg = Config::Get();
            bool skipStolen = ShouldSkipStolenUnstack(g_addExtraListObject);

            bool force = cfg.unstackStolen && !skipStolen && IsStolenExtraDataList(a_extra);

            if (force)
                a_merge = 1;
        }
        g_origAddExtraList(a_this, a_extra, a_merge);
        g_addExtraListObject = nullptr;
    }

    void Hooks::Install() {
        Config::Get().Load();

        auto& cfg = Config::Get();
        if (cfg.debugLogging)
            SKSE::log::info("Debug logging enabled");

        SKSE::log::info("Unstacking config: stolen={} (includeIngredients={})",
            cfg.unstackStolen, cfg.unstackStolenIncludeIngredients);

        if (!cfg.unstackStolen) {
            SKSE::log::info("Unstacking disabled, skipping hooks");
            return;
        }

        if (MH_Initialize() != MH_OK) {
            SKSE::log::error("Failed to initialize MinHook");
            return;
        }

        auto resolveAddr = [](std::uint64_t a_seID, std::uint64_t a_aeID, std::uint64_t a_vrOffset) -> std::uintptr_t {
            if (REL::Module::IsVR())
                return REL::Offset(a_vrOffset).address();
            return REL::RelocationID(a_seID, a_aeID).address();
        };

        auto installHook = [](std::uintptr_t a_target, void* a_detour, void** a_original, const char* a_name) -> bool {
            auto status = MH_CreateHook(reinterpret_cast<void*>(a_target), a_detour, a_original);
            if (status != MH_OK) {
                SKSE::log::error("Failed to hook {}: {}", a_name, MH_StatusToString(status));
                return false;
            }
            MH_EnableHook(reinterpret_cast<void*>(a_target));
            return true;
        };

        if (!installHook(resolveAddr(11452, 11598, 0x11D220),
                reinterpret_cast<void*>(&HasOnlyIgnorableExtraData_Hook),
                reinterpret_cast<void**>(&g_origHasOnlyIgnorable),
                "HasOnlyIgnorableExtraData"))
            return;

        if (!installHook(resolveAddr(11448, 11594, 0x119B10),
                reinterpret_cast<void*>(&IsNotEqual_Hook),
                reinterpret_cast<void**>(&g_origIsNotEqual),
                "IsNotEqual"))
            return;

        if (!installHook(resolveAddr(15748, 15986, 0x1E6AB0),
                reinterpret_cast<void*>(&AddExtraList_Hook),
                reinterpret_cast<void**>(&g_origAddExtraList),
                "AddExtraList"))
            return;

        SKSE::log::info("Hooks installed");
    }

    void Hooks::MergeInventoryLists() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto* changes = player->GetInventoryChanges();
        if (!changes || !changes->entryList) return;

        if (!g_origIsNotEqual) {
            SKSE::log::warn("MergeInventoryLists: original IsNotEqual not available, skipping");
            return;
        }

        auto& cfg = Config::Get();
        int totalMerged = 0;

        for (auto& entry : *changes->entryList) {
            if (!entry || !entry->extraLists) continue;

            std::vector<RE::ExtraDataList*> lists;
            for (auto& xList : *entry->extraLists) {
                if (xList) lists.push_back(xList);
            }

            if (lists.size() < 2) continue;

            std::vector<bool> removed(lists.size(), false);
            int mergedThisEntry = 0;

            for (std::size_t i = 0; i < lists.size(); i++) {
                if (removed[i]) continue;
                for (std::size_t j = i + 1; j < lists.size(); j++) {
                    if (removed[j]) continue;
                    if (lists[i]->HasType(RE::ExtraDataType::kTextDisplayData) ||
                        lists[j]->HasType(RE::ExtraDataType::kTextDisplayData) ||
                        lists[i]->HasType(RE::ExtraDataType::kEnchantment) ||
                        lists[j]->HasType(RE::ExtraDataType::kEnchantment))
                        continue;
                    bool eitherHotkeyed = lists[i]->HasType(RE::ExtraDataType::kHotkey) !=
                                          lists[j]->HasType(RE::ExtraDataType::kHotkey);
                    if (eitherHotkeyed) continue;
                    if (!g_origIsNotEqual(lists[i], lists[j], 1) &&
                        !g_origIsNotEqual(lists[j], lists[i], 1)) {
                        auto countI = lists[i]->GetCount();
                        auto countJ = lists[j]->GetCount();
                        lists[i]->SetCount(static_cast<std::uint16_t>(countI + countJ));
                        removed[j] = true;
                        mergedThisEntry++;
                    }
                }
            }

            if (mergedThisEntry == 0) continue;

            std::vector<RE::ExtraDataList*> surviving;
            for (std::size_t i = 0; i < lists.size(); i++) {
                if (!removed[i]) surviving.push_back(lists[i]);
            }

            entry->extraLists->clear();
            for (auto it = surviving.rbegin(); it != surviving.rend(); ++it) {
                entry->extraLists->push_front(*it);
            }

            totalMerged += mergedThisEntry;

            if (cfg.debugLogging) {
                const char* name = entry->object ? entry->object->GetName() : "???";
                SKSE::log::info("Merged {} ExtraDataLists for {} ({} remaining)",
                    mergedThisEntry, name, surviving.size());
            }
        }

        if (totalMerged > 0) {
            SKSE::log::info("Post-load merge: consolidated {} ExtraDataLists", totalMerged);
        }
    }

}
