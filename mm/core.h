#pragma once

template<typename T>
class  TArray
{
	T* m_Begin;
	T* m_End;
	T* m_Last;
	unsigned __int16 m_UserAllocated : 1;

public:
	int64_t GetSize()
	{
		return m_End - m_Begin;
	}

	void PushBack(T* x)
	{
		*m_End++ = *x;
	}

	T* Begin() { return m_Begin; }
	T* End() { return m_End; }

	T* operator[](uint64_t index) { return &m_Begin[index]; }
};

template <typename T, typename T2, typename T3>
class THashTable //<CEffectSystem::SEffectResourceHandles, unsigned int, 1, unsigned short>
{
public:
	struct SChain
	{
		T2 m_Key;
		T3 m_Next;
		T m_Value;
	};
	unsigned __int16* m_HashTable;
	SChain* m_ChainPool;
	unsigned __int16 m_HashTableLength;
	unsigned __int16 m_FirstFreeChain;
	unsigned __int16 m_ChainPoolSize;
	unsigned __int16 m_ChainPoolFreeCount;
	unsigned __int16 m_Size;
	unsigned __int16 m_UserAllocated : 1;
	unsigned __int16 m_Dirty : 1;
	unsigned __int16 m_Reserved : 14;
};


class CRtti
{
public:
	virtual ~CRtti() = 0;
};

const struct CRttiTypeId
{
	unsigned int m_Hash;
};