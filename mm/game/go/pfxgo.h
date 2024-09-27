#pragma once

#include "mm/mm.h"
#include "go.h"

class IPfxInstance;
class IPfxGameObject
{
public:

    const struct __declspec(align(8)) SHitReactInfo
    {
        CVector3f m_Normal;
        CVector3f m_Position;
        float m_Impact;
        float m_IncomingImpact;
        IPfxGameObject* CollidableGameObjectA;
        IPfxGameObject* CollidableGameObjectB;
        const void* m_RigidBodyA;
        const void* m_RigidBodyB;
        CVector3f m_VelocityA;
        CVector3f m_VelocityB;
        bool m_RagdollImpact;
    };


    // invalid args, just for padding
    virtual ~IPfxGameObject() = 0;
    virtual IPfxInstance* GetIPfxInstance() = 0;
    virtual void DoReactToHit() = 0;
    virtual void OnBreak() = 0;
    virtual void NotifyOutOfBroadphase() = 0;
    virtual void OnOutOfBroadphase() = 0;
};