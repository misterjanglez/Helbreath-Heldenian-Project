#include "updater_http.h"
#include "UpdaterConstants.h"
#include "updater_platform.h"

#include "vendor/httplib.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace hb::updater
{
	namespace
	{
		void configure_timeouts(httplib::Client& client)
		{
			client.set_connection_timeout(connect_timeout);
			client.set_read_timeout(read_timeout);
		}
	}

	// One-shot by design: issued once before the GUI exists, with its own
	// user-facing retry loop at the call site — keep-alive/auto-retry would
	// buy nothing here (unlike the 600+ file download path below).
	bool http_get_text(const char* host, int port, const char* path, std::string& response_body)
	{
		httplib::Client client(host, port);
		configure_timeouts(client);

		auto result = client.Get(path);
		if (!result || result->status != 200)
			return false;

		response_body = std::move(result->body);
		return true;
	}

	struct http_client::impl
	{
		httplib::Client client;

		impl(const char* host, int port)
			: client(host, port)
		{
			configure_timeouts(client);
			client.set_keep_alive(true);
		}
	};

	http_client::http_client(const char* host, int port)
		: m_impl(std::make_unique<impl>(host, port))
	{
	}

	http_client::~http_client() = default;

	bool http_client::download_file(const char* url_path, const std::string& local_path)
	{
		// Ensure parent directory exists
		auto parent = std::filesystem::path(local_path).parent_path();
		if (!parent.empty())
			std::filesystem::create_directories(parent);

		// Transient failures (dropped connection, brief server hiccup) are
		// expected across a 600+ file install — retry with backoff before
		// reporting failure to the caller. httplib re-establishes the
		// connection on the next Get() if the previous one died.
		for (int attempt = 1; attempt <= max_download_attempts; ++attempt)
		{
			if (attempt > 1)
				std::this_thread::sleep_for(std::chrono::seconds(attempt - 1));

			std::ofstream ofs(local_path, std::ios::binary | std::ios::trunc);
			if (!ofs.is_open())
				return false; // local disk problem — retrying won't help

			// Stream chunks straight to disk — keeps memory flat regardless
			// of file size (large .pak files would otherwise be buffered
			// whole per worker).
			auto result = m_impl->client.Get(url_path,
				[&ofs](const char* data, size_t len)
				{
					ofs.write(data, static_cast<std::streamsize>(len));
					return ofs.good();
				});

			if (result && result->status == 200 && ofs.good())
				return true;
		}

		return false;
	}
}
