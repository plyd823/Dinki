#pragma once

#include "mm/hookmgr.h"

#define HASHSTR(str) ([]() { static auto _hash = CHashString(str); return _hash; }())
#define HASHSTR_INT(str) ([]() { static auto _hash = CHashString(str); return _hash; }()).m_Hash

inline SMETHOD(0x140F332C0, 0x142FBD230, uint32_t, HashString(const char* str), str)

class CHashString
{
public:
	CHashString() { m_Hash = 0; }
	CHashString(const char* str) { 
		if (str && *str)
			m_Hash = HashString(str);
		else
			m_Hash = 0;
	}
	CHashString(std::string const& str) {
		if (!str.empty())
			m_Hash = HashString(str.c_str());
		else
			m_Hash = 0;
	}
	CHashString(int hash) {
		if (hash != 0xDEADBEEF)
			m_Hash = hash;
		else
			m_Hash = 0;
	}
	CHashString(uint32_t hash) { 
		if (hash != 0xDEADBEEF)
			m_Hash = hash;
		else
			m_Hash = 0;
	}
	CHashString(CHashString const& hashS) { m_Hash = hashS.m_Hash; }
	CHashString(CHashString && hashS) noexcept { m_Hash = hashS.m_Hash; }


	uint32_t m_Hash;

	CHashString& operator=(CHashString&& rhs) noexcept {
		if (this != &rhs) {
			m_Hash = rhs.m_Hash;
			rhs.m_Hash = 0;
		}
		return *this;
	}

	CHashString& operator=(const CHashString& rhs) noexcept {
		if (this != &rhs) {
			m_Hash = rhs.m_Hash;
		}
		return *this;
	}

	CHashString& operator=(const char* str) noexcept {
		if (str && *str)
			m_Hash = HashString(str);
		else
			m_Hash = 0;
		return *this;
	}


	bool operator!=(const CHashString& rhs) const { return m_Hash != rhs.m_Hash; }
	bool operator!=(uint32_t rhs) const { return m_Hash != rhs; }

	bool operator==(const CHashString& rhs) const { return m_Hash == rhs.m_Hash; }
	bool operator==(uint32_t rhs) const { return m_Hash == rhs; }

	operator uint32_t() const { return m_Hash; }
};