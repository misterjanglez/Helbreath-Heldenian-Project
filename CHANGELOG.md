# Changelog

## [Server 0.1.23 / Client 0.2.51] - 2026-03-03

### Ground Item Attribute Refactor

**Bug fixes:**
- Ground items now display proper names with prefixes and enchant levels (e.g., "Ancient Rapier+3" instead of "Rapier")
- Item obtained event log message now shows the full formatted name
- Ground item tooltip now shows only the item name — no longer displays stat breakdowns (e.g., "Hitting Probability +13")
- Ground item tooltip and sprite now use the correct dye color from the color palette (was showing green instead of the prefix color)
- Fixed `& 0x0F` mask on ground item color index that truncated palette indices above 15

**Refactoring:**
- Added shared `item_instance_data` struct — canonical type for all per-instance item data (17 fields)
- Added `PacketEventGroundItem` packet — carries full item instance data for ground item events, replacing packed `uint32_t` bitmask
- Added `CItem::to_instance_data()` conversion method
- Server: Added `send_ground_item_event()`, replacing 25 manual `send_event_to_near_client_type_b` calls across 9 files
- Server: Simplified `Map::get_item` from 6 decomposed output params to `CItem** remain`
- Server: Consolidated 5 manual field-by-field copy blocks with `copy_attributes_to()` template
- Client: Replaced CTile's 4 separate item fields with single `item_instance_data m_item`
- Client: `MapData::set_item` now accepts `item_instance_data` struct
- Client: Exchange dialog uses `item_instance_data` instead of packed `dw_v1`
- Client: `ItemNameFormatter` — new `format(item_instance_data)` overload; `format(CItem*)` delegates to it
- Removed all packed `uint32_t` attribute code (`pack_attributes_uint32`, `unpack_legacy_attribute`, `pack_exchange_attribute`)

**Files changed:** 28 (1 new, 27 modified)
