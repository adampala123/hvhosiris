#include "AntiAim.h"
#include "../Interfaces.h"
#include "../SDK/Engine.h"
#include "../SDK/Entity.h"
#include "../SDK/EntityList.h"
#include "../SDK/NetworkChannel.h"
#include "../SDK/UserCmd.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/WeaponData.h"
#include "../SDK/WeaponId.h"
#include "../SDK/ConVar.h"
#include "../Memory.h"
#include "../SDK/Vector.h"

#include "../Interfaces.h"
#include "../SDK/Surface.h"

#include <stdlib.h>
#include <cmath> 

float AntiAim::DEG2RAD(float degree) {
    return (float)(degree * 22.0 / (180.0 * 7.0));
}

void AntiAim::CorrectMovement(float OldAngleY, UserCmd* pCmd, float fOldForward, float fOldSidemove)
{
    //side/forward move correction
    float deltaView = pCmd->viewangles.y - OldAngleY;
    float f1;
    float f2;

    if (OldAngleY < 0.f)
        f1 = 360.0f + OldAngleY;
    else
        f1 = OldAngleY;

    if (pCmd->viewangles.y < 0.0f)
        f2 = 360.0f + pCmd->viewangles.y;
    else
        f2 = pCmd->viewangles.y;

    if (f2 < f1)
        deltaView = abs(f2 - f1);
    else
        deltaView = 360.0f - abs(f1 - f2);
    deltaView = 360.0f - deltaView;

    pCmd->forwardmove = cos(DEG2RAD(deltaView)) * fOldForward + cos(DEG2RAD(deltaView + 90.f)) * fOldSidemove;
    pCmd->sidemove = sin(DEG2RAD(deltaView)) * fOldForward + sin(DEG2RAD(deltaView + 90.f)) * fOldSidemove;
}






bool AntiAim::LBY_UPDATE(){

	//float --- config->AntiAim.lastlby;
	//float --- config->AntiAim.lbyNextUpdate;
	//

	float servertime = memory->globalVars->serverTime();
	float Velocity = localPlayer->getVelocity().length2D();

	
	if (Velocity > 0.0f) { //LBY updates on any velocity
		config->antiAim.lbyNextUpdate = 0.22f + servertime;
		return false;
	}
	else if (config->antiAim.lbyNextUpdate <= servertime){ // LBY ipdates on no velocity, .22s after last velocity, 1.1s after previous no-velocity
		config->antiAim.lbyNextUpdate = 1.1f + config->antiAim.lbyNextUpdate;
		//config->AntiAim.lbyNextUpdate = 1.1f + servertime;
		return false;
	}

	if ((localPlayer->flags() & 1) && (config->antiAim.lbyNextUpdate <= servertime)){
			return true;
	}else{
		return false;
	}
	
}
/*
	float updateTime;
	float lastUpdate;
	float wasmoving;
	bool performBreak;

	void lbyBreaker(CUserCmd *pCmd, bool &bSendPacket) {
		IClientEntity* LocalPlayer = hackManager.pLocal();
		float flServerTime = (float)(LocalPlayer->GetTickBase()  * Interfaces::Globals->interval_per_tick);
		float Velocity = LocalPlayer->GetVelocity().Length2D();

		if (!performBreak) {
			if (Velocity > 1.f && (LocalPlayer->GetFlags() & FL_ONGROUND)) {
				lastUpdate = flServerTime;
				wasmoving = true;
			}
			else {
				if (wasmoving && flServerTime - lastUpdate > 0.22f && (LocalPlayer->GetFlags() & FL_ONGROUND)) {
					wasmoving = false;
					performBreak = true;
				}
				else if (flServerTime - lastUpdate > 1.1f && (LocalPlayer->GetFlags() & FL_ONGROUND)) {
					performBreak = true;
				}
				else {
				}
			}
		}
		else {
			bSendPacket = false;
			pCmd->viewangles.y += 105.f;
			lastUpdate = flServerTime;
			performBreak = false;
		}
	}


*/






void AntiAim::run(UserCmd* cmd, const Vector& previousViewAngles, const Vector& currentViewAngles, bool& sendPacket) noexcept
{
	const auto activeWeapon = localPlayer->getActiveWeapon();
	if (!activeWeapon || !activeWeapon->clip())
		return;

	if (activeWeapon->isGrenade()) {
		if (activeWeapon->isInThrow())
			return;
	}


    if (config->antiAim.enabled) {
        if (!localPlayer)
            return;
		 


        if (config->antiAim.pitch && cmd->viewangles.x == currentViewAngles.x)
            cmd->viewangles.x = config->antiAim.pitchAngle;



        if (config->antiAim.Spin == true) {
            if ((!(cmd->buttons & cmd->IN_ATTACK) || (cmd->buttons & cmd->IN_ATTACK && activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime()))
                && !(cmd->buttons & cmd->IN_USE))
            {

                
                cmd->viewangles.y = currentViewAngles.y + 45 * (config->antiAim.state % 8);
                //cmd->viewangles.y += 45 * (config->antiAim.state % 8);
                
                if (!(config->antiAim.bJitter)) {
                    CorrectMovement(currentViewAngles.y, cmd, cmd->forwardmove, cmd->sidemove);
                }

                config->antiAim.state++;

            }
        }






        if (config->antiAim.bJitter) {
            float jitterDeg = (float)(rand() % config->antiAim.JitterRange);

            if (140 > previousViewAngles.y > 40 || 320 > previousViewAngles.y > 90) {
                switch ((int)round(rand())){
                    case 1:
                        jitterDeg = 180;
                        break;
                    case 2:
                        jitterDeg = 0;
                        break;
                }
            }

            int jitterHit = (int)(round(rand() * (float)((config->antiAim.JitterChance)/100.0f)));
            int jitterDir = (int)(rand());
            int jitterMult;

            if (!jitterHit)
                return;

            if (jitterDir) { jitterMult = 1;  } else { jitterMult = -1; }

            if (!config->antiAim.currAng) {
                cmd->viewangles.y += ((float)jitterMult * jitterDeg);
            }
            else {
                cmd->viewangles.y = currentViewAngles.y + (((float)jitterMult * jitterDeg));
            }

            CorrectMovement(currentViewAngles.y, cmd, cmd->forwardmove, cmd->sidemove);

        }
        

        // DESYNC
        if (config->antiAim.yaw  && ((cmd->viewangles.y == currentViewAngles.y) || config->antiAim.currViewBypass)) {
            
            bool sendPacket_b = sendPacket;
            if (config->antiAim.swapPacket) {
                sendPacket_b = !sendPacket;
            }

			float max_DeSync = localPlayer->getMaxDesyncAngle();

			if (-2 < config->antiAim.DeSyncManualSet > 2) {
				max_DeSync = config->antiAim.DeSyncManualSet;
			}

				if (config->antiAim.KeyYaw) {
					if (GetAsyncKeyState(config->antiAim.BackwardYKey)) {
						config->antiAim.currYaw = 2;
					}
					else if (GetAsyncKeyState(config->antiAim.LeftYKey)) {
						config->antiAim.currYaw = 1;
					}
					else if (GetAsyncKeyState(config->antiAim.RightYKey)) {
						config->antiAim.currYaw = 3;
					}
					else if (GetAsyncKeyState(config->antiAim.ForwardYKey)) {
						config->antiAim.currYaw = 0;
					}

					switch (config->antiAim.currYaw) {
					case 0:
						break;
					case 1:
						cmd->viewangles.y += 270;
						break;
					case 2:
						cmd->viewangles.y += 180;
						break;
					case 3:
						cmd->viewangles.y += 90;
						break;
					default:
						break;
					}
				}
			

			if (LBY_UPDATE()) {
				cmd->viewangles.y = (cmd->viewangles.y + config->antiAim.manYaw) - max_DeSync;
				CorrectMovement(currentViewAngles.y, cmd, cmd->forwardmove, cmd->sidemove);
				sendPacket_b = false;
			}
            else if (sendPacket_b) { /* fake */
				cmd->viewangles.y = (cmd->viewangles.y + config->antiAim.manYaw);
            }
            else { /* real */

                cmd->viewangles.y += (cmd->viewangles.y + config->antiAim.manYaw) + config->antiAim.clamped;

            }



			sendPacket = sendPacket_b;
            /*
            if (fabsf(cmd->sidemove) < 5.0f) {
                if (cmd->buttons & UserCmd::IN_DUCK)
                    cmd->sidemove = cmd->tickCount & 1 ? 3.25f : -3.25f;
                else
                    cmd->sidemove = cmd->tickCount & 1 ? 1.1f : -1.1f;
            }
            */
        }


    }

}

void AntiAim::fakeWalk(UserCmd* cmd, bool& sendPacket, const Vector& currentViewAngles) noexcept
{
	if (config->antiAim.general.fakeWalk.key != 0) {
		if (config->antiAim.general.fakeWalk.keyMode == 0) {
			if (!GetAsyncKeyState(config->antiAim.general.fakeWalk.key))
			{
				config->antiAim.general.fakeWalk.keyToggled = false;
			}
			else
				config->antiAim.general.fakeWalk.keyToggled = true;
		}
		else {
			if (GetAsyncKeyState(config->antiAim.general.fakeWalk.key) & 1)
				config->antiAim.general.fakeWalk.keyToggled = !config->antiAim.general.fakeWalk.keyToggled;
		}
	}

	if (config->antiAim.general.fakeWalk.enabled && config->antiAim.general.fakeWalk.keyToggled)
	{
		if (interfaces->engine->getNetworkChannel()->chokedPackets < config->antiAim.general.fakeWalk.maxChoke)
		{
			sendPacket = false;
		}
		else if (interfaces->engine->getNetworkChannel()->chokedPackets == config->antiAim.general.fakeWalk.maxChoke)
		{
			sendPacket = false;
		}
		else if (interfaces->engine->getNetworkChannel()->chokedPackets == config->antiAim.general.fakeWalk.maxChoke + 1)
		{
			cmd->forwardmove = 0;

			if (cmd->buttons & UserCmd::IN_DUCK)
				cmd->sidemove = cmd->tickCount & 1 ? 3.25f : -3.25f;
			else
				cmd->sidemove = cmd->tickCount & 1 ? 1.1f : -1.1f;

			sendPacket = false;
		}
		else
		{
			cmd->forwardmove = 0;

			if (cmd->buttons & UserCmd::IN_DUCK)
				cmd->sidemove = cmd->tickCount & 1 ? 3.25f : -3.25f;
			else
				cmd->sidemove = cmd->tickCount & 1 ? 1.1f : -1.1f;

			sendPacket = true;
		}


		//CorrectMovement(currentViewAngles.y, cmd, cmd->forwardmove, cmd->sidemove);
	}


}

/*

void C_CSGOPlayerAnimState::SetupVelocity()
{
	MDLCACHE_CRITICAL_SECTION();

	Vector velocity = m_vVelocity;
	if (Interfaces::EngineClient->IsHLTV() || Interfaces::EngineClient->IsPlayingDemo())
		pBaseEntity->GetAbsVelocity(velocity);
	else
		pBaseEntity->EstimateAbsVelocity(velocity);

	float spd = velocity.LengthSqr();

	if (spd > std::pow(1.2f * 260.0f, 2))
	{
		Vector velocity_normalized = velocity;
		VectorNormalizeFast(velocity_normalized);
		velocity = velocity_normalized * (1.2f * 260.0f);
	}

	m_flAbsVelocityZ = velocity.z;
	velocity.z = 0.0f;

	float leanspd = m_vecLastSetupLeanVelocity.LengthSqr();

	m_bIsAccelerating = velocity.
	Sqr() > leanspd;

	m_vVelocity = GetSmoothedVelocity(m_flLastClientSideAnimationUpdateTimeDelta * 2000.0f, velocity, m_vVelocity);

	m_vVelocityNormalized = VectorNormalizeReturn(m_vVelocity);

	float speed = std::fmin(m_vVelocity.Length(), 260.0f);
	m_flSpeed = speed;

	if (speed > 0.0f)
		m_vecLastAcceleratingVelocity = m_vVelocityNormalized;

	CBaseCombatWeapon *weapon = pBaseEntity->GetWeapon();
	pActiveWeapon = weapon;

	float flMaxMovementSpeed = 260.0f;
	if (weapon)
		flMaxMovementSpeed = std::fmax(weapon->GetMaxSpeed(), 0.001f);

	m_flSpeedNormalized = clamp(m_flSpeed / flMaxMovementSpeed, 0.0f, 1.0f);

	m_flRunningSpeed = m_flSpeed / (flMaxMovementSpeed * 0.520f);
	m_flDuckingSpeed = m_flSpeed / (flMaxMovementSpeed * 0.340f);

	if (m_flRunningSpeed < 1.0f)
	{
		if (m_flRunningSpeed < 0.5f)
		{
			float vel = m_flVelocityUnknown;
			float delta = m_flLastClientSideAnimationUpdateTimeDelta * 60.0f;
			float newvel;
			if ((80.0f - vel) <= delta)
			{
				if (-delta <= (80.0f - vel))
					newvel = 80.0f;
				else
					newvel = vel - delta;
			}
			else
			{
				newvel = vel + delta;
			}
			m_flVelocityUnknown = newvel;
		}
	}
	else
	{
		m_flVelocityUnknown = m_flSpeed;
	}

	bool bWasMovingLastUpdate = false;
	bool bJustStartedMovingLastUpdate = false;
	if (m_flSpeed <= 0.0f)
	{
		m_flTimeSinceStartedMoving = 0.0f;
		bWasMovingLastUpdate = m_flTimeSinceStoppedMoving <= 0.0f;
		m_flTimeSinceStoppedMoving += m_flLastClientSideAnimationUpdateTimeDelta;
	}
	else
	{
		m_flTimeSinceStoppedMoving = 0.0f;
		bJustStartedMovingLastUpdate = m_flTimeSinceStartedMoving <= 0.0f;
		m_flTimeSinceStartedMoving = m_flLastClientSideAnimationUpdateTimeDelta + m_flTimeSinceStartedMoving;
	}

	m_flCurrentFeetYaw = m_flGoalFeetYaw;
	m_flGoalFeetYaw = clamp(m_flGoalFeetYaw, -360.0f, 360.0f);

	float eye_feet_delta = AngleDiff(m_flEyeYaw, m_flGoalFeetYaw);

	float flRunningSpeed = clamp(m_flRunningSpeed, 0.0f, 1.0f);

	float flYawModifier = (((m_flGroundFraction * -0.3f) - 0.2f) * flRunningSpeed) + 1.0f;

	if (m_fDuckAmount > 0.0f)
	{
		float flDuckingSpeed = clamp(m_flDuckingSpeed, 0.0f, 1.0f);
		flYawModifier = flYawModifier + ((m_fDuckAmount * flDuckingSpeed) * (0.5f - flYawModifier));
	}

	float flMaxYawModifier = flYawModifier * m_flMaxYaw;
	float flMinYawModifier = flYawModifier * m_flMinYaw;

	if (eye_feet_delta <= flMaxYawModifier)
	{
		if (flMinYawModifier > eye_feet_delta)
			m_flGoalFeetYaw = fabs(flMinYawModifier) + m_flEyeYaw;
	}
	else
	{
		m_flGoalFeetYaw = m_flEyeYaw - fabs(flMaxYawModifier);
	}

	NormalizeAngle(m_flGoalFeetYaw);

	if (m_flSpeed > 0.1f || fabs(m_flAbsVelocityZ) > 100.0f)
	{
		m_flGoalFeetYaw = ApproachAngle(
			m_flEyeYaw,
			m_flGoalFeetYaw,
			((m_flGroundFraction * 20.0f) + 30.0f)
			* m_flLastClientSideAnimationUpdateTimeDelta);
	}
	else
	{
		m_flGoalFeetYaw = ApproachAngle(
			pBaseEntity->GetLowerBodyYaw(),
			m_flGoalFeetYaw,
			m_flLastClientSideAnimationUpdateTimeDelta * 100.0f);
	}

	C_Anim
	
	
	
	ationLayer *layer3 = pBaseEntity->GetAnimOverlay(3);
	if (layer3 && layer3->m_flWeight > 0.0f)
	{
		IncrementLayerCycle(3, false);
		LayerWeightAdvance(3);
	}

	if (m_flSpeed > 0.0f)
	{
		float velAngle = (atan2(-m_vVelocity.y, -m_vVelocity.x) * 180.0f) * (1.0f / M_PI);

		if (velAngle < 0.0f)
			velAngle += 360.0f;

		m_flGoalMoveDirGoalFeetDelta = AngleNormalize(AngleDiff(velAngle, m_flGoalFeetYaw));
	}

	m_flFeetVelDirDelta = AngleNormalize(AngleDiff(m_flGoalMoveDirGoalFeetDelta, m_flCurrentMoveDirGoalFeetDelta));

	if (bJustStartedMovingLastUpdate && m_flFeetYawRate <= 0.0f)
	{
		m_flCurrentMoveDirGoalFeetDelta = m_flGoalMoveDirGoalFeetDelta;

		C_AnimationLayer *layer = pBaseEntity->GetAnimOverlay(6);
		if (layer && layer->m_nSequence != -1)
		{
			if (*(DWORD*)((DWORD)pBaseEntity->pSeqdesc(layer->m_nSequence) + 0xC4) > 0)
			{
				int tag = ANIMTAG_UNINITIALIZED;

				if (std::fabs(AngleDiff(m_flCurrentMoveDirGoalFeetDelta, 180.0f)) > 22.5f)
				{
					if (std::fabs(AngleDiff(m_flCurrentMoveDirGoalFeetDelta, 135.0f)) > 22.5f)
					{
						if (std::fabs(AngleDiff(m_flCurrentMoveDirGoalFeetDelta, 90.0f)) > 22.5f)
						{
							if (std::fabs(AngleDiff(m_flCurrentMoveDirGoalFeetDelta, 45.0f)) > 22.5f)
							{
								if (std::fabs(AngleDiff(m_flCurrentMoveDirGoalFeetDelta, 0.0f)) > 22.5f)
								{
									if (std::fabs(AngleDiff(m_flCurrentMoveDirGoalFeetDelta, -45.0f)) > 22.5f)
									{
										if (std::fabs(AngleDiff(m_flCurrentMoveDirGoalFeetDelta, -90.0f)) > 22.5f)
										{
											if (std::fabs(AngleDiff(m_flCurrentMoveDirGoalFeetDelta, -135.0f)) <= 22.5f)
												tag = ANIMTAG_STARTCYCLE_NW;
										}
										else
										{
											tag = ANIMTAG_STARTCYCLE_W;
										}
									}
									else
									{
										tag = ANIMTAG_STARTCYCLE_SW;
									}
								}
								else
								{
									tag = ANIMTAG_STARTCYCLE_S;
								}
							}
							else
							{
								tag = ANIMTAG_STARTCYCLE_SE;
							}
						}
						else
						{
							tag = ANIMTAG_STARTCYCLE_E;
						}
					}
					else
					{
						tag = ANIMTAG_STARTCYCLE_NE;
					}
				}
				else
				{
					tag = ANIMTAG_STARTCYCLE_N;
				}
				m_flFeetCycle = pBaseEntity->GetFirstSequenceAnimTag(layer->m_nSequence, tag);
			}
		}

		if (m_flDuckRate >= 1.0f && !clientpad[0] && std::fabs(m_flFeetVelDirDelta) > 45.0f)
		{
			if (m_bOnGround)
			{
				if (pBaseEntity->GetUnknownAnimationFloat() <= 0.0f)
					pBaseEntity->DoUnknownAnimationCode(0.3f);
			}
		}
	}
	else
	{
		if (m_flDuckRate >= 1.0f
			&& !clientpad[0]
			&& std::fabs(m_flFeetVelDirDelta) > 100.0
			&& m_bOnGround
			&& pBaseEntity->GetUnknownAnimationFloat() <= 0.0)
		{
			pBaseEntity->DoUnknownAnimationCode(0.3f);
		}

		C_
 *layer = pBaseEntity->GetAnimOverlay(6);
		if (layer->m_flWeight >= 1.0f)
		{
			m_flCurrentMoveDirGoalFeetDelta = m_flGoalMoveDirGoalFeetDelta;
		}
		else
		{
			float flDuckSpeedClamp = clamp(m_flDuckingSpeed, 0.0f, 1.0f);
			float flRunningSpeed = clamp(m_flRunningSpeed, 0.0f, 1.0f);
			float flBiasMove = Bias(Lerp(m_fDuckAmount, flDuckSpeedClamp, flRunningSpeed), 0.18f);
			m_flCurrentMoveDirGoalFeetDelta = AngleNormalize(((flBiasMove + 0.1f) * m_flFeetVelDirDelta) + m_flCurrentMoveDirGoalFeetDelta);
		}
	}

	m_arrPoseParameters[4].SetValue(pBaseEntity, m_flCurrentMoveDirGoalFeetDelta);




	float eye_goalfeet_delta = AngleDiff(m_flEyeYaw - m_flGoalFeetYaw, 360.0f);

	float new_body_yaw_pose = 0.0f; //not initialized?

	if (eye_goalfeet_delta < 0.0f || m_flMaxYaw == 0.0f)
	{
		if (m_flMinYaw != 0.0f)
			new_body_yaw_pose = (eye_goalfeet_delta / m_flMinYaw) * -58.0f;
	}
	else
	{
		new_body_yaw_pose = (eye_goalfeet_delta / m_flMaxYaw) * 58.0f;
	}





	m_arrPoseParameters[6].SetValue(pBaseEntity, new_body_yaw_pose);

	float eye_pitch_normalized = AngleNormalize(m_flPitch);
	float new_body_pitch_pose;

	if (eye_pitch_normalized <= 0.0f)
		new_body_pitch_pose = (eye_pitch_normalized / m_flMaximumPitch) * -90.0f;
	else
		new_body_pitch_pose = (eye_pitch_normalized / m_flMinimumPitch) * 90.0f;

	m_arrPoseParameters[7].SetValue(pBaseEntity, new_body_pitch_pose);

	m_arrPoseParameters[1].SetValue(pBaseEntity, m_flRunningSpeed);

	m_arrPoseParameters[9].SetValue(pBaseEntity, m_flDuckRate * m_fDuckAmount);
}


*/


/*


m_flGoalFeetYaw = clamp(m_flGoalFeetYaw, -360.0f, 360.0f);

	float EyeFeetDelta = AngleDiff(m_flEyeYaw, m_flGoalFeetYaw);
	float flRunningSpeed = clamp(m_flRunningSpeed, 0.0f, 1.0f);
	float flYawModifier = (((m_flGroundFraction * -0.3f) - 0.2f) * flRunningSpeed) + 1.0f;

	if (m_fDuckAmount > 0.0f)  {

		float flDuckingSpeed = clamp(m_flDuckingSpeed, 0.0f, 1.0f);
		flYawModifier = flYawModifier + ((m_fDuckAmount * flDuckingSpeed) * (0.5f - flYawModifier));
	}

	float flMaxYawModifier = flYawModifier * m_flMaxYaw;
	float flMinYawModifier = flYawModifier * m_flMinYaw;

	if (EyeFeetDelta <= flMaxYawModifier) {

		if (flMinYawModifier > EyeFeetDelta)

			m_flGoalFeetYaw = fabs(flMinYawModifier) + m_flEyeYaw;
	}
	else {

		m_flGoalFeetYaw = m_flEyeYaw - fabs(flMaxYawModifier);
	}

	NormalizeAngle(m_flGoalFeetYaw);

	if (m_flSpeed > 0.1f || fabs(m_flAbsVelocityZ) > 100.0f) {

		m_flGoalFeetYaw = ApproachAngle(
			m_flEyeYaw,
			m_flGoalFeetYaw,
			((m_flGroundFraction * 20.0f) + 30.0f)
			* m_flLastClientSideAnimationUpdateTimeDelta);
	}
	else {

		m_flGoalFeetYaw = ApproachAngle(
			pBaseEntity->m_flLowerBodyYawTarget(),
			m_flGoalFeetYaw,
			m_flLastClientSideAnimationUpdateTimeDelta * 100.0f);
	}



	m_flGoalFeetYaw = clamp(m_flGoalFeetYaw, -360.0f, 360.0f);

	m_flGoalFeetYaw = clamp(m_flGoalFeetYaw, m_flEyeYaw + fMaxDesyncDelta, m_flEyeYaw - fMaxDesyncDelta);

	NormalizeAngle(m_flGoalFeetYaw);

	if (m_flSpeed > 0.1f || m_flAbsVelocityZ > 100.0f) {

		m_flGoalFeetYaw = m_flEyeYaw;
	}
	else {

		m_flGoalFeetYaw = pBaseEntity->m_flLowerBodyYawTarget();
	}

*/