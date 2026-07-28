// SharedItem.h: Master include file for the unified Item system
//
// Include this single header to get access to all Item-related types:
//   - ItemEnums.h       - Type-safe enums (EquipPos, ItemType, etc.)
//   - ModifierIds.h     - Unified modifier ID space (via Item.h)
//   - Item.h            - Unified CItem class
//
// Note: Display names are now stored directly in the database.
// The server sends display names in m_name, no client-side lookup needed.
//
//////////////////////////////////////////////////////////////////////

#pragma once

// Item enums and constants
#include "ItemEnums.h"

// Unified CItem class
#include "Item.h"
