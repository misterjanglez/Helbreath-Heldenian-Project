// TESTER MENU — entire file is tester-only
#pragma once
#ifdef TESTER_ONLY
#include "IDialogBox.h"
#include "CommonTypes.h"
#include "Packet/PacketNotify.h"
#include "Item/ModifierIds.h"
#include "Item/ItemAttributeData.h"
#include "ModifierCatalog.h"
#include <cstdint>
#include <string>
#include <vector>

class DialogBox_ItemCreator : public IDialogBox
{
public:
	DialogBox_ItemCreator(CGame* game);
	~DialogBox_ItemCreator() override = default;

	void on_draw() override;
	bool on_click() override;
	bool on_disable() override;

	void receive_search_results(const hb::net::PacketNotifyTesterItemSearchResult* pkt);
	void on_enter_pressed();

	// The server answers a mint with a NoticeMsg — "Created Nx ..." or
	// "Mint rejected: <reason>". While the creator is waiting for that answer
	// it claims the notice so the reason lands beside the pickers that caused
	// it instead of scrolling away in the event list.
	bool wants_server_notice() const { return m_awaiting_mint_reply; }
	void receive_server_notice(const char* text);

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;
private:
	enum class item_category { none, weapon, armor, magic_weapon };

	int m_page = 0;

	// Search state
	std::string m_search_text;
	std::string m_last_sent_search;
	int m_result_count = 0;
	hb::net::TesterItemSearchEntry m_results[50]{};
	int m_selected_index = -1;
	int m_scroll_offset = 0;
	bool m_initial_load = false;

	// Track dialog position to fix cursor drift
	short m_last_sx = 0;
	short m_last_sy = 0;

	// Attribute configuration
	int m_prefix_index = 0;
	int m_prefix_value = 1;
	int m_secondary_index = 0;
	int m_secondary_value = 1;
	static constexpr int max_value = 13;
	int m_enchant_value = 0;    // 0-15 (bits 28-31 of attribute)
	int m_item_count = 1;       // 1-10

	item_category m_category = item_category::none;
	struct attr_option
	{
		int type;
		const char* name;
		int multiplier;
	};
	std::vector<attr_option> m_valid_prefixes;
	std::vector<attr_option> m_valid_secondaries;

	// --- Tiered mode (Item Tiers 4-D) ---------------------------------------
	// The GM states the whole instance: a Tier and exactly `tier` modifier
	// slots. The pickers mirror the two structural rules the replicated
	// catalog knows — the min-tier ladder and one modifier per Bucket — so a
	// legal mint round-trips; Bands and tier scope stay the server's word
	// (bands do not replicate, and the search row cannot classify the item).
	struct tier_slot
	{
		uint8_t modifier_id = 0;   // unified modifier ID; 0 = empty
		int value = 1;             // stored (pre-multiplier) roll value
		int value2 = 1;            // pairs only
	};
	// One offerable catalog row. Everything else about the modifier — its
	// multiplier, whether it is a pair — is read from the catalog by ID at
	// the point of use, so this never goes stale against a `reload tiers`.
	struct tier_option
	{
		uint8_t modifier_id;
		std::string label;
	};

	int m_tier = 0;                // 0 = untiered (plain mint), 1-4 = Common..Legendary
	tier_slot m_tier_slots[hb::shared::item::modifier_slot_count];
	bool m_awaiting_mint_reply = false;
	std::string m_server_notice;
	bool m_server_notice_ok = false;

	// The raw values a modifier may legally hold, derived from its replicated
	// display band and multiplier. Empty (max < min) = no legal value.
	struct raw_range
	{
		int min = 1;
		int max = 0;
	};
	static raw_range legal_raw_range(const modifier_catalog_entry& row);

	// Dropdown state
	enum class dropdown_id : int
	{
		none = -1,
		prefix_type, prefix_value,
		effect_type, effect_value,
		upgrade, count,
		tier, mod_type, mod_value, mod_value2
	};
	dropdown_id m_open_dropdown = dropdown_id::none;
	int m_open_slot = 0;           // modifier slot the open mod_* dropdown belongs to
	int m_dropdown_scroll = 0;
	static constexpr int dropdown_h = 14;
	static constexpr int dropdown_max_vis = 8;

	void build_valid_options(int16_t effect_type);
	static item_category classify_item(int16_t effect_type);
	static const char* category_name(item_category cat);
	std::string build_preview_string() const;

	void draw_search_page(short sX, short sY, short size_x, short mouse_x, short mouse_y, short z);
	void draw_configure_page(short sX, short sY, short size_x, short mouse_x, short mouse_y, short z);
	bool on_click_search(short sX, short sY, short size_x);
	bool on_click_configure(short sX, short sY, short size_x);

	// Tiered page
	// The requested instance, built once from the pickers — the preview
	// formats it and the mint packet carries it, so what the GM reads is
	// exactly what the server judges.
	hb::shared::item::item_attribute_data build_requested_attributes() const;
	void reset_tier_state();
	void apply_tier_change(int tier);
	std::vector<tier_option> tier_options_for_slot(int slot) const;
	std::vector<std::string> value_options(const modifier_catalog_entry& row) const;
	std::string missing_tier_input() const;
	void send_tiered_mint();
	void draw_tiered_body(short sX, short sY, short size_x, short mouse_x, short mouse_y);
	bool on_click_tiered_body(short sX, short sY);
	// The one description of the open dropdown — anchor, options and the
	// selected row — so the overlay and the click handler can never disagree
	// about what is on screen.
	std::vector<std::string> open_dropdown_options(short sX, short sY,
		int& out_x, int& out_y, int& out_w, int& out_selected) const;

	// UI helpers
	// `text_color` lets the Tier field carry the replicated Tier color; every
	// other field keeps the dialog's gold.
	void draw_dropdown_field(int x, int y, int w, const char* text, bool is_open, bool is_hover,
		const hb::shared::render::Color& text_color = GameColors::UIPaleYellow);
};
#endif // TESTER_ONLY
