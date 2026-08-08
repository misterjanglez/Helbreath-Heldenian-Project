#pragma once

#include "PacketCommon.h"
#include "PacketHeaders.h"
#include "NetMessages.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>
#include <cstring>

namespace hb {
namespace net {
	template <typename T>
	inline bool IsPacketType()
	{
		return std::is_trivially_copyable<T>::value && std::is_standard_layout<T>::value;
	}

	template <typename T>
	inline const T* PacketCast(const void* data, std::size_t size)
	{
		if (!IsPacketType<T>() || data == nullptr || size < sizeof(T)) return nullptr;
		return reinterpret_cast<const T*>(data);
	}

	// --- Fixed-array wire strings ---------------------------------------
	// Every string on the wire is a fixed char array, null-padded, that a
	// hostile peer controls end to end. These three are the only sanctioned
	// ways across that boundary — per-file re-spellings of them are exactly
	// the copies that fork and leak unpadded stack bytes.

	// Write-side: zero-fill then copy, truncating, always terminated.
	template <std::size_t N>
	inline void fill_wire_name(char (&dst)[N], const char* src)
	{
		std::memset(dst, 0, N);
		if (src != nullptr)
		{
			std::snprintf(dst, N, "%s", src);
		}
	}

	// Read-side into a same-sized local: termination guaranteed even when
	// the sender filled every byte.
	template <std::size_t N>
	inline void terminate_wire_string(char (&dst)[N], const char (&src)[N])
	{
		std::memcpy(dst, src, N);
		dst[N - 1] = '\0';
	}

	// Read-side into a std::string, bounded by the array.
	template <std::size_t N>
	inline std::string wire_string(const char (&src)[N])
	{
		return std::string(src, strnlen(src, N));
	}

	// A zeroed notify packet with its header stamped — the Notify-channel
	// builders all start here.
	template <typename PacketT>
	inline PacketT make_notify(uint16_t msg_type)
	{
		PacketT pkt{};
		pkt.header.msg_id = hb::shared::net::MsgId::Notify;
		pkt.header.msg_type = msg_type;
		return pkt;
	}

	template <typename T>
	inline T* PacketCast(void* data, std::size_t size)
	{
		if (!IsPacketType<T>() || data == nullptr || size < sizeof(T)) return nullptr;
		return reinterpret_cast<T*>(data);
	}

	class PacketWriter
	{
	public:
		void Reset()
		{
			m_buffer.clear();
		}

		void Reserve(std::size_t size)
		{
			m_buffer.reserve(size);
		}

		template <typename T>
		T* Append()
		{
			static_assert(std::is_trivially_copyable<T>::value && std::is_standard_layout<T>::value,
				"PacketWriter only supports packet types.");
			const auto offset = m_buffer.size();
			m_buffer.resize(offset + sizeof(T));
			std::memset(m_buffer.data() + offset, 0, sizeof(T));
			return reinterpret_cast<T*>(m_buffer.data() + offset);
		}

		void AppendBytes(const void* data, std::size_t size)
		{
			if (size == 0) return;
			const auto offset = m_buffer.size();
			m_buffer.resize(offset + size);
			if (data) {
				std::memcpy(m_buffer.data() + offset, data, size);
			} else {
				std::memset(m_buffer.data() + offset, 0, size);
			}
		}

		const char* Data() const
		{
			return reinterpret_cast<const char*>(m_buffer.data());
		}

		char* Data()
		{
			return reinterpret_cast<char*>(m_buffer.data());
		}

		std::size_t size() const
		{
			return m_buffer.size();
		}

	private:
		std::vector<std::uint8_t> m_buffer;
	};
}
}
