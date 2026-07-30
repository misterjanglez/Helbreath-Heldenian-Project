#pragma once
#include "IDialogBox.h"

// 4:3 resolution options
struct Resolution {
	int width;
	int height;
};

class DialogBox_SysMenu : public IDialogBox
{
public:
	// Tab indices
	enum Tab
	{
		TAB_GENERAL = 0,
		TAB_GRAPHICS,
		TAB_AUDIO,
		TAB_SYSTEM,
		TAB_COUNT
	};

	DialogBox_SysMenu(CGame* game);
	~DialogBox_SysMenu() override = default;

	void on_draw() override;
	bool on_click() override;
	PressResult on_press() override;

	// Resolution management
	static const Resolution s_Resolutions[];
	static const int s_NumResolutions;
	static int get_current_resolution_index();
	static int get_nearest_resolution_index(int width, int height);
	static void cycle_resolution();
	static void apply_resolution(int index);

private:
	// Tab drawing
	void draw_tabs(short sX, short sY);
	void draw_tab_content(short sX, short sY, short mouse_x, short mouse_y, char lb);

	// Individual tab content
	void draw_general_tab(short sX, short sY);
	void draw_graphics_tab(short sX, short sY);
	void draw_audio_tab(short sX, short sY, short mouse_x, short mouse_y, char lb);
	void draw_system_tab(short sX, short sY);

	// Click handlers for each tab
	bool on_click_general();
	bool on_click_graphics(short sX, short sY);
	bool on_click_audio(short sX, short sY);
	bool on_click_system(short sX, short sY);

	// Helper to draw On/Off toggle
	void draw_toggle(int x, int y, bool enabled);
	bool is_in_toggle_area(int x, int y);

	// One selectable segment of a value box. The box is drawn once for the whole
	// row, so a segment paints no face of its own — only its caption, lit when it
	// is the current value or the cursor is over it.
	void draw_option(int x, int y, int w, const char* text,
		bool selected, bool enabled = true);

	// One "Label: [On/Off] ---o------" row of the audio tab. All five were the
	// same nine lines with a different volume accessor.
	void draw_volume_row(int label_x, int toggle_x, int slider_x, int y,
		const char* caption, bool available, bool enabled, int volume);

	int m_iActiveTab;
	int m_graphics_scroll_offset = 0;
};
