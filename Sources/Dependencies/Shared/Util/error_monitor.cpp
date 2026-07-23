// std::getenv is fine here (read-once at startup); silence MSVC's C4996
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "error_monitor.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef HB_SENTRY
#include <sentry.h>
#endif

namespace hb::shared::error_monitor
{

namespace
{
	bool g_active = false;

	void stop()
	{
		if (!g_active)
			return;
		g_active = false;
#ifdef HB_SENTRY
		sentry_close();
#endif
	}
}

session::session(session&& other) noexcept
	: m_active(other.m_active)
{
	other.m_active = false;
}

session& session::operator=(session&& other) noexcept
{
	if (this != &other)
	{
		if (m_active)
			stop();
		m_active = other.m_active;
		other.m_active = false;
	}
	return *this;
}

session::~session()
{
	if (m_active)
		stop();
}

#ifdef HB_SENTRY

session start(const char* app_name, const char* app_version)
{
	// DSNs are endpoint identifiers, not secrets.
	constexpr const char* default_dsn =
		"https://3e96dc976fef7b981997d9088c498f1b@o4511779906781184.ingest.us.sentry.io/4511782824443904";

	std::string dsn = default_dsn;
	if (const char* env_dsn = std::getenv("SENTRY_DSN"))
		dsn = env_dsn;

	if (g_active || dsn.empty())
		return session(false);

	const std::string release = std::string(app_name) + "@" + app_version;
#ifdef NDEBUG
	const char* environment = "production";
#else
	const char* environment = "development";
#endif
	// SENTRY_ENVIRONMENT overrides the build-type default (e.g. "staging"
	// on the test box) so deployments self-describe
	if (const char* env_environment = std::getenv("SENTRY_ENVIRONMENT"))
		environment = env_environment;

	sentry_options_t* options = sentry_options_new();
	sentry_options_set_dsn(options, dsn.c_str());
	sentry_options_set_database_path(options, ".sentry-native");
	sentry_options_set_release(options, release.c_str());
	sentry_options_set_environment(options, environment);
#ifdef _WIN32
	sentry_options_set_handler_path(options, "crashpad_handler.exe");
#else
	sentry_options_set_handler_path(options, "crashpad_handler");
#endif
	sentry_options_set_enable_logs(options, 1);
	// SDK-internal diagnostics to stderr when SENTRY_DEBUG is set
	sentry_options_set_debug(options, std::getenv("SENTRY_DEBUG") != nullptr ? 1 : 0);

	if (sentry_init(options) != 0)
	{
		std::printf("[error_monitor] sentry_init failed - monitoring disabled\n");
		return session(false);
	}

	g_active = true;
	std::printf("[error_monitor] active (%s, %s)\n", release.c_str(), environment);
	return session(true);
}

void capture_message(const char* message)
{
	if (!g_active)
		return;
	sentry_capture_event(sentry_value_new_message_event(
		SENTRY_LEVEL_INFO, "app", message));
}

void capture_error(const char* message)
{
	if (!g_active)
		return;
	sentry_capture_event(sentry_value_new_message_event(
		SENTRY_LEVEL_ERROR, "app", message));
}

void add_breadcrumb(const char* category, const char* message)
{
	if (!g_active)
		return;
	sentry_value_t crumb = sentry_value_new_breadcrumb("default", message);
	sentry_value_set_by_key(crumb, "category", sentry_value_new_string(category));
	sentry_add_breadcrumb(crumb);
}

#else // !HB_SENTRY — no-op stubs

session start(const char*, const char*) { return session(); }
void capture_message(const char*) {}
void capture_error(const char*) {}
void add_breadcrumb(const char*, const char*) {}

#endif

}
