#include "Aimbot.h"

#include "../Config.h"
#include "../SDK/ConVar.h"
#include "../Interfaces.h"
#include "../Memory.h"
#include "../SDK/Entity.h"
#include "../SDK/UserCmd.h"
#include "../SDK/Vector.h"
#include "../SDK/WeaponId.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/PhysicsSurfaceProps.h"
#include "../SDK/WeaponData.h"
#include "../SDK/ModelInfo.h"
#include "../SDK/matrix3x4.h"
#include "../SDK/Math.h"
#include "../Multipoints.h"
#include "Resolver.h"
#include "AutoWall.h"
#include "Backtrack.h"
//DEBUG
#include <fstream>
#include <iostream>
#include <cstddef>
#include <thread>
#include <future>

#include <math.h>

Vector Aimbot::calculateRelativeAngle(const Vector& source, const Vector& destination, const Vector& viewAngles) noexcept
{
    Vector delta = destination - source;
    Vector angles{ radiansToDegrees(atan2f(-delta.z, std::hypotf(delta.x, delta.y))) - viewAngles.x,
                   radiansToDegrees(atan2f(delta.y, delta.x)) - viewAngles.y };
    angles.normalize();
    return angles;
}

static float handleBulletPenetration(SurfaceData* enterSurfaceData, const Trace& enterTrace, const Vector& direction, Vector& result, float penetration, float damage) noexcept
{
    Vector end;
    Trace exitTrace;
    __asm {
        mov ecx, end
        mov edx, enterTrace
    }
    if (!memory->traceToExit(enterTrace.endpos.x, enterTrace.endpos.y, enterTrace.endpos.z, direction.x, direction.y, direction.z, exitTrace))
        return -1.0f;

    SurfaceData* exitSurfaceData = interfaces->physicsSurfaceProps->getSurfaceData(exitTrace.surface.surfaceProps);

    float damageModifier = 0.16f;
    float penetrationModifier = (enterSurfaceData->penetrationmodifier + exitSurfaceData->penetrationmodifier) / 2.0f;

    if (enterSurfaceData->material == 71 || enterSurfaceData->material == 89) {
        damageModifier = 0.05f;
        penetrationModifier = 3.0f;
    } else if (enterTrace.contents >> 3 & 1 || enterTrace.surface.flags >> 7 & 1) {
        penetrationModifier = 1.0f;
    }

    if (enterSurfaceData->material == exitSurfaceData->material) {
        if (exitSurfaceData->material == 85 || exitSurfaceData->material == 87)
            penetrationModifier = 3.0f;
        else if (exitSurfaceData->material == 76)
            penetrationModifier = 2.0f;
    }

    damage -= 11.25f / penetration / penetrationModifier + damage * damageModifier + (exitTrace.endpos - enterTrace.endpos).squareLength() / 24.0f / penetrationModifier;

    result = exitTrace.endpos;
    return damage;
}

static bool canScan(Entity* entity, const Vector& destination, const WeaponInfo* weaponData, int minDamage, bool allowFriendlyFire, float& damageret) noexcept
{
    if (!localPlayer)
        return false;

    float damage{ static_cast<float>(weaponData->damage) };

    Vector start{ localPlayer->getEyePosition() };
    Vector direction{ destination - start };
    direction /= direction.length();

    int hitsLeft = 4;

    while (damage >= 1.0f && hitsLeft) {
        Trace trace;
        interfaces->engineTrace->traceRay({ start, destination }, 0x4600400B, localPlayer.get(), trace);

        if (!allowFriendlyFire && trace.entity && trace.entity->isPlayer() && !localPlayer->isOtherEnemy(trace.entity))
            return false;

        if (trace.fraction == 1.0f)
            break;

        if (trace.entity == entity && trace.hitgroup > HitGroup::Generic && trace.hitgroup <= HitGroup::RightLeg) {
            damage = HitGroup::getDamageMultiplier(trace.hitgroup) * damage * powf(weaponData->rangeModifier, trace.fraction * weaponData->range / 500.0f);

            if (float armorRatio{ weaponData->armorRatio / 2.0f }; HitGroup::isArmored(trace.hitgroup, trace.entity->hasHelmet()))
                damage -= (trace.entity->armor() < damage * armorRatio / 2.0f ? trace.entity->armor() * 4.0f : damage) * (1.0f - armorRatio);

            damageret = damage;
            return (damage >= minDamage) || (damage >= entity->health());


        }
        const auto surfaceData = interfaces->physicsSurfaceProps->getSurfaceData(trace.surface.surfaceProps);

        if (surfaceData->penetrationmodifier < 0.1f)
            break;

        damage = handleBulletPenetration(surfaceData, trace, direction, start, weaponData->penetration, damage);

        damageret = damage;

        hitsLeft--;
    }
    return false;
}



static bool Aimbot::HitChance(Vector angles, Entity* entity, Entity* weapon, int weaponIndex, UserCmd* cmd, const int chance) noexcept
{
    if (!chance)
        return true;

    int hitseed = 256;

    int iHit = 0;
    int iHitsNeed = (int)((float)hitseed * ((float)chance / 100.f));
    bool bHitchance = false;

    Vector forward, right, up;
    Math::AngleVectors(angles, &forward, &right, &up);

    weapon->UpdateAccuracyPenalty();

    for (auto i = 0; i < hitseed; ++i) {

        float RandomA = Math::RandomFloat(0.0f, 1.0f);
        float RandomB = 1.0f - RandomA * RandomA;
        RandomA = Math::RandomFloat(0.0f, M_PIF * 2.0f);
        RandomB *= weapon->getSpread() + weapon->getInaccuracy();
        float SpreadX1 = (cos(RandomA) * RandomB);
        float SpreadY1 = (sin(RandomA) * RandomB);
        float RandomC = Math::RandomFloat(0.0f, 1.0f);
        float RandomF = RandomF = 1.0f - RandomC * RandomC;
        RandomC = Math::RandomFloat(0.0f, M_PIF * 2.0f);
        RandomF *= weapon->getSpread();
        float SpreadX2 = (cos(RandomC) * RandomF);
        float SpreadY2 = (sin(RandomC) * RandomF);
        float fSpreadX = SpreadX1 + SpreadX2;
        float fSpreadY = SpreadY1 + SpreadY2;

        Vector vSpreadForward;
        vSpreadForward.x = forward.x + (fSpreadX * right.x) + (fSpreadY * up.x);
        vSpreadForward.y = forward.y + (fSpreadX * right.y) + (fSpreadY * up.y);
        vSpreadForward.z = forward.z + (fSpreadX * right.z) + (fSpreadY * up.z);
        vSpreadForward.NormalizeInPlace();

        Vector qaNewAngle;
        Math::VectorAngles(vSpreadForward, qaNewAngle);
        qaNewAngle.normalize();

        Vector vEnd;
        Math::AngleVectors(qaNewAngle, &vEnd);
        vEnd = localPlayer->getEyePosition() + (vEnd * 8192.f);

        if (Autowall->PenetrateWall(entity, vEnd, weaponIndex))
            iHit++;

        if ((int)(((float)iHit / 256.f) * 100.f) >= chance) {
            bHitchance = true;
            break;
        }
        if ((256.f - 1 - i + iHit) < iHitsNeed)
            break;
    }
    return bHitchance;
}


void Aimbot::Autostop(UserCmd* cmd) noexcept
{

    if (!localPlayer || !localPlayer->isAlive())
        return;

    Vector Velocity = localPlayer->velocity();

    if (Velocity.length2D() == 0)
        return;

    static float Speed = 450.f;

    Vector Direction;
    Vector RealView;
    Math::VectorAngles(Velocity, Direction);
    interfaces->engine->getViewAngles(RealView);
    Direction.y = RealView.y - Direction.y;

    Vector Forward;
    Math::AngleVectors(Direction, &Forward);
    Vector NegativeDirection = Forward * -Speed;

    cmd->forwardmove = NegativeDirection.x;
    cmd->sidemove = NegativeDirection.y;
}

/*
void Aimbot::run(UserCmd* cmd) noexcept
{
    if (!localPlayer || localPlayer->nextAttack() > memory->globalVars->serverTime())
        return;

    const auto activeWeapon = localPlayer->getActiveWeapon();
    if (!activeWeapon || !activeWeapon->clip())
        return;

    auto weaponIndex = getWeaponIndex(activeWeapon->itemDefinitionIndex2());
    if (!weaponIndex)
        return;

    auto weaponClass = getWeaponClass(activeWeapon->itemDefinitionIndex2());
    if (!config->aimbot[weaponIndex].enabled)
        weaponIndex = weaponClass;

    if (!config->aimbot[weaponIndex].enabled)
        weaponIndex = 0;

    if (!config->aimbot[weaponIndex].betweenShots && activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime())
        return;

    if (!config->aimbot[weaponIndex].ignoreFlash && localPlayer->isFlashed())
        return;

    if (config->aimbot[weaponIndex].onKey) {
        if (!config->aimbot[weaponIndex].keyMode) {
            if (!GetAsyncKeyState(config->aimbot[weaponIndex].key))
                return;
        }
        else {
            static bool toggle = true;
            if (GetAsyncKeyState(config->aimbot[weaponIndex].key) & 1)
                toggle = !toggle;
            if (!toggle)
                return;
        }
    }

    if (activeWeapon->isKnife() || activeWeapon->isGrenade())
        return;

    Entity* Target{};
    Vector AimPoint{};




    if (config->aimbot[weaponIndex].enabled && (cmd->buttons & UserCmd::IN_ATTACK || config->aimbot[weaponIndex].autoShot || config->aimbot[weaponIndex].aimlock) && activeWeapon->getInaccuracy() <= config->aimbot[weaponIndex].maxAimInaccuracy) {}





}

*/


void Aimbot::oldstyle(UserCmd* cmd) noexcept
{
    if (!localPlayer || localPlayer->nextAttack() > memory->globalVars->serverTime())
        return;

    const auto activeWeapon = localPlayer->getActiveWeapon();
    if (!activeWeapon || !activeWeapon->clip())
        return;

    auto weaponIndex = getWeaponIndex(activeWeapon->itemDefinitionIndex2());
    if (!weaponIndex)
        return;

    auto weaponClass = getWeaponClass(activeWeapon->itemDefinitionIndex2());
    if (!config->aimbot[weaponIndex].enabled)
        weaponIndex = weaponClass;

    if (!config->aimbot[weaponIndex].enabled)
        weaponIndex = 0;

    if (!config->aimbot[weaponIndex].betweenShots && activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime())
        return;

    if (!config->aimbot[weaponIndex].ignoreFlash && localPlayer->isFlashed())
        return;

    if (config->aimbot[weaponIndex].onKey) {
        if (!config->aimbot[weaponIndex].keyMode) {
            if (!GetAsyncKeyState(config->aimbot[weaponIndex].key))
                return;
        }
        else {
            static bool toggle = true;
            if (GetAsyncKeyState(config->aimbot[weaponIndex].key) & 1)
                toggle = !toggle;
            if (!toggle)
                return;
        }
    }


    if (config->aimbot[weaponIndex].enabled && (cmd->buttons & UserCmd::IN_ATTACK || config->aimbot[weaponIndex].autoShot || config->aimbot[weaponIndex].aimlock) && activeWeapon->getInaccuracy() <= config->aimbot[weaponIndex].maxAimInaccuracy) {

        if (config->aimbot[weaponIndex].scopedOnly && activeWeapon->isSniperRifle() && !localPlayer->isScoped())
            return;

        auto bestFov = config->aimbot[weaponIndex].fov;
        Vector bestTarget{ };
        auto localPlayerEyePosition = localPlayer->getEyePosition();

        const auto aimPunch = activeWeapon->requiresRecoilControl() ? localPlayer->getAimPunch() : Vector{ };




        for (int i = 1; i <= interfaces->engine->getMaxClients(); i++) {
            auto entity = interfaces->entityList->getEntity(i);
            if (!entity || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive()
                || !entity->isOtherEnemy(localPlayer.get()) && !config->aimbot[weaponIndex].friendlyFire || entity->gunGameImmunity())
                continue;

            auto boneList = config->aimbot[weaponIndex].bone == 1 ? std::initializer_list{ 8, 4, 3, 7, 6, 5 } : std::initializer_list{ 8, 7, 6, 5, 4, 3 };

            if (!config->aimbot[weaponIndex].multipointenabled) {

                float damageret = 0;
                float maxDamage = 0;
                for (auto bone : boneList) {
                    auto bonePosition = entity->getBonePosition(config->aimbot[weaponIndex].bone > 1 ? 10 - config->aimbot[weaponIndex].bone : bone);





                    if (!entity->isVisible(bonePosition) && (config->aimbot[weaponIndex].visibleOnly || !canScan(entity, bonePosition, activeWeapon->getWeaponData(), config->aimbot[weaponIndex].killshot ? entity->health() : config->aimbot[weaponIndex].minDamage, config->aimbot[weaponIndex].friendlyFire, damageret)))
                        continue;

                    auto angle = calculateRelativeAngle(localPlayerEyePosition, bonePosition, cmd->viewangles + aimPunch);
                    auto fov = std::hypotf(angle.x, angle.y);
                    if (fov < bestFov) {
                        bestFov = fov;
                        bestTarget = bonePosition;
                    }
                    if (config->aimbot[weaponIndex].bone)
                        break;
                }
            }
            else {

                //std::ofstream myfile;
                //myfile.open("C:\\Users\\userWIN\\source\\repos\\test\\Debug\\aimbotlog.txt", std::ios_base::app);

                    // myfile << "Get Model Function called\n";

                const auto model = entity->getModel();
                if (!model) {
                    //myfile << "Get Model returned null model, exiting\n";
                    return;
                }
                //myfile << "Calling getStudioModel\n";
                const auto studioModel = interfaces->modelInfo->getStudioModel(model);
                if (!studioModel) {
                    //myfile << "getStudioModel retyrbed null model\n";
                    return;
                }

                // StudioHitboxSet* getHitboxSet(int i)
                // myfile << "Calling getHitboxSet\n";
                const auto studiohitboxset = studioModel->getHitboxSet(0);
                if (!studiohitboxset) {
                    // myfile << "getHitboxSet returned null \n";
                    return;
                }

                //if (!(matrix3x4 boneMatrices[256]; entity->setupBones(boneMatrices, 256, 256, 0.0f)))
                //   return;

                // Multipoint StudioHitBoxCode 
                float maxDamage = 0;
                float damage = 0;

                for (auto bone : boneList) {

                    //myfile << "bone = " << bone << "\n";
                    auto studiobone = studiohitboxset->getHitbox(config->aimbot[weaponIndex].bone > 1 ? 10 - config->aimbot[weaponIndex].bone : bone);

                    if (!studiobone) {
                        //myfile << "No Studiobone returned from getHitBox\n";
                        return;
                    }

                    //myfile << "studiobone->bone gives " << studiobone->bone;


                    matrix3x4 boneMatrices[256];
                    entity->setupBones(boneMatrices, 256, 256, 0.0f);

                    auto basebonePosition = entity->getBonePosition(studiobone->bone);

                    //myfile << "\nbasebonePosition = " << basebonePosition.x << " " << basebonePosition.y << " " << basebonePosition.z;



                   // myfile << "\nstudio->bbMin " << studiobone->bbMin.x << " " << studiobone->bbMin.y <<  " " << studiobone->bbMin.z;
                    //myfile << "\nstudio->bbMax " << studiobone->bbMax.x << " " << studiobone->bbMax.y << " " << studiobone->bbMax.z;

                    Vector bbmin = studiobone->bbMin * config->aimbot[weaponIndex].multidistance;
                    Vector bbmax = studiobone->bbMax * config->aimbot[weaponIndex].multidistance;

                    Vector points[] = { Vector{bbmin.x, bbmin.y, bbmin.z},
                         Vector{bbmin.x, bbmax.y, bbmin.z},
                         Vector{bbmax.x, bbmax.y, bbmin.z},
                         Vector{bbmax.x, bbmin.y, bbmin.z},
                         Vector{bbmax.x, bbmax.y, bbmax.z},
                         Vector{bbmin.x, bbmax.y, bbmax.z},
                         Vector{bbmin.x, bbmin.y, bbmax.z},
                         Vector{bbmax.x, bbmin.y, bbmax.z},
                    };

                    //auto bonePosition = entity->getBonePosition(config->aimbot[weaponIndex].bone > 1 ? 10 - config->aimbot[weaponIndex].bone : bone);
                    Vector bonePositions[8];
                    bonePositions[0] = basebonePosition;
                    int i = 1;
                    for (auto point : points) {
                        bonePositions[i] = point.transform(boneMatrices[bone]);
                        //myfile << "bonePosition: " << i << " is " << bonePositions[i].x << " " << bonePositions[i].y << " " << bonePositions->z << "\n";
                        i++;
                    }

                    //Vector bonePositions[] = { (basebonePosition - (studiobone->bbMin * config->aimbot[weaponIndex].multidistance)), basebonePosition, (basebonePosition + (studiobone->bbMax * config->aimbot[weaponIndex].multidistance)) };





                    for (auto bonePosition : bonePositions) {

                        // myfile << "\nCurrent Bone Position in multipoint is: " << bonePosition.x << " " << bonePosition.y << " " << bonePosition.z << "\n";
                        if (!entity->isVisible(bonePosition) && (config->aimbot[weaponIndex].visibleOnly || !canScan(entity, bonePosition, activeWeapon->getWeaponData(), config->aimbot[weaponIndex].killshot ? entity->health() : config->aimbot[weaponIndex].minDamage, config->aimbot[weaponIndex].friendlyFire, damage)))
                            continue;

                        auto angle = calculateRelativeAngle(localPlayerEyePosition, bonePosition, cmd->viewangles + aimPunch);
                        auto fov = std::hypotf(angle.x, angle.y);
                        if (fov < bestFov) {
                            bestFov = fov;
                            if ((damage > maxDamage)) {
                                bestTarget = bonePosition;
                                maxDamage = damage;

                            }

                            if ((damage > 100.0f) || (entity->health() - damage <= 0)) {
                                bestTarget = bonePosition;
                                maxDamage = damage;
                                break;
                            }

                        }
                        // myfile << "Curr Damage is: " << damage << " best damage is: " << maxDamage << "\n";


                    }

                    //myfile << "Best target is " << bestTarget.x << " " << bestTarget.y << " " << bestTarget.z << "\n";
                   // myfile.close();
                    if (config->aimbot[weaponIndex].bone || (damage > 100.0f) || (damage > entity->health()))
                        break;


                }









            }
        }




        if (bestTarget.notNull() && (config->aimbot[weaponIndex].ignoreSmoke
            || !memory->lineGoesThroughSmoke(localPlayer->getEyePosition(), bestTarget, 1))) {

            static Vector lastAngles{ cmd->viewangles };
            static int lastCommand{ };

            if (lastCommand == cmd->commandNumber - 1 && lastAngles.notNull() && config->aimbot[weaponIndex].silent)
                cmd->viewangles = lastAngles;

            auto angle = calculateRelativeAngle(localPlayer->getEyePosition(), bestTarget, cmd->viewangles + aimPunch);
            bool clamped{ false };

            if (fabs(angle.x) > config->misc.maxAngleDelta || fabs(angle.y) > config->misc.maxAngleDelta) {
                angle.x = std::clamp(angle.x, -config->misc.maxAngleDelta, config->misc.maxAngleDelta);
                angle.y = std::clamp(angle.y, -config->misc.maxAngleDelta, config->misc.maxAngleDelta);
                clamped = true;
            }

            angle /= config->aimbot[weaponIndex].smooth;
            cmd->viewangles += angle;
            if (!config->aimbot[weaponIndex].silent)
                interfaces->engine->setViewAngles(cmd->viewangles);

            if (config->aimbot[weaponIndex].autoScope && activeWeapon->nextPrimaryAttack() <= memory->globalVars->serverTime() && activeWeapon->isSniperRifle() && !localPlayer->isScoped())
                cmd->buttons |= UserCmd::IN_ATTACK2;

     
            if (config->aimbot[weaponIndex].autoShot && activeWeapon->nextPrimaryAttack() <= memory->globalVars->serverTime() && !clamped && activeWeapon->getInaccuracy() <= config->aimbot[weaponIndex].maxShotInaccuracy)
                cmd->buttons |= UserCmd::IN_ATTACK;

            if (clamped)
                cmd->buttons &= ~UserCmd::IN_ATTACK;

            if (clamped || config->aimbot[weaponIndex].smooth > 1.0f) lastAngles = cmd->viewangles;
            else lastAngles = Vector{ };

            lastCommand = cmd->commandNumber;
        }
    }
}






// retrieveOne(Entity* entity, float multiPointsExpansion, Vector(&multiPoints)[Multipoints::MULTIPOINTS_MAX], int desiredHitBox)
// bool retrieveAll(Entity * entity, float multiPointsExpansion, Vector(&multiPoints)[Multipoints::HITBOX_MAX][Multipoints::MULTIPOINTS_MAX])


bool Aimbot::GetAllPoints(Entity* entity, int weaponIndex, std::vector<Vector> &AimPoint) { // Gets Multipoint Expansion

    if ((config->aimbot[weaponIndex].bone != 0) && (config->aimbot[weaponIndex].bone != 1)) { //If not nearest or best damage, get bone selected
        Vector bonePoints[Multipoints::MULTIPOINTS_MAX];
        if (!Multipoints::retrieveOne(entity, config->aimbot[weaponIndex].multidistance, bonePoints, config->aimbot[weaponIndex].bone)) // <---- take a look at this code 
            return true;

        for (auto point : bonePoints) {
            AimPoint.push_back(point);
        }

    }
    else { // else get all points
        Vector bonePointsMulti[Multipoints::HITBOX_MAX][Multipoints::MULTIPOINTS_MAX];
        if (!Multipoints::retrieveAll(entity, config->aimbot[weaponIndex].multidistance, bonePointsMulti))
            return true;

        for (int i = 0; i < Multipoints::HITBOX_MAX; i++) {
            //Vector bone[Multipoints::MULTIPOINTS_MAX];
            for (int b = 0; b < Multipoints::MULTIPOINTS_MAX; b++) {
                Vector point = bonePointsMulti[i][b];
                AimPoint.push_back(point);
            }
        }
    }

    return false;
}
bool Aimbot::GetBestPoint(Entity* entity, int weaponIndex, std::vector<Vector> &AimPoints, Vector &BestPoint) {

    float bestDamage = 0;
    float damage = 0;
    for (auto point : AimPoints) {
        damage = Autowall->Damage(point);

        if (damage > bestDamage) {
            bestDamage = damage;
            BestPoint = point;
        }
        else if (damage > entity->health()) {
            BestPoint = point;
            return false;
        }
        else if ((damage > config->aimbot[weaponIndex].minDamage) && (!config->aimbot[weaponIndex].killshot) && (config->aimbot[weaponIndex].bone == 0)){ // if nearest it set, exit on first bone that fufills requirements
            BestPoint = point;
            return false;
        }
    }
    
    if (damage < config->aimbot[weaponIndex].minDamage)
        return true;

    return false;


}


inline float DistanceToEntity(Entity* Enemy)
{
    return sqrt(pow(localPlayer->origin().x - Enemy->origin().x, 2) + pow(localPlayer->origin().y - Enemy->origin().y, 2) + pow(localPlayer->origin().z - Enemy->origin().z, 2));
}



bool Aimbot::ScanAllPoints(Entity* entity, int weaponIndex, Vector& AimPoint, int bone) { // Gets Multipoint Expansion

    if ((bone != 0) && (bone != 1)) { //If not nearest or best damage, get bone selected
        Vector bonePoints[Multipoints::MULTIPOINTS_MAX];
        if (!Multipoints::retrieveOne(entity, config->aimbot[weaponIndex].multidistance, bonePoints, bone)) // <---- take a look at this code 
            return false;
        int i = 0;
        for (auto point : bonePoints) {
            if (willPointWork(entity, weaponIndex, point)) {
                AimPoint = point;
                return true;
            }  else if ((config->aimbot[weaponIndex].optimize)) {
                i = i + 1;
            }
        }

    }
    else { // else get all points
        Vector bonePointsMulti[Multipoints::HITBOX_MAX][Multipoints::MULTIPOINTS_MAX];
        if (!Multipoints::retrieveAll(entity, config->aimbot[weaponIndex].multidistance, bonePointsMulti))
            return false;

       
        
        
        struct Point_Inf{
            float Damage;
            Vector Point;
            std::shared_future<float> future;
        };
        
        std::vector<Point_Inf> All_Points;

        for (int i = 0; i < Multipoints::HITBOX_MAX; i++) {
            for (int b = 0; b < Multipoints::MULTIPOINTS_MAX; b++) {
                Vector point = bonePointsMulti[i][b];
                if (willPointWork(entity, weaponIndex, point)) {
                    AimPoint = point;
                    return true;
                }
                else if ((config->aimbot[weaponIndex].optimize)) {
                    b = b + 1;
                    //if (b / 2 > 2)
                    //    break;
                }

            }
        }
        /*
        for (int i = 0; i < Multipoints::HITBOX_MAX; i++) {
            //std::vector<std::shared_future<float>> futures;
            std::vector<Point_Inf> Points;
            for (int b = 0; b < Multipoints::MULTIPOINTS_MAX; b++) {
                Point_Inf curr_point_inf;
                curr_point_inf.Point = bonePointsMulti[i][b];
                std::shared_future future = std::async(willPointWork, entity, weaponIndex, curr_point_inf.Point);
                curr_point_inf.future = std::move(future);
                Points.push_back(std::move(curr_point_inf));

            }

            if (Points.empty())
                return false;

            for (auto point_inf : Points) {
                point_inf.Damage = point_inf.future.get();
                All_Points.push_back(point_inf);
            }

            //Vector bone[Multipoints::MULTIPOINTS_MAX];

            if (All_Points.empty())
                return false;

            struct entity_sort {
                bool operator() (Point_Inf A, Point_Inf B) {
                    return (A.Damage > B.Damage);
                }
            } point_sort;

            std::sort(All_Points.begin(), All_Points.end(), point_sort);
            if (All_Points.front().Damage > 0) {
                AimPoint = All_Points.front().Point;
                return true;
            }

        }
        */



    }

    return false;
}



float Aimbot::willPointWork(Entity* entity, int weaponIndex, Vector AimPoint) {

    float bestDamage = 0;
    float damage = 0;
    Vector vAimPoint = AimPoint;
    damage = Autowall->Damage(vAimPoint);

    if (damage > entity->health()) {
            return damage;
    }
    else if ((damage > config->aimbot[weaponIndex].minDamage) && (!config->aimbot[weaponIndex].killshot)) { // if nearest it set, exit on first bone that fufills requirements
            return damage;
    }

    return 0;
}



bool Aimbot::BaseLogic(int entity_indx, int weaponIndex, std::vector<Vector> AimPoint, Vector& BestPoint, int bone) {
    auto entity = interfaces->entityList->getEntity(entity_indx);
    if (!entity || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive()
        || !entity->isOtherEnemy(localPlayer.get()) && !config->aimbot[weaponIndex].friendlyFire || entity->gunGameImmunity())
        return false;



    if ((bone == 1) || (bone == 0) && ((config->aimbot[weaponIndex].bone != 8) && (config->aimbot[weaponIndex].bone != 9))) {
        if (!config->aimbot[weaponIndex].optimize) {
            if (GetAllPoints(entity, weaponIndex, AimPoint))
                return false;

            if (GetBestPoint(entity, weaponIndex, AimPoint, BestPoint))
                return false;

            return true;
        }
        else {
            if (ScanAllPoints(entity, weaponIndex, BestPoint, bone)) {
                return true;
            }
        }
    }
    else {
        if (!config->aimbot[weaponIndex].optimize) {
            if (GetAllPoints(entity, weaponIndex, AimPoint))
                return false;

            if (GetBestPoint(entity, weaponIndex, AimPoint, BestPoint))
                return false;
            return true;
        }
        else {
            if (ScanAllPoints(entity, weaponIndex, BestPoint, bone - 2)) {
                return true;
            }
        }
    }

    return false;
}


/*

vector<int> vec{ }



int Array[];


*/



void Aimbot::run(UserCmd* cmd) noexcept {

        if (!localPlayer || localPlayer->nextAttack() > memory->globalVars->serverTime())
            return;

        const auto activeWeapon = localPlayer->getActiveWeapon();
        if (!activeWeapon || !activeWeapon->clip())
            return;

        auto weaponIndex = getWeaponIndex(activeWeapon->itemDefinitionIndex2());
        if (!weaponIndex)
            return;

        auto weaponClass = getWeaponClass(activeWeapon->itemDefinitionIndex2());
        if (!config->aimbot[weaponIndex].enabled)
            weaponIndex = weaponClass;

        if (!config->aimbot[weaponIndex].enabled)
            weaponIndex = 0;

        if (!config->aimbot[weaponIndex].betweenShots && activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime())
            return;

        if (!config->aimbot[weaponIndex].ignoreFlash && localPlayer->isFlashed())
            return;

        if (activeWeapon->getInaccuracy() >= config->aimbot[weaponIndex].maxTriggerInaccuracy)
        {
            oldstyle(cmd);
            return;
        }

        if (config->aimbot[weaponIndex].onKey) {
            if (!config->aimbot[weaponIndex].keyMode) {
                if (!GetAsyncKeyState(config->aimbot[weaponIndex].key))
                    return;
            }
            else {
                static bool toggle = true;
                if (GetAsyncKeyState(config->aimbot[weaponIndex].key) & 1)
                    toggle = !toggle;
                if (!toggle)
                    return;
            }
        }

        if (config->aimbot[weaponIndex].oldstyle) {
            oldstyle(cmd);
            return;
        }

        if (activeWeapon->isKnife() || activeWeapon->isGrenade())
            return;

        Entity* Target = interfaces->entityList->getEntity(localPlayer->index());
        std::vector<Vector> AimPoint;
        Vector BestPoint;

        struct EntityVal {
            Entity* entity_ptr;
            int entity = 0;
            float distance = 0;
            int health = 0;
            bool isVisible = false;
            bool wasTargeted = false;
            Resolver::Record* record = &Resolver::invalid_record;
        };
        std::vector<EntityVal> enemies;

        struct entity_sort {
            bool operator() (EntityVal A, EntityVal B) { 
                if (A.wasTargeted && !B.wasTargeted) {
                    return true;
                }
                else if (!A.wasTargeted && B.wasTargeted) {
                    return false;
                } 
                
                if (A.isVisible && !B.isVisible) {
                    return true;
                } else if (!A.isVisible && B.isVisible){
                    return false;
                }

                if (A.distance < B.distance) {
                    return true;
                }
                else {
                    return false;
                }
                if (A.entity < B.entity) { // WTF IS THIS
                    return true;
                }

            }
        } ent_sort;


        static auto frameRate = 1.0f;
        frameRate = 1/(0.9f * frameRate + 0.1f * memory->globalVars->absoluteFrameTime);



        for (int i = 1; i <= interfaces->engine->getMaxClients(); i++) {
            auto entity = interfaces->entityList->getEntity(i);
            if (!entity || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive()
                || !entity->isOtherEnemy(localPlayer.get()) && !config->aimbot[weaponIndex].friendlyFire || entity->gunGameImmunity())
                continue;

            EntityVal curr_ent;
            curr_ent.distance = DistanceToEntity(entity);
            curr_ent.entity = entity->index();
            curr_ent.health = entity->health();
            curr_ent.entity_ptr = entity;
            auto record = &Resolver::PlayerRecords.at(i);
            if (record && !record->invalid) {
                curr_ent.wasTargeted = record->wasTargeted;
                curr_ent.record = record;
            }
            else {
                curr_ent.wasTargeted = false;
            }
            curr_ent.isVisible = entity->isVisible();
            /*
            if (config->optimization.nearestonly.enabled && (frameRate < config->optimzation.nearestonly.frames)) {
                if (!(curr_ent.isVisible) && !(curr_ent.wasTargeted) curr_ent.distance < 2000) {
                    continue;
                }
            }
            */

            enemies.push_back(curr_ent);
        }

        if (enemies.empty()) {
            return;
        }

        if (enemies.size() > 1) {
            std::sort(enemies.begin(), enemies.end(), ent_sort);
        }

        Resolver::Record* record = &Resolver::invalid_record;



        if ((frameRate < 50) && config->debug.TraceLimit && (enemies.size() > 3)) {
            enemies.erase((enemies.begin()+2), enemies.end());
        }

        for (auto enemy : enemies) {
            bool baimTriggered = false;
            record = enemy.record;
            if (record && !record->invalid) {
                if (record->missedshots > config->aimbot[weaponIndex].baimshots) {
                    baimTriggered = true;
                    if (config->aimbot[weaponIndex].onshot) {

                        auto Animstate = enemy.entity_ptr->getAnimstate();

                        if (!Animstate)
                            continue;

                        int currAct = 1;


                        for (int b = 0; b < enemy.entity_ptr->getAnimationLayerCount(); b++) {
                            auto AnimLayer = enemy.entity_ptr->getAnimationLayer(b);
                            currAct = enemy.entity_ptr->getSequenceActivity(AnimLayer->sequence);
                            
                            if (currAct == ACT_CSGO_FIRE_PRIMARY && (AnimLayer->weight > (0.5f)) && (AnimLayer->cycle < .8f)){
                                baimTriggered = false;
                                break;
                            }

                        };
                    }
                }
            }
            if ((config->aimbot[weaponIndex].baim && ((GetAsyncKeyState(config->aimbot[weaponIndex].baimkey) || baimTriggered)))) {
                int bodyAim[] = { 6, 5, 4, 3, 2 };
                for (int bone : bodyAim) {
                    if (BaseLogic(enemy.entity, weaponIndex, AimPoint, BestPoint, bone)) {
                        Target = enemy.entity_ptr;
                        break;
                    }
                }
            }
            else if ((config->aimbot[weaponIndex].bone == 8) || (config->aimbot[weaponIndex].bone == 9)) {
                std::vector<int> bones;
                for (int i = 0; i < Multipoints::HITBOX_MAX; i++) {
                    if (config->aimbot[weaponIndex].hitboxes[i] == true)
                        bones.push_back(i);
                }
                if (bones.size() < 1)
                    continue;

                for (int bone : bones) {
                    if (BaseLogic(enemy.entity, weaponIndex, AimPoint, BestPoint, bone)) {
                        Target = enemy.entity_ptr;
                        break;
                    }
                }
            }
            
            else{
                if (BaseLogic(enemy.entity, weaponIndex, AimPoint, BestPoint, config->aimbot[weaponIndex].bone)) {
                    Target = enemy.entity_ptr;
                    break;
                }
            }                  

            //Resolver::UpdateTargeted();
        }






        for (int i = 0; i < Resolver::PlayerRecords.size(); i++) {
            Resolver::Record* record_b = &Resolver::PlayerRecords.at(i);
            if (!record_b || !record_b->invalid) {
                record_b->wasTargeted = false;
                record_b->FiredUpon = false;
            }
        }

        if (!(record->invalid) && config->debug.animstatedebug.resolver.enabled) {
            record->wasTargeted = true;
        }

        if (Target != nullptr) {
            if (Target == localPlayer.get())
                return;

            Vector Angle = Math::CalcAngle(localPlayer->getEyePosition(), BestPoint);
            static float MinimumVelocity = 0.0f;
            MinimumVelocity = localPlayer->getActiveWeapon()->getWeaponData()->maxSpeedAlt * 0.34f;



            if (localPlayer->velocity().length() >= MinimumVelocity && config->aimbot[weaponIndex].autoStop && (localPlayer->flags() & PlayerFlags::ONGROUND))
                Autostop(cmd); //Auto Stop

            if (activeWeapon->nextPrimaryAttack() <= memory->globalVars->serverTime() && activeWeapon->isSniperRifle() && !localPlayer->isScoped())
            {
                cmd->buttons |= UserCmd::IN_ATTACK2; //Auto Scope
            }

            Angle -= (localPlayer->aimPunchAngle() * interfaces->cvar->findVar("weapon_recoil_scale")->getFloat());
            bool hitchance = true;

            if (config->aimbot[weaponIndex].hitChance != 0.0f) {
                hitchance = HitChance(Angle, Target, activeWeapon, weaponIndex, cmd, (const int)config->aimbot[weaponIndex].hitChance);
            }
            else {
                hitchance = (config->aimbot[weaponIndex].maxAimInaccuracy >= activeWeapon->getInaccuracy());
            }

            if ( hitchance && activeWeapon->nextPrimaryAttack() <= memory->globalVars->serverTime())
            {
                cmd->viewangles = Angle; //Set Angles

                if (!config->aimbot[weaponIndex].silent)
                    interfaces->engine->setViewAngles(cmd->viewangles);

                cmd->buttons |= UserCmd::IN_ATTACK; //shoot

                if (config->debug.animstatedebug.resolver.enabled && !(record->invalid)) {
                    record->FiredUpon = true;
                }
            }




        }



}


















/*
                //std::ofstream myfile;
                //myfile.open("C:\\Users\\userWIN\\source\\repos\\test\\Debug\\aimbotlog.txt", std::ios_base::app);

                // myfile << "Get Model Function called\n";

const auto model = entity->getModel();
if (!model) {
    //myfile << "Get Model returned null model, exiting\n";
    return;
}
//myfile << "Calling getStudioModel\n";
const auto studioModel = interfaces->modelInfo->getStudioModel(model);
if (!studioModel) {
    //myfile << "getStudioModel retyrbed null model\n";
    return;
}

// StudioHitboxSet* getHitboxSet(int i)
// myfile << "Calling getHitboxSet\n";
const auto studiohitboxset = studioModel->getHitboxSet(0);
if (!studiohitboxset) {
    // myfile << "getHitboxSet returned null \n";
    return;
}

//if (!(matrix3x4 boneMatrices[256]; entity->setupBones(boneMatrices, 256, 256, 0.0f)))
//   return;

// Multipoint StudioHitBoxCode 
float maxDamage = 0;
float damage = 0;

for (auto bone : boneList) {

    //myfile << "bone = " << bone << "\n";
    auto studiobone = studiohitboxset->getHitbox(config->aimbot[weaponIndex].bone > 1 ? 10 - config->aimbot[weaponIndex].bone : bone);

    if (!studiobone) {
        //myfile << "No Studiobone returned from getHitBox\n";
        return;
    }

    //myfile << "studiobone->bone gives " << studiobone->bone;


    matrix3x4 boneMatrices[256];
    entity->setupBones(boneMatrices, 256, 256, 0.0f);

    auto basebonePosition = entity->getBonePosition(studiobone->bone);

    //myfile << "\nbasebonePosition = " << basebonePosition.x << " " << basebonePosition.y << " " << basebonePosition.z;



   // myfile << "\nstudio->bbMin " << studiobone->bbMin.x << " " << studiobone->bbMin.y <<  " " << studiobone->bbMin.z;
    //myfile << "\nstudio->bbMax " << studiobone->bbMax.x << " " << studiobone->bbMax.y << " " << studiobone->bbMax.z;

    Vector bbmin = studiobone->bbMin * config->aimbot[weaponIndex].multidistance;
    Vector bbmax = studiobone->bbMax * config->aimbot[weaponIndex].multidistance;

    Vector points[] = { Vector{bbmin.x, bbmin.y, bbmin.z},
         Vector{bbmin.x, bbmax.y, bbmin.z},
         Vector{bbmax.x, bbmax.y, bbmin.z},
         Vector{bbmax.x, bbmin.y, bbmin.z},
         Vector{bbmax.x, bbmax.y, bbmax.z},
         Vector{bbmin.x, bbmax.y, bbmax.z},
         Vector{bbmin.x, bbmin.y, bbmax.z},
         Vector{bbmax.x, bbmin.y, bbmax.z},
    };

    //auto bonePosition = entity->getBonePosition(config->aimbot[weaponIndex].bone > 1 ? 10 - config->aimbot[weaponIndex].bone : bone);
    Vector bonePositions[8];
    bonePositions[0] = basebonePosition;
    int i = 1;
    for (auto point : points) {
        bonePositions[i] = point.transform(boneMatrices[bone]);
        //myfile << "bonePosition: " << i << " is " << bonePositions[i].x << " " << bonePositions[i].y << " " << bonePositions->z << "\n";
        i++;
    }

    //Vector bonePositions[] = { (basebonePosition - (studiobone->bbMin * config->aimbot[weaponIndex].multidistance)), basebonePosition, (basebonePosition + (studiobone->bbMax * config->aimbot[weaponIndex].multidistance)) };





    for (auto bonePosition : bonePositions) {

        // myfile << "\nCurrent Bone Position in multipoint is: " << bonePosition.x << " " << bonePosition.y << " " << bonePosition.z << "\n";
        if (!entity->isVisible(bonePosition) && (config->aimbot[weaponIndex].visibleOnly || !canScan(entity, bonePosition, activeWeapon->getWeaponData(), config->aimbot[weaponIndex].killshot ? entity->health() : config->aimbot[weaponIndex].minDamage, config->aimbot[weaponIndex].friendlyFire, damage)))
            continue;

        auto angle = calculateRelativeAngle(localPlayerEyePosition, bonePosition, cmd->viewangles + aimPunch);
        auto fov = std::hypotf(angle.x, angle.y);
        if (fov < bestFov) {
            bestFov = fov;
            if ((damage > maxDamage)) {
                bestTarget = bonePosition;
                maxDamage = damage;

            }

            if ((damage > 100.0f) || (damage > entity->health())) {
                bestTarget = bonePosition;
                maxDamage = damage;
                break;
            }

        }
        // myfile << "Curr Damage is: " << damage << " best damage is: " << maxDamage << "\n";


    }

    //myfile << "Best target is " << bestTarget.x << " " << bestTarget.y << " " << bestTarget.z << "\n";
   // myfile.close();
    if (config->aimbot[weaponIndex].bone || (damage > 100.0f) || (damage > entity->health()))
        break;

    */

        /*
    for (float i = config->aimbot[weaponIndex].multidistance; i < 3; i += config->aimbot[weaponIndex].multidistance) {


        if (!entity->isVisible(bonePosition) && (config->aimbot[weaponIndex].visibleOnly || !canScan(entity, bonePosition, activeWeapon->getWeaponData(), config->aimbot[weaponIndex].killshot ? entity->health() : config->aimbot[weaponIndex].minDamage, config->aimbot[weaponIndex].friendlyFire, damage)))
            continue;

        auto angle = calculateRelativeAngle(localPlayerEyePosition, bonePosition, cmd->viewangles + aimPunch);
        auto fov = std::hypotf(angle.x, angle.y);
        if (fov < bestFov) {
            bestFov = fov;
            if (damage > maxDamage) {
                bestTarget = bonePosition;
                maxDamage = damage;
            }
        }
        if (config->aimbot[weaponIndex].bone)
            break;
        Vector adder;
        adder.x = i;
        adder.y = 0.0f;
        adder.z = 0.0f;
        bonePosition += adder;
    }
    */