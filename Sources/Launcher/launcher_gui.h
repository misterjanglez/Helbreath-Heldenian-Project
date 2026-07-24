#pragma once
#include <memory>
#include <string>

// Backend-neutral launcher window: artwork backdrop + custom-drawn widgets
// (status, progress, primary CTA, cycle selectors, install path row).
// Implemented per platform in launcher_gui_win32.cpp / launcher_gui_x11.cpp.
// The app drives it via pump_messages() + poll_event() from its own loop.

namespace hb::launcher
{
	enum class ui_event
	{
		none,
		closed,             // user closed the window
		primary_clicked,    // the CTA button (Play / Update / Install / Retry)
		browse_clicked,     // folder-browse button (install row)
		mode_cycled,        // launch-mode selector clicked
		resolution_cycled,  // resolution selector clicked
		path_changed        // install-path text edited
	};

	class launcher_gui
	{
	public:
		launcher_gui();
		~launcher_gui();

		bool create();
		void destroy();

		void pump_messages();
		ui_event poll_event();  // drains one queued event, none when empty

		void set_status(const std::string& text);
		void set_version_line(const std::string& text);

		void set_progress(float fraction);  // 0.0 to 1.0
		void show_progress(bool visible);

		// Primary CTA; label decides meaning (Play Game / Update / Install / Retry)
		void set_primary(const std::string& label, bool enabled, bool visible);

		void show_install_row(bool visible);   // path field + browse button
		void show_options_row(bool visible);   // mode + resolution selectors
		void set_mode_label(const std::string& text);
		void set_resolution_label(const std::string& text);

		std::string path_text() const;
		void set_path_text(const std::string& text);

	private:
		struct impl;
		std::unique_ptr<impl> m_impl;
	};

	// Native folder picker where the platform has one (Win32: IFileDialog).
	// Returns the chosen directory, or empty on cancel. On platforms without
	// a native picker (X11) this always returns empty — the path text field
	// is the picker there, and the app hides the Browse button.
	std::string browse_for_folder(launcher_gui& gui);
}
