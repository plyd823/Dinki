#pragma once

#include "mm/mm.h"
#include "mm/game/go/go.h"


class CAsyncLoadedResource;
class SResourceCache;

enum ESpawnPriority : int
{
	E_SPAWN_PRIORITY_LOW = -2,
	E_SPAWN_PRIORITY_NORMAL = -1,
	E_SPAWN_PRIORITY_MEDIUM = 9,
	E_SPAWN_PRIORITY_HIGH = 1,
	E_SPAWN_PRIORITY_CRITICAL = 2,
	E_SPAWN_PRIORITY_COUNT = 5,
};

class CSpawnedEntity
{
public:
	char unk[32];
	CHashString		m_EntityId;
	char unk1[28];

	//mm::std::vector<boost::shared_ptr<CGameObject>> m_SpawnedObjects;
	//mm::std::vector<boost::shared_ptr<CGameObject>>& GetGameObjects() { return m_SpawnedObjects; }
};

class SSpawnInstanceInfo
{
public:

	CHashString m_SpawnId;		//00-04
	CHashString m_SpawnPatchId;	//04-08
	float m_DespawnDistanceSq;	//08-12
	uint8_t unk12[4];
	char unkevents[24];			//16-40
	mm::std::vector<uint64_t> m_nObjects;
	CHashString m_LocationHash;	//72-76
	char m_SpawnPriority;		//76-77
	unsigned __int8 m_Flags[3];
	float m_VehicleCheckRadius;
	unsigned __int8 m_TypesToSpawn[2];
	unsigned __int8 m_TypesToIgnore[2];
	unsigned __int8 m_ResourceSetIds[20];
	mm::std::string m_SpawnEntityFile;

	CMETHOD(0x140548C60, 0x142171820, void, SetEntityFile(const char* filename), filename)

	static SSpawnInstanceInfo* New() {
		SSpawnInstanceInfo* ptr = (SSpawnInstanceInfo*)(operator new(sizeof(SSpawnInstanceInfo)));
		HookMgr::Call<void>(ADDRESS(0x1402E19D0, 0x141EB2410), ptr); //ctor
		return ptr;
	}

	static void Delete(void* ptr) {
		HookMgr::Call<void>(ADDRESS(0x1402E2320, 0x141EB2E10), ptr); //dtor
		operator delete(ptr);
	}

private:

};
static_assert(sizeof(SSpawnInstanceInfo) == 0x98, "f");


class CSpawnQueueItem
{
public:
	void* vtbl;
	CMatrix				m_WorldTransform;
	CMatrix				m_ReferenceTransform;
	SSpawnInstanceInfo	m_Info;
	CSpawnedEntity* m_EntityInfo;
	class CResourceLoader* m_ResourceLoader;
	SResourceCache* m_Cache;
	char m_Iter[0x10];
	unsigned int m_SpawnIdCount;
	unsigned int m_SpawnIdCountCheck;
	float m_NotifyTimer;
	unsigned __int8 m_SpawnState;
	unsigned __int8 m_ErrorCode;
	bool m_ForceAbovePhysics;
	bool m_HasReferenceTransform;
	bool m_ResourcesLoaded;
};
static_assert(sizeof(CSpawnQueueItem) == 0x160, "");

class CSpawnedEntityHandler
{
public:

	char unk[80];
	mm::std::vector<CSpawnedEntity*>	m_SpawnedEntities;
	mm::std::vector<CSpawnQueueItem*>	m_SpawnQueue;
	mm::std::vector<CSpawnQueueItem*>	m_DeleteQueue;
	mm::std::vector<CHashString>		m_SpawnedLastFrame;
	mm::std::vector<CHashString>		m_DespawnedLastFrame;
	mm::std::vector<CHashString>		m_FailedLastFrame;

	SGameObjectSetPropertiesContext m_SetPropertyContext;
	SGameObjectInitContext m_GameObjectInitContext;
	CVector3f m_PlayerPos;
	TArray<class CSpawnBudgetGroup*> m_SpawnBudgetGroups;
	bool m_SpawnInfo;
	bool m_SpawnEntityInfo;
	bool m_SpawnGameObjectInfo;
	bool m_ShowCharacterInfo;
	bool m_ShowVehicleInfo;
	bool m_ShowCharacterBeam;
	bool m_ShowVehicleBeam;
	bool m_ShowLoadedSets;
	bool m_UseSpawnLimits;
	bool m_DoReset;
	bool m_SoftReset;
	bool m_Enable;
	bool m_InQueryState;
	bool m_IsSpawning;
	bool m_BlockEELoading;
	bool m_CreateTimeIsUp;

	CAsyncLoadedResource* m_SpawnResourcesHandle;
	CAsyncLoadedResource* m_ResourceSetsHandle;
	CAsyncLoadedResource* m_SpawnDebugHandle;

	CSpawnedEntity* GetSpawnedEntity(CHashString& hash)
	{
		for (auto entity : m_SpawnedEntities)
		{
			if (entity->m_EntityId == hash)
			{
				return entity;
			}
		}

		/*for (auto Item : m_SpawnQueue)
		{
			if (Item->m_Info.m_SpawnId == hash)
			{
				return Item->m_EntityInfo;
			}
		}*/

		return 0x0;
	}

	CSpawnQueueItem* GetSpawnQueue(CHashString& hash)
	{
		for (auto Item : m_SpawnQueue)
		{
			if (Item->m_Info.m_SpawnId == hash)
			{
				return Item;
			}
		}

		return 0x0;
	}

	void Spawn(const SSpawnInstanceInfo* info, const CMatrix& mat) { HookMgr::Call<void>(0x140562C10, this, info, &mat); }

	static CSpawnedEntityHandler* Instance() { return *(CSpawnedEntityHandler**)0x14171B678; }
};
//static_assert(sizeof(CSpawnedEntityHandler) == 0x1A8, "");

inline SMETHOD(0x1405099D0, 0x14212B750, CHashString&, SpawnSystemGetNewSpawnId(CHashString const& result), &result)
inline SMETHOD(0x140530400, 0x142157080, void, SpawnSystemDespawn(CHashString const& id), &id)
inline SMETHOD(0x140562EB0, 0x14218D5B0, size_t, SpawnSystemSpawn(SSpawnInstanceInfo* info, CMatrix* mat), info, mat)

extern SVARDEF(CSpawnedEntityHandler*, s_SpawnSystemContext);