#pragma once
#include "IDialogBox.h"
#include <string>

class DialogBox_GuildOperation : public IDialogBox
{
public:
	DialogBox_GuildOperation(CGame* game);
	~DialogBox_GuildOperation() override = default;

	void on_draw() override;
	bool on_click() override;

	struct GuildOp { std::string name; char op_mode = 0; };

	void put(const char* name, char op_mode);
	void shift();
	void reset();

private:
	GuildOp m_op_list[100]{};

	void draw_join_request(short sX, short sY);
	void draw_dismiss_request(short sX, short sY);
	void draw_info_message(short sX, short sY, short mouse_x, short mouse_y, int mode);
};
