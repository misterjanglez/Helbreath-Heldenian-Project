#pragma once

// Cross-platform crash reporting and error monitoring (Sentry Native SDK).
// Targets that link the SDK define HB_SENTRY; without it every function is a
// no-op stub so builds never depend on the SDK being present.
//
// DSN, database path, environment, and crashpad handler location are
// implementation details owned by error_monitor.cpp. The DSN can be
// overridden (or set empty to disable) via the SENTRY_DSN environment
// variable.
namespace hb::shared::error_monitor
{
	class session;

	// Starts crash/error reporting as "<app_name>@<app_version>". Call first
	// thing in main() and keep the returned session alive until exit.
	[[nodiscard]] session start(const char* app_name, const char* app_version);

	// RAII handle for the monitoring session; the destructor flushes pending
	// events and stops the SDK.
	class session
	{
	public:
		session() noexcept = default;
		session(const session&) = delete;
		session& operator=(const session&) = delete;
		session(session&& other) noexcept;
		session& operator=(session&& other) noexcept;
		~session();

		bool active() const noexcept { return m_active; }

	private:
		friend session start(const char* app_name, const char* app_version);
		explicit session(bool active) noexcept : m_active(active) {}

		bool m_active = false;
	};

	// Manual capture for non-fatal problems worth a Sentry event.
	void capture_message(const char* message);
	void capture_error(const char* message);

	// Breadcrumbs enrich the next crash/event with recent context.
	void add_breadcrumb(const char* category, const char* message);
}
