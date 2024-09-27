#pragma once


#include "damageable.h"

class __declspec(align(16)) CVehicle : public CDamageable {
public:



	virtual void StopPartAnimation(unsigned int) = 0;
	virtual void SetWheelSkew(int, float) = 0;
	virtual void UpdatePfxPreSim() = 0;
	virtual void UpdatePfxPostSimStep() = 0;
	virtual void Update() = 0;
	virtual void Clear() = 0;
	virtual void SetVisibilityModelsAndEffects(bool) = 0;
	virtual void OnEntering(CCharacter*, bool, bool) = 0;
	virtual void OnEnter(CCharacter*, bool, bool) = 0;
	virtual void OnExit(CCharacter*, bool, bool) = 0;
	virtual void SetVelocity(const CVector3f&) = 0;
	virtual void SetTargetHeight(float) = 0;
	virtual void SetLOD(bool, unsigned __int8, unsigned __int8) = 0;
	virtual int GetNumberOfWheelsOnGround() = 0;
	virtual float GetTrust() = 0;
	virtual float GetTargetHeight() = 0;
	virtual void GetAABB(CAABox*) = 0;
	virtual void GetLocalAABB(CAABox*) = 0;
	virtual void AttachObject(boost::shared_ptr<CGameObject>*, unsigned int) = 0;
	virtual void FadeIn(float) = 0;
	virtual char GetSpawnTypeId() = 0;
	virtual char GetSpawnVariationIndex() = 0;
	virtual char GetSpawnTeam() = 0;
	virtual void KillPhysics() = 0;
	virtual float CalculateLod(float, const CVector3f*) = 0;
	virtual void OnEmerge() = 0;
	virtual void OnSubmerge() = 0;
	virtual void SetDirtyWheels() = 0;
	virtual void UpdateAiEntityInstance() = 0;
	virtual void GetAngularVelocity(CVector3f*) = 0;
	virtual void GetVelocity(CVector3f*) = 0;
	virtual const CVector3f*  GetVelocityLastFrame() = 0;
	virtual void GetAcceleration(CVector3f*) = 0;
	virtual const CHashString*  GetState() = 0;
};