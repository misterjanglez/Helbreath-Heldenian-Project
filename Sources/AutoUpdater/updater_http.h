#pragma once
#include <memory>
#include <string>

namespace hb::updater
{
	// HTTP GET a text resource. Returns true on success (HTTP 200).
	// response_body receives the full body on success.
	bool http_get_text(const char* host, int port, const char* path, std::string& response_body);

	// Reusable HTTP connection (keep-alive across requests). One instance per
	// thread — instances are not thread-safe, but independent instances may be
	// used concurrently.
	class http_client
	{
	public:
		http_client(const char* host, int port);
		~http_client();

		// HTTP GET a binary file and write it to disk. Creates parent
		// directories as needed. Retries transient failures internally
		// (reconnecting as needed). Returns true on success.
		bool download_file(const char* url_path, const std::string& local_path);

	private:
		struct impl;
		std::unique_ptr<impl> m_impl;
	};
}
