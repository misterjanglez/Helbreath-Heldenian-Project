#pragma once

#include "DialogBoxIDs.h"
#include "CommonTypes.h"
#include "GameConstants.h"   // ui_layout button slots, shared by every dialog
#include <cstdint>
#include <string>
#include <string_view>

// ui_rect (dialog-relative hit-test rect) comes in with CommonTypes.h.

class CGame;
class CPlayer;
class DialogBoxManager;
namespace hb { namespace net { struct packet_base; } }

// Result of on_press - determines how the click is handled
enum class PressResult
{
	Normal = 0,        // Normal click, allow dialog dragging
	ItemSelected = 1,  // Item/equipment selected, dialog handles CursorTarget
	ScrollClaimed = -1 // Scroll/slider region claimed, prevent dragging
};

class IDialogBox
{
public:
	IDialogBox(DialogBoxId::Type id, CGame* game);
	virtual ~IDialogBox() = default;

	// --- Core virtual methods (no mouse params — use hb::shared::input) ---
	virtual void on_draw() = 0;
	virtual bool on_click() = 0;

	// Optional virtual methods - override as needed
	virtual void on_update() {}  // Called once per frame for enabled dialogs
	virtual bool on_double_click() { return false; }

	// Called on mouse button down within dialog bounds
	virtual PressResult on_press() { return PressResult::Normal; }

	virtual bool on_item_drop() { return false; }  // Item dropped on dialog
	virtual bool on_enable(int type, int64_t v1, int v2, const char* string) { return true; }
	virtual bool on_disable() { return true; }

	// --- Behavioral flags (override in subclasses) ---
	virtual bool is_draggable() const { return true; }
	virtual bool cancels_text_input_on_enable() const { return true; }

	// --- Manager injection (called by DialogBoxManager::register_dialog_box) ---
	void set_manager(DialogBoxManager& mgr) { m_manager = &mgr; }

	// --- Common state ---
	DialogBoxId::Type get_id() const { return m_id; }
	bool is_enabled() const { return m_enabled; }
	void set_enabled(bool enabled) { m_enabled = enabled; }

	short m_x = 0, m_y = 0;
	short m_size_x = 0, m_size_y = 0;
	bool m_is_scroll_selected = false;
	bool m_can_close_on_right_click = true;

protected:
	// Returns true if mouse cursor is inside the rect (relative to dialog position)
	bool mouse_in(const ui_rect& r) const;

	// Themed flat button. The rect supplies both the face and the hover test, so
	// a button's chrome and its click target cannot disagree — the frame-pair
	// version they replace drew a fixed 74x20 sprite wherever it was told and
	// hit-tested a rect that only happened to line up with it.
	void draw_button(int sX, int sY, const ui_rect& r, const char* caption, bool enabled = true);

	// Wheel + track-drag input for a vertical list, applied only while this
	// dialog is on top: `bar` is the dialog-relative scrollbar rect (its h is
	// the track), `max_scroll` the highest first-visible index; `scroll` is
	// updated and clamped in place. Drawing the thumb stays with the caller
	// (ui_theme::scrollbar) — how much is visible differs per dialog, where a
	// drag lands does not.
	void handle_vscroll(const ui_rect& bar, int max_scroll, int& scroll);

	// The text_input_manager lifecycle for a dialog-owned field, called every
	// frame the field should be live: starts the input when this dialog is on
	// top and nothing else holds it, and re-anchors it when the dialog has
	// been dragged. Pair with unbind_text_input on mode exit / disable.
	// (x, y) are SCREEN coordinates of the text baseline.
	void bind_text_input(int x, int y, unsigned char max_len, std::string& buffer,
		std::string_view filter = {}, bool hidden = false);
	void unbind_text_input();

	// Helper methods - delegate to CGame
	void draw_new_dialog_box(char type, int sX, int sY, int frame, bool is_no_color_key = false, bool is_trans = false);
	void put_string(int iX, int iY, const char* string, const hb::shared::render::Color& color);
	void put_aligned_string(int x1, int x2, int iY, const char* string, const hb::shared::render::Color& color = GameColors::UILabel);
	void add_event_list(const char* txt, char color = 0, bool dup_allow = true);
	bool send_game_packet_impl(const hb::net::packet_base& pkt, size_t size, bool encrypt = true);

	template<typename PacketT>
	bool send_game_packet(const PacketT& pkt, bool encrypt = true)
	{
		return send_game_packet_impl(pkt, sizeof(PacketT), encrypt);
	}
	void set_default_rect(short sX, short sY, short size_x, short size_y);

	// Dialog management helpers
	void enable_dialog_box(DialogBoxId::Type id, int type = 0, int64_t v1 = 0, int v2 = 0, const char* string = nullptr);
	void disable_dialog_box(DialogBoxId::Type id);
	void disable_this_dialog();

	// Inter-dialog communication
	IDialogBox* get_dialog_box(DialogBoxId::Type id);
	template<typename T>
	T* get_dialog_box_as(DialogBoxId::Type id) { return static_cast<T*>(get_dialog_box(id)); }

	// Player access via manager
	CPlayer& player() const;
	DialogBoxManager& manager() const { return *m_manager; }

	// Direct access to game - use m_game->member for all game state
	CGame* m_game;
	DialogBoxId::Type m_id;

private:
	DialogBoxManager* m_manager = nullptr;
	bool m_enabled = false;
	// Where bind_text_input last anchored the field; -1 = not bound.
	int m_bound_input_x = -1, m_bound_input_y = -1;
};
