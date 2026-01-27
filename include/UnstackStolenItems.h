#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <REL/Relocation.h>

namespace UnstackStolenItems {

    // Hook for InventoryChanges::AddItem
    
    struct CompareExtraDataListsHook {
        static void Install();
    };

}
