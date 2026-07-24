#ifdef _WIN32

#include "launcher_gui.h"
#include "launcher_layout.h"
#include "launcher_art.h"
#include "launcher_resource.h"

#include <deque>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shobjidl.h>

namespace hb::launcher
{
	namespace
	{
		constexpr const char* window_class_name = "HBMTLauncherWnd";
		constexpr int edit_control_id = 1001;

		// Backdrop pixels live in an RCDATA resource (a C array of this size
		// exceeds MSVC's compiler heap). Locked resources stay valid for the
		// process lifetime — no unlock/free needed.
		const unsigned char* art_pixel_data()
		{
			static const unsigned char* pixels = []() -> const unsigned char*
			{
				HMODULE module = GetModuleHandleA(nullptr);
				HRSRC resource = FindResourceA(module,
					MAKEINTRESOURCEA(IDR_LAUNCHER_ART), MAKEINTRESOURCEA(10) /*RT_RCDATA*/);
				if (!resource || SizeofResource(module, resource) != static_cast<DWORD>(art_bytes))
					return nullptr;
				HGLOBAL loaded = LoadResource(module, resource);
				return loaded ? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
			}();
			return pixels;
		}

		// Widget palette (drawn over the pre-darkened art strip)
		constexpr COLORREF color_strip_text = RGB(232, 226, 214);
		constexpr COLORREF color_dim_text = RGB(150, 144, 132);
		constexpr COLORREF color_button_fill = RGB(46, 42, 52);
		constexpr COLORREF color_button_fill_pressed = RGB(30, 27, 36);
		constexpr COLORREF color_button_border = RGB(150, 126, 84);
		constexpr COLORREF color_button_text_disabled = RGB(110, 105, 96);
		constexpr COLORREF color_progress_track = RGB(28, 26, 34);
		constexpr COLORREF color_progress_fill = RGB(202, 162, 64);
		constexpr COLORREF color_edit_bg = RGB(28, 26, 34);
	}

	// Widgets a mouse press can land on (for pressed-state tracking)
	enum class widget_id
	{
		none,
		primary,
		browse,
		mode,
		resolution
	};

	struct launcher_gui::impl
	{
		HWND hwnd = nullptr;
		HWND edit = nullptr;
		HFONT font = nullptr;
		HFONT font_bold = nullptr;
		HFONT font_small = nullptr;
		HBRUSH edit_brush = nullptr;

		std::deque<ui_event> events;

		std::string status;
		std::string version_line;
		std::string primary_label;
		std::string mode_label;
		std::string resolution_label;

		float progress = 0.0f;
		bool progress_visible = false;
		bool primary_enabled = false;
		bool primary_visible = false;
		bool install_row_visible = false;
		bool options_row_visible = false;

		widget_id pressed = widget_id::none;
		bool suppress_edit_events = false;

		void push(ui_event e) { events.push_back(e); }
		void repaint() const { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }

		widget_id hit_test(int x, int y) const
		{
			if (primary_visible && primary_enabled && layout::primary_button.contains(x, y))
				return widget_id::primary;
			if (install_row_visible && layout::browse_button.contains(x, y))
				return widget_id::browse;
			if (options_row_visible && layout::mode_selector.contains(x, y))
				return widget_id::mode;
			if (options_row_visible && layout::resolution_selector.contains(x, y))
				return widget_id::resolution;
			return widget_id::none;
		}

		void draw_button(HDC dc, const ui_rect& r, const std::string& label,
			bool enabled, bool is_pressed, HFONT label_font) const
		{
			HBRUSH fill = CreateSolidBrush(is_pressed ? color_button_fill_pressed : color_button_fill);
			RECT rc{r.x, r.y, r.x + r.w, r.y + r.h};
			FillRect(dc, &rc, fill);
			DeleteObject(fill);

			HPEN pen = CreatePen(PS_SOLID, layout::ui_scale, color_button_border);
			HGDIOBJ old_pen = SelectObject(dc, pen);
			HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
			Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
			SelectObject(dc, old_brush);
			SelectObject(dc, old_pen);
			DeleteObject(pen);

			SetTextColor(dc, enabled ? color_strip_text : color_button_text_disabled);
			HGDIOBJ old_font = SelectObject(dc, label_font);
			DrawTextA(dc, label.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			SelectObject(dc, old_font);
		}

		void paint(HDC dc) const
		{
			// Artwork backdrop
			const unsigned char* pixels = art_pixel_data();
			if (pixels)
			{
				BITMAPINFO bmi{};
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = art_width;
				bmi.bmiHeader.biHeight = -art_height; // top-down
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiHeader.biCompression = BI_RGB;
				StretchDIBits(dc, 0, 0, art_width, art_height, 0, 0, art_width, art_height,
					pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
			}
			else
			{
				RECT full{0, 0, layout::window_width, layout::window_height};
				HBRUSH bg = CreateSolidBrush(RGB(24, 22, 30));
				FillRect(dc, &full, bg);
				DeleteObject(bg);
			}

			SetBkMode(dc, TRANSPARENT);

			// Status line
			SetTextColor(dc, color_strip_text);
			HGDIOBJ old_font = SelectObject(dc, font);
			RECT rc{layout::status_line.x, layout::status_line.y,
				layout::status_line.x + layout::status_line.w,
				layout::status_line.y + layout::status_line.h};
			DrawTextA(dc, status.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

			// Version line
			SetTextColor(dc, color_dim_text);
			SelectObject(dc, font_small);
			rc = {layout::version_line.x, layout::version_line.y,
				layout::version_line.x + layout::version_line.w,
				layout::version_line.y + layout::version_line.h};
			DrawTextA(dc, version_line.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			SelectObject(dc, old_font);

			// Progress bar
			if (progress_visible)
			{
				const auto& p = layout::progress_bar;
				RECT track{p.x, p.y, p.x + p.w, p.y + p.h};
				HBRUSH track_brush = CreateSolidBrush(color_progress_track);
				FillRect(dc, &track, track_brush);
				DeleteObject(track_brush);

				int fill_w = static_cast<int>(p.w * (progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress));
				if (fill_w > 0)
				{
					RECT fill{p.x, p.y, p.x + fill_w, p.y + p.h};
					HBRUSH fill_brush = CreateSolidBrush(color_progress_fill);
					FillRect(dc, &fill, fill_brush);
					DeleteObject(fill_brush);
				}

				HPEN pen = CreatePen(PS_SOLID, layout::ui_scale, color_button_border);
				HGDIOBJ old_pen = SelectObject(dc, pen);
				HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
				Rectangle(dc, track.left, track.top, track.right, track.bottom);
				SelectObject(dc, old_brush);
				SelectObject(dc, old_pen);
				DeleteObject(pen);
			}

			// Buttons and selectors
			if (install_row_visible)
				draw_button(dc, layout::browse_button, "Browse...", true, pressed == widget_id::browse, font);
			if (options_row_visible)
			{
				draw_button(dc, layout::mode_selector, mode_label, true, pressed == widget_id::mode, font);
				draw_button(dc, layout::resolution_selector, resolution_label, true, pressed == widget_id::resolution, font);
			}
			if (primary_visible)
				draw_button(dc, layout::primary_button, primary_label, primary_enabled, pressed == widget_id::primary, font_bold);
		}

		static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
		{
			auto* self = reinterpret_cast<impl*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

			switch (msg)
			{
			case WM_NCCREATE:
			{
				auto* create = reinterpret_cast<CREATESTRUCTA*>(lparam);
				SetWindowLongPtrA(hwnd, GWLP_USERDATA,
					reinterpret_cast<LONG_PTR>(create->lpCreateParams));
				return DefWindowProcA(hwnd, msg, wparam, lparam);
			}

			case WM_PAINT:
			{
				if (!self)
					break;
				PAINTSTRUCT ps;
				HDC dc = BeginPaint(hwnd, &ps);

				// Double buffer to avoid flicker
				HDC mem = CreateCompatibleDC(dc);
				HBITMAP bmp = CreateCompatibleBitmap(dc, layout::window_width, layout::window_height);
				HGDIOBJ old_bmp = SelectObject(mem, bmp);
				self->paint(mem);
				BitBlt(dc, 0, 0, layout::window_width, layout::window_height, mem, 0, 0, SRCCOPY);
				SelectObject(mem, old_bmp);
				DeleteObject(bmp);
				DeleteDC(mem);

				EndPaint(hwnd, &ps);
				return 0;
			}

			case WM_LBUTTONDOWN:
				if (self)
				{
					self->pressed = self->hit_test(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
					if (self->pressed != widget_id::none)
					{
						SetCapture(hwnd);
						self->repaint();
					}
				}
				return 0;

			case WM_LBUTTONUP:
				if (self && self->pressed != widget_id::none)
				{
					ReleaseCapture();
					widget_id released_over = self->hit_test(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
					if (released_over == self->pressed)
					{
						switch (self->pressed)
						{
						case widget_id::primary:    self->push(ui_event::primary_clicked); break;
						case widget_id::browse:     self->push(ui_event::browse_clicked); break;
						case widget_id::mode:       self->push(ui_event::mode_cycled); break;
						case widget_id::resolution: self->push(ui_event::resolution_cycled); break;
						case widget_id::none:       break;
						}
					}
					self->pressed = widget_id::none;
					self->repaint();
				}
				return 0;

			case WM_COMMAND:
				if (self && LOWORD(wparam) == edit_control_id && HIWORD(wparam) == EN_CHANGE
					&& !self->suppress_edit_events)
				{
					self->push(ui_event::path_changed);
				}
				return 0;

			case WM_CTLCOLOREDIT:
				if (self)
				{
					HDC edit_dc = reinterpret_cast<HDC>(wparam);
					SetTextColor(edit_dc, color_strip_text);
					SetBkColor(edit_dc, color_edit_bg);
					return reinterpret_cast<LRESULT>(self->edit_brush);
				}
				break;

			case WM_ERASEBKGND:
				return 1; // fully painted in WM_PAINT

			case WM_CLOSE:
				if (self)
					self->push(ui_event::closed);
				return 0; // app decides when to destroy
			}

			return DefWindowProcA(hwnd, msg, wparam, lparam);
		}
	};

	launcher_gui::launcher_gui() : m_impl(std::make_unique<impl>()) {}

	launcher_gui::~launcher_gui()
	{
		destroy();
	}

	bool launcher_gui::create()
	{
		HINSTANCE inst = GetModuleHandleA(nullptr);

		WNDCLASSA wc{};
		wc.lpfnWndProc = impl::wnd_proc;
		wc.hInstance = inst;
		wc.hCursor = LoadCursorA(nullptr, reinterpret_cast<LPCSTR>(IDC_ARROW));
		wc.lpszClassName = window_class_name;
		RegisterClassA(&wc);

		DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		RECT rc{0, 0, layout::window_width, layout::window_height};
		AdjustWindowRect(&rc, style, FALSE);
		int w = rc.right - rc.left;
		int h = rc.bottom - rc.top;
		int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
		int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

		m_impl->hwnd = CreateWindowExA(0, window_class_name, layout::window_title,
			style, x, y, w, h, nullptr, nullptr, inst, m_impl.get());
		if (!m_impl->hwnd)
			return false;

		m_impl->font = CreateFontA(-layout::font_normal, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
		m_impl->font_bold = CreateFontA(-layout::font_bold, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
		m_impl->font_small = CreateFontA(-layout::font_small, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
		m_impl->edit_brush = CreateSolidBrush(color_edit_bg);

		const auto& pf = layout::path_field;
		m_impl->edit = CreateWindowExA(0, "EDIT", "",
			WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
			pf.x, pf.y, pf.w, pf.h,
			m_impl->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(edit_control_id)),
			inst, nullptr);
		SendMessageA(m_impl->edit, WM_SETFONT, reinterpret_cast<WPARAM>(m_impl->font), TRUE);

		ShowWindow(m_impl->hwnd, SW_SHOW);
		UpdateWindow(m_impl->hwnd);
		return true;
	}

	void launcher_gui::destroy()
	{
		if (!m_impl)
			return;
		if (m_impl->hwnd)
		{
			DestroyWindow(m_impl->hwnd);
			m_impl->hwnd = nullptr;
			m_impl->edit = nullptr;
		}
		if (m_impl->font) { DeleteObject(m_impl->font); m_impl->font = nullptr; }
		if (m_impl->font_bold) { DeleteObject(m_impl->font_bold); m_impl->font_bold = nullptr; }
		if (m_impl->font_small) { DeleteObject(m_impl->font_small); m_impl->font_small = nullptr; }
		if (m_impl->edit_brush) { DeleteObject(m_impl->edit_brush); m_impl->edit_brush = nullptr; }
	}

	void launcher_gui::pump_messages()
	{
		MSG msg;
		while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
	}

	ui_event launcher_gui::poll_event()
	{
		if (m_impl->events.empty())
			return ui_event::none;
		ui_event e = m_impl->events.front();
		m_impl->events.pop_front();
		return e;
	}

	void launcher_gui::set_status(const std::string& text)
	{
		m_impl->status = text;
		m_impl->repaint();
	}

	void launcher_gui::set_version_line(const std::string& text)
	{
		m_impl->version_line = text;
		m_impl->repaint();
	}

	void launcher_gui::set_progress(float fraction)
	{
		m_impl->progress = fraction;
		m_impl->repaint();
	}

	void launcher_gui::show_progress(bool visible)
	{
		m_impl->progress_visible = visible;
		m_impl->repaint();
	}

	void launcher_gui::set_primary(const std::string& label, bool enabled, bool visible)
	{
		m_impl->primary_label = label;
		m_impl->primary_enabled = enabled;
		m_impl->primary_visible = visible;
		m_impl->repaint();
	}

	void launcher_gui::show_install_row(bool visible)
	{
		m_impl->install_row_visible = visible;
		if (m_impl->edit)
			ShowWindow(m_impl->edit, visible ? SW_SHOW : SW_HIDE);
		m_impl->repaint();
	}

	void launcher_gui::show_options_row(bool visible)
	{
		m_impl->options_row_visible = visible;
		m_impl->repaint();
	}

	void launcher_gui::set_mode_label(const std::string& text)
	{
		m_impl->mode_label = text;
		m_impl->repaint();
	}

	void launcher_gui::set_resolution_label(const std::string& text)
	{
		m_impl->resolution_label = text;
		m_impl->repaint();
	}

	std::string launcher_gui::path_text() const
	{
		if (!m_impl->edit)
			return {};
		char buf[1024] = {};
		GetWindowTextA(m_impl->edit, buf, sizeof(buf));
		return std::string(buf);
	}

	void launcher_gui::set_path_text(const std::string& text)
	{
		if (!m_impl->edit)
			return;
		// Programmatic updates must not echo back as user edits
		m_impl->suppress_edit_events = true;
		SetWindowTextA(m_impl->edit, text.c_str());
		m_impl->suppress_edit_events = false;
	}

	// Native folder picker (IFileDialog in folder mode). Returns empty on cancel.
	std::string browse_for_folder(launcher_gui& gui)
	{
		(void)gui;
		std::string result;

		HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		bool uninit = SUCCEEDED(hr);

		IFileDialog* dialog = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&dialog))))
		{
			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

			if (SUCCEEDED(dialog->Show(nullptr)))
			{
				IShellItem* item = nullptr;
				if (SUCCEEDED(dialog->GetResult(&item)))
				{
					PWSTR wide_path = nullptr;
					if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path)))
					{
						char buf[1024] = {};
						WideCharToMultiByte(CP_ACP, 0, wide_path, -1, buf, sizeof(buf), nullptr, nullptr);
						result = buf;
						CoTaskMemFree(wide_path);
					}
					item->Release();
				}
			}
			dialog->Release();
		}

		if (uninit)
			CoUninitialize();
		return result;
	}
}

#endif // _WIN32
