#pragma once

#define MMASSERT(item, size) static_assert(sizeof(item) == size, #item)

//#ifndef _DEBUG
//#define _NEED_UNDEF_DEBUG
////#define _DEBUG
//
////#pragma pack(push, 8)
//
//#endif

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>


#include <iostream>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <array>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <fstream>

#include "hookmgr.h"

namespace mm {
	namespace std {

		template<typename T1, typename T2>
		class pair {
		public:
			T1 first;
			T2 second;
		};

		template<typename T>
		class __declspec(align(8)) vector {
		public:
			T* _Myfirst;
			T* _MyLast;
			T* _MyEnd;

			T*& begin() {
				return _Myfirst;
			}

			T*& end() {
				return _MyLast;
			}

			signed __int64 size() {
				return _MyLast - _Myfirst;
			}

			bool empty() {
				return _Myfirst == _MyLast;
			}

			T& operator[](size_t _Pos) {
				return _Myfirst[_Pos];
			}

		private:
			char pad[8];
		};

		template<typename T>
		class list {
		public:
			void* _MyHead;
			size_t _MySize;
		private:
			char pad[8];
		};

		template<typename Key, typename Value>
		class map {
		private:
			char pad[8];
		public:
			void* _MyHead;
			size_t _MySize;
		private:
			char pad1[8];
		};

		class wstring {
		public:
			union {
				wchar_t _Buf[8]{ 0 };
				wchar_t* _Ptr;
				char _Alias[8];
			};

			size_t _Mysize = 0;
			size_t _Myres = 7;

			/*wstring(::std::wstring const& normalstr) {
				operator=(normalstr.c_str());
			}*/

			const wchar_t* c_str() {
				if (_Myres < 8)
					return _Buf;

				return _Ptr;
			}

			/*wstring& operator=(const wchar_t* ptr) {
				return HookMgr::Call<wstring&>(0x140619D60, this, ptr);
			}*/

			::std::wstring normal() {
				return c_str();
			}

		private:
			char gap20[8];
		};

		class string {
		public:
			union {
				char _Buf[16]{ 0 };
				char* _Ptr;
				char _Alias[16];
			};

			size_t _Mysize = 0;
			size_t _Myres = 15;

			string(::std::string const& normalstr) {
				operator=(normalstr.c_str());
			}

			string(const char* normalstr) {
				operator=(normalstr);
			}

			const char* c_str() {
				if (_Myres < 16)
					return _Buf;

				return _Ptr;
			}

			/*string& operator=(const char* ptr) {
				return HookMgr::Call<string&>(0x140061950, this, ptr);
			}*/

			::std::string normal() {
				return c_str();
			}

		private:
			char gap20[8];
		};
	}
}

//#ifdef _NEED_UNDEF_DEBUG
//#undef _NEED_UNDEF_DEBUG
////#undef _DEBUG
////#pragma pack(pop)
//#endif // !_DEBUG




