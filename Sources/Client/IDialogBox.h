#pragma once

#include "DialogBoxIDs.h"
#include "CommonTypes.h"
#include <cstdint>

// Simple rect for dialog-relative hit-testing
struct ui_rect { int x, y, w, h; };

class CGame;
class DialogBoxManager;

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

	// Helper methods - delegate to CGame
	void draw_new_dialog_box(char type, int sX, int sY, int frame, bool is_no_color_key = false, bool is_trans = false);
	void put_string(int iX, int iY, const char* string, const hb::shared::render::Color& color);
	void put_aligned_string(int x1, int x2, int iY, const char* string, const hb::shared::render::Color& color = GameColors::UIBlack);
	void play_sound_effect(char type, int num, int dist, long lPan = 0);
	void add_event_list(const char* txt, char color = 0, bool dup_allow = true);
	bool send_command(uint32_t msg_id, uint16_t command, char dir, int v1, int v2, int v3, const char* string, int v4 = 0);
	void set_default_rect(short sX, short sY, short size_x, short size_y);

	// Dialog management helpers
	void enable_dialog_box(DialogBoxId::Type id, int type = 0, int64_t v1 = 0, int v2 = 0, const char* string = nullptr);
	void disable_dialog_box(DialogBoxId::Type id);
	void disable_this_dialog();

	// Inter-dialog communication
	IDialogBox* get_dialog_box(DialogBoxId::Type id);
	template<typename T>
	T* get_dialog_box_as(DialogBoxId::Type id) { return static_cast<T*>(get_dialog_box(id)); }

	// Direct access to game - use m_game->member for all game state
	CGame* m_game;
	DialogBoxId::Type m_id;

private:
	bool m_enabled = false;
};
