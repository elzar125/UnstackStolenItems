#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <REL/Relocation.h>

namespace UnstackStolenItems {

    // Hook for InventoryChanges::AddItem to prevent stolen items from
    // stacking with legitimate items.
    //
    // Address Library IDs:
    // - SE: 11448
    // - AE: 11594
    //
    // The original function searches for existing inventory entries by
    // base form only, ignoring ownership. Our hook adds ownership checking
    // to ensure stolen and legitimate items stay in separate entries.
    
    struct CompareExtraDataListsHook {
        static void Install();
    };

}
