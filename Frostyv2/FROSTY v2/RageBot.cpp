#include "RageBot.h"
#include "Render.h"
#include "Autowall.h"
#include <iostream>
#include "MathFunctions.h"
#include "SDK.h"
#include "EnginePrediction.h"
#include "LagComp.h"


using namespace std;

#define TICK_INTERVAL			(g_Globals->interval_per_tick)
#define TICKS_TO_TIME( t )		( g_Globals->interval_per_tick *( t ) )
#define TIME_TO_TICKS( dt )		( (int)( 0.5f + (float)(dt) / TICK_INTERVAL ) )
ragebot::ragebot()
{
	IsLocked = false;
	TargetID = -1;
	pTarget = nullptr;
}

void ragebot::OnCreateMove(CInput::CUserCmd *pCmd, bool& bSendPacket)
{
	bool storedHitscan = g_Options.Ragebot.Hitscan;

	if (!g_Options.Ragebot.MainSwitch)
		return;

	C_BaseEntity *pLocal = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());
	if (pLocal && pLocal->IsAlive())
	{
		if (g_Options.Ragebot.BAIMkey && G::PressedKeys[g_Options.Ragebot.BAIMkey] && g_Options.Ragebot.Hitscan != 4)
		{
			g_Options.Ragebot.Hitscan = 4;
		}
		else if (g_Options.Ragebot.BAIMkey && !G::PressedKeys[g_Options.Ragebot.BAIMkey] && g_Options.Ragebot.Hitscan != 3)
		{
			g_Options.Ragebot.Hitscan = storedHitscan;
		}

		CBaseCombatWeapon* weapon = (CBaseCombatWeapon*)g_EntityList->GetClientEntityFromHandle(pLocal->GetActiveWeaponHandle());
		if (weapon && weapon->m_AttributeManager()->m_Item()->GetItemDefinitionIndex() == 64)
		{
			if (!CanAttack() && weapon->ammo() > 0)
			{
				pCmd->buttons |= IN_ATTACK;
			}
		}


		if (g_Options.Ragebot.Enabled)
			DoAimbot(pCmd, bSendPacket);

		if (g_Options.Ragebot.AntiRecoil)
			DoNoRecoil(pCmd);

		if (g_Options.Ragebot.EnabledAntiAim)
			DoAntiAim(pCmd, bSendPacket);

		if (g_Options.Ragebot.FakeLag)
			FakeLag(pCmd);

	}
}
bool ragebot::hit_chance(C_BaseEntity* local, CInput::CUserCmd* cmd, CBaseCombatWeapon* weapon, C_BaseEntity* target)
{
	Vector forward, right, up;

	C_BaseEntity *pLocal = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());

	CBaseCombatWeapon* pWeapon = (CBaseCombatWeapon*)g_EntityList->GetClientEntityFromHandle(pLocal->GetActiveWeaponHandle());

	constexpr auto max_traces = 256;

	AngleVectors(cmd->viewangles, &forward, &right, &up);

	int total_hits = 0;

	int needed_hits;

	if (MiscFunctions::IsSniper(pWeapon))
	{
		needed_hits = static_cast<int>(max_traces * (g_Options.Ragebot.HitchanceSniper / 100.f));
	}
	else if (MiscFunctions::IsPistol(pWeapon))
	{
		needed_hits = static_cast<int>(max_traces * (g_Options.Ragebot.HitchancePistol / 100.f));
	}
	else if (MiscFunctions::IsHeavy(pWeapon))
	{
		needed_hits = static_cast<int>(max_traces * (g_Options.Ragebot.HitchanceHeavy / 100.f));
	}
	else if (MiscFunctions::IsSmg(pWeapon))
	{
		needed_hits = static_cast<int>(max_traces * (g_Options.Ragebot.HitchanceSmgs / 100.f));
	}
	else if (MiscFunctions::IsRifle(pWeapon))
	{
		needed_hits = static_cast<int>(max_traces * (g_Options.Ragebot.HitchanceRifle / 100.f));
	}
	else if (MiscFunctions::IsRevolver(pWeapon))
	{
		needed_hits = static_cast<int>(max_traces * (g_Options.Ragebot.HitchanceRevolver / 100.f));
	}

	weapon->UpdateAccuracyPenalty(weapon);

	auto eyes = local->GetEyePosition();
	auto flRange = weapon->GetCSWpnData()->m_fRange;

	for (int i = 0; i < max_traces; i++) {
		RandomSeed(i + 1);

		float fRand1 = RandomFloat(0.f, 1.f);
		float fRandPi1 = RandomFloat(0.f, XM_2PI);
		float fRand2 = RandomFloat(0.f, 1.f);
		float fRandPi2 = RandomFloat(0.f, XM_2PI);

		float fRandInaccuracy = fRand1 * weapon->GetInaccuracy();
		float fRandSpread = fRand2 * weapon->GetSpread();

		float fSpreadX = cos(fRandPi1) * fRandInaccuracy + cos(fRandPi2) * fRandSpread;
		float fSpreadY = sin(fRandPi1) * fRandInaccuracy + sin(fRandPi2) * fRandSpread;

		auto viewSpreadForward = (forward + fSpreadX * right + fSpreadY * up).Normalized();

		Vector viewAnglesSpread;
		VectorAngles(viewSpreadForward, viewAnglesSpread);
		MiscFunctions::NormaliseViewAngle(viewAnglesSpread);

		Vector viewForward;
		AngleVectors(viewAnglesSpread, &viewForward);
		viewForward.NormalizeInPlace();

		viewForward = eyes + (viewForward * flRange);

		trace_t tr;
		Ray_t ray;
		ray.Init(eyes, viewForward);

		g_EngineTrace->ClipRayToEntity(ray, MASK_SHOT | CONTENTS_GRATE, target, &tr);


		if (tr.m_pEnt == target)
			total_hits++;

		if (total_hits >= needed_hits)
			return true;

		if ((max_traces - i + total_hits) < needed_hits)
			return false;
	}

	return false;
}
template<class T, class U>
T clamp(T in, U low, U high)
{
	if (in <= low)
		return low;

	if (in >= high)
		return high;

	return in;

}

void ragebot::DoAimbot(CInput::CUserCmd *pCmd, bool& bSendPacket)
{

	C_BaseEntity* pLocal = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());
	bool FindNewTarget = true;
	//IsLocked = false;

	// Don't aimbot with the knife..
	CBaseCombatWeapon* pWeapon = (CBaseCombatWeapon*)g_EntityList->GetClientEntityFromHandle(pLocal->GetActiveWeaponHandle());

	if (pWeapon != nullptr)
	{

		if (pWeapon->ammo() == 0 || MiscFunctions::IsKnife(pWeapon) || MiscFunctions::IsGrenade(pWeapon))
		{
			//TargetID = 0;
			//pTarget = nullptr;
			//HitBox = -1;
			return;
		}
	}
	else
		return;

	// Make sure we have a good target
	if (IsLocked && TargetID >= 0 && HitBox >= 0)
	{
		pTarget = g_EntityList->GetClientEntity(TargetID);
		if (pTarget  && TargetMeetsRequirements(pTarget))
		{
			HitBox = HitScan(pTarget);
			if (HitBox >= 0)
			{
				Vector ViewOffset = pLocal->GetOrigin() + pLocal->GetViewOffset();
				Vector View; g_Engine->GetViewAngles(View);
				float FoV = FovToPlayer(ViewOffset, View, pTarget, HitBox);
				if (FoV < g_Options.Ragebot.FOV)
					FindNewTarget = false;
			}
		}
	}



	// Find a new target, apparently we need to
	if (FindNewTarget)
	{
		Globals::Shots = 0;
		TargetID = 0;
		pTarget = nullptr;
		HitBox = -1;


		TargetID = GetTargetCrosshair();


		// Memesj
		if (TargetID >= 0)
		{
			pTarget = g_EntityList->GetClientEntity(TargetID);
		}
	}

	if (TargetID >= 0 && pTarget)
	{
		HitBox = HitScan(pTarget);

		// Key
		if (g_Options.Ragebot.KeyPress)
		{
			if (g_Options.Ragebot.KeyPress > 0 && !G::PressedKeys[g_Options.Ragebot.KeyPress])
			{
				TargetID = -1;
				pTarget = nullptr;
				HitBox = -1;
				return;
			}
		}


		Globals::AimPoint = GetHitboxPosition(pTarget, HitBox);

		if (AimAtPoint(pLocal, Globals::AimPoint, pCmd))
		{
			if (g_Options.Ragebot.AutoFire && CanAttack() && MiscFunctions::IsSniper(pWeapon) && g_Options.Ragebot.AutoScope)
			{
				if (pLocal->IsScoped()) if (!g_Options.Ragebot.Hitchance || hit_chance(pLocal, pCmd, pWeapon, pTarget)) pCmd->buttons |= IN_ATTACK;
				if (!pLocal->IsScoped()) pCmd->buttons |= IN_ATTACK2;
			}
			if (g_Options.Ragebot.AutoFire && CanAttack() && !(MiscFunctions::IsSniper(pWeapon)))
			{
				if (!g_Options.Ragebot.Hitchance || hit_chance(pLocal, pCmd, pWeapon, pTarget)) pCmd->buttons |= IN_ATTACK;
			}
			if (g_Options.Ragebot.AutoFire && CanAttack() && (MiscFunctions::IsSniper(pWeapon)) && !g_Options.Ragebot.AutoScope)
			{
				if (!g_Options.Ragebot.Hitchance || hit_chance(pLocal, pCmd, pWeapon, pTarget)) if (pLocal->IsScoped()) pCmd->buttons |= IN_ATTACK;
			}

			//if (CanAttack() && pCmd->buttons & IN_ATTACK)
			//	Globals::Shots += 1;
		}

		//Calculate shot fired
		if (pWeapon) {
			if (!(pCmd->buttons & IN_ATTACK) && pWeapon->GetNextPrimaryAttack() <= (pLocal->GetTickBase() * g_Globals->interval_per_tick)) {
				Globals::Shots = 0;
				Globals::missedshots = 0;
			}
			else {
				Globals::Shots += pLocal->m_iShotsFired();
			}
		}




		if (g_Options.Ragebot.AutoStop)
		{
			pCmd->forwardmove = 0.f;
			pCmd->sidemove = 0.f;
		}



		if (g_Options.Ragebot.AutoCrouch)
		{
			pCmd->buttons |= IN_DUCK;
		}

	}

	// Auto Pistol
	static bool WasFiring = false;
	if (pWeapon != nullptr)
	{
		if (MiscFunctions::IsPistol(pWeapon) && g_Options.Ragebot.AutoPistol && pWeapon->m_AttributeManager()->m_Item()->GetItemDefinitionIndex() != 64)
		{
			if (pCmd->buttons & IN_ATTACK && !MiscFunctions::IsKnife(pWeapon) && !MiscFunctions::IsGrenade(pWeapon))
			{
				if (WasFiring)
				{
					pCmd->buttons &= ~IN_ATTACK;
				}
			}

			WasFiring = pCmd->buttons & IN_ATTACK ? true : false;
		}
	}


}



bool ragebot::TargetMeetsRequirements(C_BaseEntity* pEntity)
{
	auto local = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());
	// Is a valid player
	if (pEntity && pEntity->IsDormant() == false && pEntity->IsAlive() && pEntity->GetIndex() != local->GetIndex())
	{
		// Entity Type checks
		ClientClass *pClientClass = pEntity->GetClientClass();
		player_info_t pinfo;
		if (pClientClass->m_ClassID == (int)ClassID::CCSPlayer && g_Engine->GetPlayerInfo(pEntity->GetIndex(), &pinfo))
		{
			// Team Check
			if (pEntity->GetTeamNum() != local->GetTeamNum() || g_Options.Ragebot.FriendlyFire)
			{
				// Spawn Check
				if (!pEntity->HasGunGameImmunity())
				{
					return true;
				}
			}
		}
	}

	// They must have failed a requirement
	return false;
}




float ragebot::FovToPlayer(Vector ViewOffSet, Vector View, C_BaseEntity* pEntity, int aHitBox)
{
	// Anything past 180 degrees is just going to wrap around
	CONST FLOAT MaxDegrees = 180.0f;

	// Get local angles
	Vector Angles = View;

	// Get local view / eye position
	Vector Origin = ViewOffSet;

	// Create and intiialize vectors for calculations below
	Vector Delta(0, 0, 0);
	//Vector Origin(0, 0, 0);
	Vector Forward(0, 0, 0);

	// Convert angles to normalized directional forward vector
	AngleVectors(Angles, &Forward);
	Vector AimPos = GetHitboxPosition(pEntity, aHitBox); //pvs fix disabled
														 // Get delta vector between our local eye position and passed vector
	VectorSubtract(AimPos, Origin, Delta);
	//Delta = AimPos - Origin;

	// Normalize our delta vector
	Normalize(Delta, Delta);

	// Get dot product between delta position and directional forward vectors
	FLOAT DotProduct = Forward.Dot(Delta);

	// Time to calculate the field of view
	return (acos(DotProduct) * (MaxDegrees / PI));
}

int ragebot::GetTargetCrosshair()
{
	// Target selection
	int target = -1;
	float minFoV = g_Options.Ragebot.FOV;

	C_BaseEntity* pLocal = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());
	Vector ViewOffset = pLocal->GetOrigin() + pLocal->GetViewOffset();
	Vector View; g_Engine->GetViewAngles(View);

	for (int i = 0; i < g_EntityList->GetHighestEntityIndex(); i++)
	{
		C_BaseEntity *pEntity = g_EntityList->GetClientEntity(i);

		if (TargetMeetsRequirements(pEntity))
		{
			int NewHitBox = HitScan(pEntity);
			if (NewHitBox >= 0)
			{
				float fov = FovToPlayer(ViewOffset, View, pEntity, 0);
				if (fov < minFoV)
				{
					minFoV = fov;
					target = i;
				}
			}
		}
	}

	return target;
}

int ragebot::HitScan(C_BaseEntity* pEntity)
{
	vector<int> HitBoxesToScan{ Head , Neck, Chest, Stomach };

	C_BaseEntity *pLocal = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());

	CBaseCombatWeapon* pWeapon = (CBaseCombatWeapon*)g_EntityList->GetClientEntityFromHandle(pLocal->GetActiveWeaponHandle());

	int HitScanMode = g_Options.Ragebot.Hitscan;

	if (!g_Options.Ragebot.Hitscan)
	{
		switch (g_Options.Ragebot.Hitbox)
		{
		case 0:
			HitBoxesToScan.push_back(Head);
			break;
		case 1:
			HitBoxesToScan.push_back(Neck);
			break;
		case 2:
			HitBoxesToScan.push_back(Pelvis);
			break;
		}
	}
	else
	{
		switch (HitScanMode)
		{
		case 1:
			// low
			HitBoxesToScan.push_back(Head);
			HitBoxesToScan.push_back(Neck);
			HitBoxesToScan.push_back(UpperChest);
			HitBoxesToScan.push_back(Chest);
			HitBoxesToScan.push_back(Stomach);
			HitBoxesToScan.push_back(Pelvis);
			break;
		case 2:
			// medium
			HitBoxesToScan.push_back(Head);
			HitBoxesToScan.push_back(Neck);
			HitBoxesToScan.push_back(UpperChest);
			HitBoxesToScan.push_back(Chest);
			HitBoxesToScan.push_back(Stomach);
			HitBoxesToScan.push_back(Pelvis);
			HitBoxesToScan.push_back(LeftUpperArm);
			HitBoxesToScan.push_back(RightUpperArm);
			HitBoxesToScan.push_back(LeftThigh);
			HitBoxesToScan.push_back(RightThigh);
			break;
		case 3:
			// high
			HitBoxesToScan.push_back(Head);
			HitBoxesToScan.push_back(Neck);
			HitBoxesToScan.push_back(UpperChest);
			HitBoxesToScan.push_back(Chest);
			HitBoxesToScan.push_back(Stomach);
			HitBoxesToScan.push_back(Pelvis);
			HitBoxesToScan.push_back(LeftThigh);
			HitBoxesToScan.push_back(RightThigh);
			HitBoxesToScan.push_back(LeftFoot);
			HitBoxesToScan.push_back(RightFoot);
			HitBoxesToScan.push_back(LeftShin);
			HitBoxesToScan.push_back(RightShin);
			HitBoxesToScan.push_back(LeftLowerArm);
			HitBoxesToScan.push_back(RightLowerArm);
			HitBoxesToScan.push_back(LeftUpperArm);
			HitBoxesToScan.push_back(RightUpperArm);
			HitBoxesToScan.push_back(LeftHand);
			HitBoxesToScan.push_back(RightHand);
			break;
		case 4:
			// baim
			HitBoxesToScan.push_back(UpperChest);
			HitBoxesToScan.push_back(Chest);
			HitBoxesToScan.push_back(Stomach);
			HitBoxesToScan.push_back(Pelvis);
			HitBoxesToScan.push_back(LeftUpperArm);
			HitBoxesToScan.push_back(RightUpperArm);
			HitBoxesToScan.push_back(LeftThigh);
			HitBoxesToScan.push_back(RightThigh);
			HitBoxesToScan.push_back(LeftHand);
			HitBoxesToScan.push_back(RightHand);
			HitBoxesToScan.push_back(LeftFoot);
			HitBoxesToScan.push_back(RightFoot);
			HitBoxesToScan.push_back(LeftShin);
			HitBoxesToScan.push_back(RightShin);
			HitBoxesToScan.push_back(LeftLowerArm);
			HitBoxesToScan.push_back(RightLowerArm);
			break;
		}
	}
	static vector<int> baim{ UpperChest ,Chest ,Stomach ,Pelvis ,LeftUpperArm ,RightUpperArm ,LeftThigh,RightThigh ,LeftHand ,RightHand, LeftFoot, RightFoot, LeftShin, RightShin,LeftLowerArm,RightLowerArm };

	int bestHitbox = -1;
	float highestDamage;

	if (MiscFunctions::IsSniper(pWeapon))
	{
		highestDamage = g_Options.Ragebot.MinimumDamageSniper;
	}
	else if (MiscFunctions::IsPistol(pWeapon))
	{
		highestDamage = g_Options.Ragebot.MinimumDamagePistol;
	}
	else if (MiscFunctions::IsHeavy(pWeapon))
	{
		highestDamage = g_Options.Ragebot.MinimumDamageHeavy;
	}
	else if (MiscFunctions::IsSmg(pWeapon))
	{
		highestDamage = g_Options.Ragebot.MinimumDamageSmg;
	}
	else if (MiscFunctions::IsRifle(pWeapon))
	{
		highestDamage = g_Options.Ragebot.MinimumDamageRifle;
	}
	else if (MiscFunctions::IsRevolver(pWeapon))
	{
		highestDamage = g_Options.Ragebot.MinimumDamageRevolver;
	}

	for (auto HitBoxID : HitBoxesToScan)
	{

		Vector Point = GetHitboxPosition(pEntity, HitBoxID); //pvs fix disabled

		float damage = 0.0f;
		if (CanHit(pEntity, Point, &damage))
		{
			if (damage > highestDamage || damage > pEntity->GetHealth())
			{
				bestHitbox = HitBoxID;
				highestDamage = damage;
			}
		}
	}
	/*for (auto HitBoxID : baim)
	{

	Vector Point = GetHitboxPosition(pEntity, HitBoxID); //pvs fix disabled

	float damage = 0.0f;
	if (CanHit(pEntity, Point, &damage))
	{
	if (damage > highestDamage && damage > pEntity->GetHealth())
	{
	bestHitbox = HitBoxID;
	highestDamage = damage;
	}
	}
	}*/
	return bestHitbox;

}



void ragebot::DoNoRecoil(CInput::CUserCmd *pCmd)
{
	// Ghetto rcs shit, implement properly later
	C_BaseEntity* pLocal = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());
	if (pLocal != nullptr)
	{
		Vector AimPunch = pLocal->localPlayerExclusive()->GetAimPunchAngle();
		if (AimPunch.Length2D() > 0 && AimPunch.Length2D() < 150)
		{
			pCmd->viewangles -= AimPunch * 2;
			MiscFunctions::NormaliseViewAngle(pCmd->viewangles);
		}
	}
}

float FovToPoint(Vector ViewOffSet, Vector View, Vector Point)
{
	// Get local view / eye position
	Vector Origin = ViewOffSet;

	// Create and intiialize vectors for calculations below
	Vector Delta(0, 0, 0);
	Vector Forward(0, 0, 0);

	// Convert angles to normalized directional forward vector
	AngleVectors(View, &Forward);
	Vector AimPos = Point;

	// Get delta vector between our local eye position and passed vector
	Delta = AimPos - Origin;
	//Delta = AimPos - Origin;

	// Normalize our delta vector
	Normalize(Delta, Delta);

	// Get dot product between delta position and directional forward vectors
	FLOAT DotProduct = Forward.Dot(Delta);

	// Time to calculate the field of view
	return (acos(DotProduct) * (180.f / PI));
}
bool me123 = false;
bool ragebot::AimAtPoint(C_BaseEntity* pLocal, Vector point, CInput::CUserCmd *pCmd)
{
	bool ReturnValue = false;

	if (point.Length() == 0) return ReturnValue;

	Vector angles;

	Vector src = pLocal->GetOrigin() + pLocal->GetViewOffset();

	VectorAngles(point - src, angles);





	IsLocked = true;
	ReturnValue = true;



	if (g_Options.Ragebot.Silent)
	{
		if (CanAttack()) {
			pCmd->viewangles = angles;
		}
	}

	if (!g_Options.Ragebot.Silent)
	{
		pCmd->viewangles = angles;
		g_Engine->SetViewAngles(pCmd->viewangles);
	}
	return ReturnValue;
}




void NormalizeVector(Vector& vec) {
	for (int i = 0; i < 3; ++i) {
		while (vec[i] > 180.f)
			vec[i] -= 360.f;

		while (vec[i] < -180.f)
			vec[i] += 360.f;
	}
	vec[2] = 0.f;
}


void VectorAngles2(const Vector &vecForward, Vector &vecAngles)
{
	Vector vecView;
	if (vecForward[1] == 0.f && vecForward[0] == 0.f)
	{
		vecView[0] = 0.f;
		vecView[1] = 0.f;
	}
	else
	{
		vecView[1] = vec_t(atan2(vecForward[1], vecForward[0]) * 180.f / M_PI);

		if (vecView[1] < 0.f)
			vecView[1] += 360.f;

		vecView[2] = sqrt(vecForward[0] * vecForward[0] + vecForward[1] * vecForward[1]);

		vecView[0] = vec_t(atan2(vecForward[2], vecView[2]) * 180.f / M_PI);
	}

	vecAngles[0] = -vecView[0];
	vecAngles[1] = vecView[1];
	vecAngles[2] = 0.f;
}




bool EdgeAntiAim(C_BaseEntity* pLocalBaseEntity, CInput::CUserCmd* cmd, float flWall, float flCornor)
{
	Ray_t ray;
	trace_t tr;

	CTraceFilter traceFilter;
	traceFilter.pSkip = pLocalBaseEntity;

	auto bRetVal = false;
	auto vecCurPos = pLocalBaseEntity->GetEyePosition();

	for (float i = 0; i < 360; i++)
	{
		Vector vecDummy(10.f, cmd->viewangles.y, 0.f);
		vecDummy.y += i;

		NormalizeVector(vecDummy);

		Vector vecForward;
		AngleVectors2(vecDummy, vecForward);

		auto flLength = ((16.f + 3.f) + ((16.f + 3.f) * sin(DEG2RAD(10.f)))) + 7.f;
		vecForward *= flLength;

		ray.Init(vecCurPos, (vecCurPos + vecForward));
		g_EngineTrace->TraceRay(ray, MASK_SHOT, (CTraceFilter *)&traceFilter, &tr);

		if (tr.fraction != 1.0f)
		{
			Vector qAngles;
			auto vecNegate = tr.plane.normal;

			vecNegate *= -1.f;
			VectorAngles2(vecNegate, qAngles);

			vecDummy.y = qAngles.y;

			NormalizeVector(vecDummy);
			trace_t leftTrace, rightTrace;

			Vector vecLeft;
			AngleVectors2(vecDummy + Vector(0.f, 30.f, 0.f), vecLeft);

			Vector vecRight;
			AngleVectors2(vecDummy - Vector(0.f, 30.f, 0.f), vecRight);

			vecLeft *= (flLength + (flLength * sin(DEG2RAD(30.f))));
			vecRight *= (flLength + (flLength * sin(DEG2RAD(30.f))));

			ray.Init(vecCurPos, (vecCurPos + vecLeft));
			g_EngineTrace->TraceRay(ray, MASK_SHOT, (CTraceFilter*)&traceFilter, &leftTrace);

			ray.Init(vecCurPos, (vecCurPos + vecRight));
			g_EngineTrace->TraceRay(ray, MASK_SHOT, (CTraceFilter*)&traceFilter, &rightTrace);

			if ((leftTrace.fraction == 1.f) && (rightTrace.fraction != 1.f))
				vecDummy.y -= flCornor; // left
			else if ((leftTrace.fraction != 1.f) && (rightTrace.fraction == 1.f))
				vecDummy.y += flCornor; // right			

			cmd->viewangles.y = vecDummy.y;
			cmd->viewangles.y -= flWall;
			cmd->viewangles.x = 89.0f;
			bRetVal = true;
		}
	}
	return bRetVal;
}

// AntiAim
void ragebot::DoAntiAim(CInput::CUserCmd *pCmd, bool& bSendPacket)
{
	C_BaseEntity* pLocal = g_EntityList->GetClientEntity(g_Engine->GetLocalPlayer());
	CBaseCombatWeapon* pWeapon = (CBaseCombatWeapon*)g_EntityList->GetClientEntityFromHandle(pLocal->GetActiveWeaponHandle());




	// If the aimbot is doing something don't do anything
	if (pCmd->buttons & IN_ATTACK && CanAttack())
		return;
	if ((pCmd->buttons & IN_USE))
		return;
	if (pLocal->GetMoveType() == MOVETYPE_LADDER)
		return;
	// Weapon shit

	if (pWeapon)
	{
		CSWeaponInfo* pWeaponInfo = pWeapon->GetCSWpnData();
		CCSGrenade* csGrenade = (CCSGrenade*)pWeapon;


		if (MiscFunctions::IsKnife(pWeapon) && !g_Options.Ragebot.KnifeAA)
			return;

		if (csGrenade->GetThrowTime() > 0.f)
			return;
	}

	// Don't do antiaim
	// if (DoExit) return;

	if (g_Options.Ragebot.Edge) {
		auto bEdge = EdgeAntiAim(pLocal, pCmd, 360.f, 89.f);
		if (bEdge)
			return;
	}

	if (g_Options.Ragebot.YawFake != 0)
		Globals::ySwitch = !Globals::ySwitch;
	else
		Globals::ySwitch = true;

	bSendPacket = Globals::ySwitch;

	Vector SpinAngles = { 0,0,0 };
	Vector FakeAngles = { 0,0,0 };
	float server_time = pLocal->GetTickBase() * g_Globals->interval_per_tick;
	static int ticks;
	static bool flip;
	if (ticks < 15 + rand() % 20)
		ticks++;
	else
	{
		flip = !flip;
		ticks = 0;
	}
	Vector StartAngles;
	double rate = 360.0 / 1.618033988749895;
	double yaw = fmod(static_cast<double>(server_time)*rate, 360.0);
	double factor = 360.0 / M_PI;
	factor *= 25;
	switch (g_Options.Ragebot.YawTrue)
	{
	case 1: //sideways
	{
		g_Engine->GetViewAngles(StartAngles);
		SpinAngles.y = flip ? StartAngles.y - 90.f : StartAngles.y + 90.f;
	}
	break;
	case 2://slowspin
		SpinAngles.y += static_cast<float>(yaw);
		break;
	case 3://fastspin
	{
		SpinAngles.y = (float)(fmod(server_time / 0.05f * 360.0f, 360.0f));
	}
	break;
	case 4://backwards
	{
		g_Engine->GetViewAngles(StartAngles);
		StartAngles.y -= 180.f;
		SpinAngles = StartAngles;
	}
	break;
	case 5:
	{
		SpinAngles.y = pLocal->GetLowerBodyYaw();
	}
	break;
	case 6:
	{
		g_Engine->GetViewAngles(StartAngles);

		static bool dir = false;

		if (GetAsyncKeyState(VK_LEFT)) dir = false; else if (GetAsyncKeyState(VK_RIGHT)) dir = true;

		if (dir && pLocal->GetVelocity().Length2D() < 1)
		{
			if (Globals::shouldflip)
			{
				SpinAngles.y = StartAngles.y - 110;
			}
			else
			{
				SpinAngles.y = StartAngles.y + 135;
			}
		}
		else if (!dir && pLocal->GetVelocity().Length2D() < 1)
		{
			if (Globals::shouldflip)
			{
				SpinAngles.y = StartAngles.y + 110;
			}
			else
			{
				SpinAngles.y = StartAngles.y - 135;
			}
		}
		else if (pLocal->GetVelocity().Length2D() > 0)
		{
			SpinAngles.y = StartAngles.y + 180;
		}
	}
	break;
	case 7:
	{
		g_Engine->GetViewAngles(StartAngles);

		SpinAngles.y = flip ? StartAngles.y - 145.f : StartAngles.y + 145.f;
	}
	break;
	case 8:
	{
		g_Engine->GetViewAngles(StartAngles);

		static bool dir = false;
		static int jitterangle = 0;

		if (GetAsyncKeyState(VK_LEFT)) dir = false; else if (GetAsyncKeyState(VK_RIGHT)) dir = true;


		if (dir && pLocal->GetVelocity().Length2D() < 1)
		{
			if (Globals::shouldflip)
			{
				SpinAngles.y = StartAngles.y - 90;
			}
			else
			{
				SpinAngles.y = StartAngles.y + 125;

				if (jitterangle <= 1)
				{
					SpinAngles.y = StartAngles.y + 125;
					jitterangle += 1;
				}
				else if (jitterangle > 1 && jitterangle <= 3)
				{
					SpinAngles.y = StartAngles.y + 145;
					jitterangle += 1;
				}
				else
				{
					jitterangle = 0;
				}
			}
		}
		else if (!dir && pLocal->GetVelocity().Length2D() < 1)
		{
			if (Globals::shouldflip)
			{
				SpinAngles.y = StartAngles.y + 90;
			}
			else
			{
				SpinAngles.y = StartAngles.y - 125;

				if (jitterangle <= 1)
				{
					SpinAngles.y = StartAngles.y - 125;
					jitterangle += 1;
				}
				else if (jitterangle > 1 && jitterangle <= 3)
				{
					SpinAngles.y = StartAngles.y - 145;
					jitterangle += 1;
				}
				else
				{
					jitterangle = 0;
				}
			}
		}
		else if (pLocal->GetVelocity().Length2D() > 0)
		{
			SpinAngles.y = StartAngles.y + 155;

			if (jitterangle <= 1)
			{
				SpinAngles.y = StartAngles.y + 155;
				jitterangle += 1;
			}
			else if (jitterangle > 1 && jitterangle <= 3)
			{
				SpinAngles.y = StartAngles.y - 155;
				jitterangle += 1;
			}
			else
			{
				jitterangle = 0;
			}
		}
	}
	break;
	}



	switch (g_Options.Ragebot.YawFake)
	{
	case 1://sideways
	{
		g_Engine->GetViewAngles(StartAngles);
		FakeAngles.y = flip ? StartAngles.y + 90.f : StartAngles.y - 90.f;
	}
	break;
	case 2://slowspin
		FakeAngles.y += static_cast<float>(yaw);
		break;
	case 3://fastspin
		FakeAngles.y = (float)(fmod(server_time / 0.05f * 360.0f, 360.0f));
		break;
	case 4://backwards
	{
		g_Engine->GetViewAngles(StartAngles);

		StartAngles -= 180.f;
		FakeAngles = StartAngles;

	}
	break;
	case 5: //lby antiaim
	{
		g_Engine->GetViewAngles(StartAngles);
		static bool llamaflip;
		static float oldLBY = 0.0f;
		float LBY = pLocal->GetLowerBodyYaw();
		if (LBY != oldLBY) // did lowerbody update?
		{
			llamaflip = !llamaflip;
			oldLBY = LBY;
		}
		FakeAngles.y = llamaflip ? StartAngles.y - 90.f : StartAngles.y + 90.f;
	}
	break;
	case 6:
	{
		g_Engine->GetViewAngles(StartAngles);
		static bool dir = false;

		if (GetAsyncKeyState(VK_LEFT)) dir = false; else if (GetAsyncKeyState(VK_RIGHT)) dir = true;

		if (dir && pLocal->GetVelocity().Length2D() < 1)
		{
			FakeAngles.y -= StartAngles.y - 110;
		}
		else if (!dir && pLocal->GetVelocity().Length2D() < 1)
		{
			FakeAngles.y += StartAngles.y + 110;
		}
		else if (pLocal->GetVelocity().Length2D() > 0)
		{
			FakeAngles.y = (float)(fmod(server_time / 0.05f * 360.0f, 360.0f));
		}
	}
	break;
	case 7:
	{
		g_Engine->GetViewAngles(StartAngles);

		FakeAngles.y = flip ? StartAngles.y - 145.f : StartAngles.y + 145.f;
	}
	break;
	case 8:
	{
		g_Engine->GetViewAngles(StartAngles);
		static bool dir = false;
		static int jitterangle = 0;

		if (GetAsyncKeyState(VK_LEFT)) dir = false; else if (GetAsyncKeyState(VK_RIGHT)) dir = true;

		if (dir && pLocal->GetVelocity().Length2D() < 1)
		{
			FakeAngles.y = StartAngles.y - 75;
			if (jitterangle <= 1)
			{
				FakeAngles.y = StartAngles.y - 75;
				jitterangle += 1;
			}
			else if (jitterangle > 1 && jitterangle <= 3)
			{
				FakeAngles.y = StartAngles.y - 105;
				jitterangle += 1;
			}
			else
			{
				jitterangle = 0;
			}
		}
		else if (!dir && pLocal->GetVelocity().Length2D() < 1)
		{
			FakeAngles.y = StartAngles.y + 75;
			if (jitterangle <= 1)
			{
				FakeAngles.y = StartAngles.y + 75;
				jitterangle += 1;
			}
			else if (jitterangle > 1 && jitterangle <= 3)
			{
				FakeAngles.y = StartAngles.y + 105;
				jitterangle += 1;
			}
			else
			{
				jitterangle = 0;
			}
		}
		else if (pLocal->GetVelocity().Length2D() > 0)
		{
			FakeAngles.y = StartAngles.y + 45 + RandomFloat(90, -90);
			if (jitterangle <= 1)
			{
				FakeAngles.y += StartAngles.y + 45 + RandomFloat(90, -90);
				jitterangle += 1;
			}
			else if (jitterangle > 1 && jitterangle <= 3)
			{
				FakeAngles.y -= StartAngles.y - 45 - RandomFloat(90, -90);
				jitterangle += 1;
			}
			else
			{
				jitterangle = 0;
			}
		}
	}
	break;
	case 9:
	{
		g_Engine->GetViewAngles(StartAngles);

		static int jitterangle = 0;

		FakeAngles.y = StartAngles.y + 45 + RandomFloat(90, -90);
		if (jitterangle <= 1)
		{
			FakeAngles.y += StartAngles.y + 45 + RandomFloat(90, -90);
			jitterangle += 1;
		}
		else if (jitterangle > 1 && jitterangle <= 3)
		{
			FakeAngles.y -= StartAngles.y - 45 - RandomFloat(90, -90);
			jitterangle += 1;
		}
		else
		{
			jitterangle = 0;
		}
	}
	break;
	}

	if (g_Options.Ragebot.PreAAs && !g_Options.Ragebot.BuilderAAs)
	{
		if (Globals::ySwitch && g_Options.Ragebot.YawTrue != 0)
			pCmd->viewangles.y = FakeAngles.y + g_Options.Ragebot.YawFakeAdder;
		if (!Globals::ySwitch && g_Options.Ragebot.YawFake != 0)
			pCmd->viewangles.y = SpinAngles.y + g_Options.Ragebot.YawTrueAdder;
	}
	else if (!g_Options.Ragebot.PreAAs && g_Options.Ragebot.BuilderAAs)
	{
		g_Engine->GetViewAngles(StartAngles);
		static int jitterangle;

		pCmd->viewangles.x = g_Options.Ragebot.BuilderPitch;

		if (Globals::ySwitch) //Builder FakeAngle
		{
			if (g_Options.Ragebot.FJitter)
			{
				pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake;
				if (jitterangle <= 1)
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake + g_Options.Ragebot.FJitterRange;
					jitterangle += 1;
				}
				else if (jitterangle > 1 && jitterangle <= 3)
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake - g_Options.Ragebot.FJitterRange;
					jitterangle += 1;
				}
				else
				{
					jitterangle = 0;
				}
			}
			else if (g_Options.Ragebot.FJitter && g_Options.Ragebot.LBYBreaker)
			{
				if (Globals::shouldflip)
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake + 118;
				}
				else
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake;
					if (jitterangle <= 1)
					{
						pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake + g_Options.Ragebot.FJitterRange;
						jitterangle += 1;
					}
					else if (jitterangle > 1 && jitterangle <= 3)
					{
						pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake - g_Options.Ragebot.FJitterRange;
						jitterangle += 1;
					}
					else
					{
						jitterangle = 0;
					}
				}
			}
			else if (!g_Options.Ragebot.FJitter && g_Options.Ragebot.LBYBreaker)
			{
				if (Globals::shouldflip)
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake + 118;
				}
				else
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake;
				}
			}
			else
			{
				pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderFake;
			}
		}
		if (!Globals::ySwitch) //Builder RealAngle
		{
			if (g_Options.Ragebot.Jitter)
			{
				pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal;
				if (jitterangle <= 1)
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal + g_Options.Ragebot.JitterRange;
					jitterangle += 1;
				}
				else if (jitterangle > 1 && jitterangle <= 3)
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal - g_Options.Ragebot.JitterRange;
					jitterangle += 1;
				}
				else
				{
					jitterangle = 0;
				}
			}
			else if (g_Options.Ragebot.Jitter && g_Options.Ragebot.LBYBreaker)
			{
				if (Globals::shouldflip)
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal + 118;
				}
				else
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal;
					if (jitterangle <= 1)
					{
						pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal + g_Options.Ragebot.JitterRange;
						jitterangle += 1;
					}
					else if (jitterangle > 1 && jitterangle <= 3)
					{
						pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal - g_Options.Ragebot.JitterRange;
						jitterangle += 1;
					}
					else
					{
						jitterangle = 0;
					}
				}
			}
			else if (!g_Options.Ragebot.Jitter && g_Options.Ragebot.LBYBreaker)
			{
				if (Globals::shouldflip)
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal + 118;
				}
				else
				{
					pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal;
				}
			}
			else
			{
				pCmd->viewangles.y = StartAngles.y + g_Options.Ragebot.BuilderReal;
			}
		}

	}

	if (g_Options.Ragebot.PreAAs && g_Options.Ragebot.BuilderAAs)
	{
		Globals::error = true;
	}

	switch (g_Options.Ragebot.Pitch && g_Options.Ragebot.PreAAs && !g_Options.Ragebot.BuilderAAs)
	{
	case 0:
		// No Pitch AA
		break;
	case 1:
		// Down
		pCmd->viewangles.x = 89 + g_Options.Ragebot.PitchAdder;
		break;
	case 2:
		pCmd->viewangles.x = -89 + g_Options.Ragebot.PitchAdder;
		break;
	case 3:
		pCmd->viewangles.x = -180 + g_Options.Ragebot.PitchAdder;
		break;
	case 4:
		pCmd->viewangles.x = 180 + g_Options.Ragebot.PitchAdder;
		break;
	case 5:
		pCmd->viewangles.x = 1080 + g_Options.Ragebot.PitchAdder;
		break;
	}

}

void ragebot::FakeLag(CInput::CUserCmd* cmd)
{
	int iChoke = g_Options.Ragebot.FakeLagAmt;

	static int iFakeLag = -1;
	iFakeLag++;

	if (iFakeLag <= iChoke && iFakeLag > -1)
	{
		Globals::ySwitch = false;
	}
	else
	{
		Globals::ySwitch = true;
		iFakeLag = 1;
	}
}