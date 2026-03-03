#include "UnstackStolenItems.h"
#include <SimpleIni.h>
#include <MinHook.h>
#include <atomic>
#include <vector>
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
    // GLOBALS
    // ============================================================
    
    static std::atomic<uint64_t> g_addToItemListCalls{0};
    static std::atomic<uint64_t> g_splitCalls{0};
    
    // AddToItemList — SE ID: 50978, AE ID: 51011
    struct AddToItemListHook {
        static void* thunk(void* a_itemList, RE::InventoryEntryData* a_entry, void* a_param3);

        static inline std::uintptr_t func{0};
        using func_t = void*(*)(void*, RE::InventoryEntryData*, void*);

        static func_t original() { return reinterpret_cast<func_t>(func); }
    };

    // ============================================================
    // MENU HANDLER 
    // ============================================================
    
    class MenuEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuEventHandler* GetSingleton() {
            static MenuEventHandler singleton;
            return &singleton;
        }
        
        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*
        ) override {
            if (Config::Get().debugLogging && a_event && a_event->opening && 
                a_event->menuName == RE::InventoryMenu::MENU_NAME) {
                SKSE::log::info("=== INVENTORY OPENED ===");
                SKSE::log::info("  AddToItemList calls: {}", g_addToItemListCalls.load());
                SKSE::log::info("  Split operations: {}", g_splitCalls.load());
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // ============================================================
    // HOOKED ADDTOITEMLIST
    // ============================================================

    void* AddToItemListHook::thunk(
        void* a_itemList,
        RE::InventoryEntryData* a_entry,
        void* a_param3
    ) {
        g_addToItemListCalls++;

        if (!a_entry || !a_itemList) {
            return original()(a_itemList, a_entry, a_param3);
        }

        if (!a_entry->object || !a_entry->extraLists) {
            return original()(a_itemList, a_entry, a_param3);
        }

        // If the entire entry is owned by the player, nothing is stolen — skip
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || a_entry->IsOwnedBy(player, true)) {
            return original()(a_itemList, a_entry, a_param3);
        }

        // Count stolen items
        std::int32_t stolenCount = 0;
        std::vector<RE::ExtraDataList*> stolenLists;
        std::vector<RE::ExtraDataList*> legitLists;

        for (auto* xList : *a_entry->extraLists) {
            if (xList) {
                if (xList->HasType(RE::ExtraDataType::kOwnership)) {
                    auto* countExtra = xList->GetByType<RE::ExtraCount>();
                    stolenCount += countExtra ? countExtra->count : 1;
                    stolenLists.push_back(xList);
                } else {
                    legitLists.push_back(xList);
                }
            }
        }

        std::int32_t legitCount = a_entry->countDelta - stolenCount;

        // Check if mixed
        if (stolenCount > 0 && legitCount > 0) {
            g_splitCalls++;

            if (Config::Get().debugLogging) {
                SKSE::log::info("Splitting: {} (stolen={}, legit={})",
                    a_entry->object ? a_entry->object->GetName() : "null",
                    stolenCount, legitCount);
            }

            // Create new entry for stolen items
            auto* stolenEntry = new RE::InventoryEntryData(a_entry->object, stolenCount);
            stolenEntry->extraLists = new RE::BSSimpleList<RE::ExtraDataList*>();
            for (auto* xList : stolenLists) {
                stolenEntry->extraLists->push_front(xList);
            }

            // Create new entry for legitimate items
            auto* legitEntry = new RE::InventoryEntryData(a_entry->object, legitCount);
            if (!legitLists.empty()) {
                legitEntry->extraLists = new RE::BSSimpleList<RE::ExtraDataList*>();
                for (auto* xList : legitLists) {
                    legitEntry->extraLists->push_front(xList);
                }
            }

            // Add stolen entry
            original()(a_itemList, stolenEntry, a_param3);

            // Add legitimate entry and return its result
            return original()(a_itemList, legitEntry, a_param3);
        }

        // Not mixed - pass through
        return original()(a_itemList, a_entry, a_param3);
    }

    // ============================================================
    // INSTALLATION
    // ============================================================

    void CompareExtraDataListsHook::Install() {
        // Load config
        Config::Get().Load();
        if (Config::Get().debugLogging) {
            SKSE::log::info("Debug logging enabled");
        }

        // Initialize MinHook
        if (MH_Initialize() != MH_OK) {
            SKSE::log::error("Failed to initialize MinHook");
            return;
        }

        // AddToItemList hook — SE ID: 50978, AE ID: 51011, VR offset: 0x880410
        {
            std::uintptr_t funcAddr;
            if (REL::Module::IsVR()) {
                funcAddr = REL::Module::get().base() + 0x880410;
            } else {
                funcAddr = REL::RelocationID(50978, 51011).address();
            }
            void* originalFunc = nullptr;

            auto status = MH_CreateHook(
                reinterpret_cast<void*>(funcAddr),
                reinterpret_cast<void*>(&AddToItemListHook::thunk),
                &originalFunc
            );

            if (status != MH_OK) {
                SKSE::log::error("Failed to create AddToItemList hook: {}", MH_StatusToString(status));
                return;
            }

            AddToItemListHook::func = reinterpret_cast<std::uintptr_t>(originalFunc);

            status = MH_EnableHook(reinterpret_cast<void*>(funcAddr));
            if (status != MH_OK) {
                SKSE::log::error("Failed to enable AddToItemList hook: {}", MH_StatusToString(status));
                return;
            }

            SKSE::log::info("AddToItemList hooked via MinHook at {:X}", funcAddr);
        }

        // Register menu handler for diagnostics (only if debug logging enabled)
        if (Config::Get().debugLogging) {
            SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
                if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
                    if (auto ui = RE::UI::GetSingleton()) {
                        ui->AddEventSink(MenuEventHandler::GetSingleton());
                    }
                }
            });
        }
    }

}
