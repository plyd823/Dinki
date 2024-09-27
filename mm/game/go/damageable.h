#pragma once

#include "physgo.h"
#include "mm/game/events.h"

class CDamageable : public CPhysicsGameObject, public NEvent::CEventHandler
{
public:

    virtual ~CDamageable() = 0;
    virtual float InflictDamage() = 0;
    virtual float CalculateDamage() = 0;
    virtual bool IsTargetable() = 0;
    virtual void OnDestroyed(CHashString) = 0;
    virtual void SendOnDestroyEvent(CHashString) = 0;

    void SetHealth(float fHealth);
    void SetInvulnerable(bool invulnerable) { m_Invulnerable = invulnerable; }

    bool m_Destroyed;
    bool m_Invulnerable;
    char unk234[214];
};