#pragma once

#include "pfxgo.h"

struct SCollisionInfo;

class CDamageMsg {
public:
    char pad[0xC0];  // 0008 - 00C8

    virtual ~CDamageMsg() = 0;
};

class IPhysicsGameObjectListenable
{
public:
    char vtable[8];
};

class CPhysicsGameObject : public IPfxGameObject, public CGameObject
{
public:
    /*char pad2[0x60]; // 00D0 - 0150
    float dragCoefficient;
    float mass;
    float linearDamping;
    float angularDamping;
    float gravityFactor;
    char pad3[0xC]; // 00D0 - 0150*/

public:

    struct SHitByFireData
    {
        float m_CoveredVolume;
        CVector3f m_ImpactPosition;
        CVector3f m_ImpactDirection;
        CVector3f m_FirePosition;
        CVector3f m_FireAt;
        float m_LifeTime;
        float m_SourceHeat;
        float m_DeltaTime;
        int m_Faction;
    };


    virtual ~CPhysicsGameObject() = 0;

    virtual bool GetPhysicsAABB(CAABox*) = 0;
    virtual void SetKeyframedMotionState(bool) = 0;
    virtual void SetNextKeyframeTransform(float, const CMatrix4f*, bool, float) = 0;
    virtual bool HitByBullet() = 0;
    virtual void HitByExplosion() = 0;
    virtual void HitByFire(CPhysicsGameObject::SHitByFireData*) = 0;
    virtual void CollideWithWater(const CVector3f*, const float, const CVector3f*, float) = 0;
    virtual void OnCollision() = 0;
    virtual void OnCollisionSliding() = 0;
    virtual void OnCollisionRolling() = 0;
    virtual void OnCollisionGeneric() = 0;
    virtual bool IsDamageable() = 0;
    virtual void SetMaterialOverride() = 0;
    virtual void ClearMaterialOverride() = 0;
    virtual void SetWaterLevel(float) = 0;
    virtual CVector3f*  GetPointClosestTo(CVector3f* result, const CVector3f*, int) = 0;
    virtual void InitBuoyancy() = 0;
}; 