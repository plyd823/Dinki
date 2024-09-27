#pragma once

#include "mm/mm.h"

class SResourceCache;

class SObjectIDData
{
public:
    uint32_t m_Hash;
    uint32_t m_UserData;
};

class SObjectID : public SObjectIDData
{
public:

};

class __declspec(align(8)) SGameObjectSetPropertiesContext
{
public:
    SResourceCache* m_ResourceCache;
    unsigned int m_LocationHash;
    const char* m_LocationName;
    bool m_CheckNoCreateOnLoadProperty;
};

class SGameObjectInitContext
{
public:
    const float m_Dt;
    const float m_DtIgnorePause;
    const float m_RealDt;
    SResourceCache* m_ResourceCache;
    void* m_ProjectContext;
};

class CGameObject : public CRtti
{
public:
    void* m_Tags;                                           //008-016
    int         m_TagCount;                                 //016-020
    SObjectID   m_ObjectID;                                 //020-028
    CHashString m_AliasID;                                  //028-032
    CHashString m_NameHash;                                 //032-036
    char pad1[108];                                         //036-144
    mm::std::vector<boost::shared_ptr<CGameObject>> m_Children; //144-176
    boost::weak_ptr<CGameObject>                m_Parent;   //176-192
    boost::weak_ptr<CGameObject>                m_WeakThis; //192-208
public:
    //virtual ~CGameObject() = 0;
    virtual CRttiTypeId* GetTypeId() = 0;
    virtual bool IsType(int32_t*) = 0;
    virtual void Init(void*) = 0;
    virtual void PostInit() = 0;
    virtual int  GetRequiredUpdates() = 0;
    virtual int  GetParallelizedUpdates() = 0;
    virtual void SetProperties(void*, void*) = 0;
    virtual void ExtendedSetProperties(void*, void*, void*) = 0;
    virtual void UpdateProperty() = 0; //zalupa
    virtual void ReadProperty() = 0; //zalupa
    virtual void UpdatePreSim(void*) = 0;
    virtual void UpdatePostSim(void*) = 0;
    virtual void UpdateRender(void*) = 0;
    virtual int  GetUpdateOrder() = 0;
    virtual bool NeedsUpdate() = 0;
    virtual bool StayInUpdate() = 0;
    virtual bool StayInUpdatePreSim() = 0;
    virtual bool StayInUpdatePostSim() = 0;
    virtual bool StayInUpdateRender() = 0;
    virtual void InitTransform(CMatrix const&) = 0;
    virtual void SetTransform(CMatrix* world_CMatrix) = 0;
    virtual bool GetTransform(CMatrix* t1) = 0;

    uint32_t GetNameHash();
    SObjectID GetObjectID();

    void InitHierarchy(void*);
    void PostInitHierarchy();

    //void InitTransformRecursivly(const CMatrix4f& mat) { HookMgr::Call<void>(0x1401E2860, this, mat); }

    //static int32_t* GetClassIdByName(const mm::std::string& name);
};

MMASSERT(CGameObject, 0xD0);