// TESTER MENU — entire file is tester-only
#pragma once
#ifdef TESTER_ONLY
#include "IDialogBox.h"
#include "Packet/PacketNotify.h"
#include <string>

class DialogBox_NpcSpawner : public IDialogBox
{
public:
	DialogBox_NpcSpawner(CGame* game);
	~DialogBox_NpcSpawner() override = default;

	void on_draw() override;
	bool on_click() override;
	bool on_disable() override;

	void receive_search_results(const hb::net::PacketNotifyTesterNpcSearchResult* pkt);
	void on_enter_pressed();

	bool on_enable(int type, int64_t v1, int v2, const char* string) override;
private:
	// Search state
	std::string m_search_text;
	std::string m_last_sent_search;
	int m_result_count = 0;
	hb::net::TesterNpcSearchEntry m_results[50]{};
	int m_selected_index = -1;
	int m_scroll_offset = 0;
	bool m_initial_load = false;

	// Track dialog position to fix cursor drift when the box is dragged
	short m_last_sx = 0;
	short m_last_sy = 0;

	// Amount to spawn (1-50)
	int m_amount = 1;
};
#endif // TESTER_ONLY
