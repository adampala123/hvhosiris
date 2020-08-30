#pragma once

struct UserCmd;
struct Vector;

namespace AntiAim {
    //void GetMovementFix(unsigned int state, float oForwardMove, float oSideMove, UserCmd *cmd);
    void CorrectMovement(float OldAngleY, UserCmd* pCmd, float fOldForward, float fOldSidemove);
    bool LBY_UPDATE();
    float DEG2RAD(float Degress);
    void run(UserCmd*, const Vector&, const Vector&, bool&) noexcept;
    void fakeWalk(UserCmd* cmd, bool& sendPacket, const Vector& currentViewAngles) noexcept;
}


