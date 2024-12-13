#include "TimerFix.h"


#include "ll/api/memory/Hook.h"
#include "ll/api/mod/RegisterHelper.h"

#include "mc/util/Timer.h"

namespace timer_fix {

static std::unique_ptr<TimerFix> instance;

TimerFix& TimerFix::getInstance() { return *instance; }

bool TimerFix::load() {
    getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

bool TimerFix::enable() {
    getSelf().getLogger().debug("Enabling...");
    // Code for enabling the mod goes here.
    return true;
}

bool TimerFix::disable() {
    getSelf().getLogger().debug("Disabling...");
    // Code for disabling the mod goes here.
    return true;
}

float inlineClamp(float v, float low, float high) {
    if (v > high) return high;
    if (v <= low) return low;
    else return v;
}

class FixedTimer : public Timer {
public:
    /*
        `int64 getMillisecondsSinceLaunch()` is the default value of `getTimeMSCallback`, but not provided by ll's api.
        So we have to resolve it by ourselves.
    */
    FixedTimer(
        float                  ticksPerSecond,
        std::function<int64()> getTimeMSCallback =
            static_cast<int64 (*)()>(ll::memory::resolveIdentifier<int64*()>("?getMillisecondsSinceLaunch@@YA_JXZ"))
    )
    : Timer(ticksPerSecond, std::forward<std::function<int64()>>(getTimeMSCallback)) {}

    // add a new field to store double precision time
    double mLastTimeSeconds_fixed = this->mLastMs * 0.001;
};

LL_AUTO_STATIC_HOOK(
    TimerCtorHook,
    ll::memory::HookPriority::Highest,
    "??$make_unique@VTimer@@AEBH$0A@@std@@YA?AV?$unique_ptr@VTimer@@U?$default_delete@VTimer@@@std@@@0@AEBH@Z",
    std::unique_ptr<Timer>,
    const int& ticksPerSecond
) {
    // Hook make_unique<Timer> and forward to make_unique<FixedTimer>, hope this will work and not crash the game
    return std::make_unique<FixedTimer>(std::forward<const int>(ticksPerSecond));
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    TimerUpdateHook,
    ll::memory::HookPriority::Highest,
    FixedTimer,
    &Timer::advanceTime,
    void,
    float preferredFrameStep
) {
    if (/* this->stepping() */ this->mSteppingTick >= 0) {
        if (this->mSteppingTick) {
            this->mTicks = 1;
            --this->mSteppingTick;
        } else {
            this->mTicks = 0;
            this->mAlpha = 0.0f;
        }
    } else {
        int64 nowMs    = this->mGetTimeMSCallback();
        int64 passedMs = nowMs - this->mLastMs;
        if (passedMs > 1000) {
            int64 passedMsSysTime = nowMs - this->mLastMsSysTime;
            if (passedMsSysTime == 0) {
                passedMsSysTime = 1;
                passedMs        = 1;
            }
            double adjustTimeT    = (double)passedMs / (double)passedMsSysTime;
            this->mAdjustTime    += (adjustTimeT - this->mAdjustTime) * 0.2;
            this->mLastMs         = nowMs;
            this->mLastMsSysTime  = nowMs;
        }
        if (passedMs < 0) {
            this->mLastMs        = nowMs;
            this->mLastMsSysTime = nowMs;
        }
        double passedSeconds =
            (nowMs * 0.001 - this->mLastTimeSeconds_fixed) * this->mAdjustTime; // the key modification
        this->mLastTimeSeconds = this->mLastTimeSeconds_fixed = nowMs * 0.001;
        if (preferredFrameStep > 0.0f) {
            float newFrameStepAlignmentRemainder = inlineClamp(
                this->mFrameStepAlignmentRemainder + preferredFrameStep - passedSeconds,
                0.0f,
                4.0f * preferredFrameStep
            );
            passedSeconds                      -= this->mFrameStepAlignmentRemainder - newFrameStepAlignmentRemainder;
            this->mFrameStepAlignmentRemainder  = newFrameStepAlignmentRemainder;
        }
        if (passedSeconds < 0.0) passedSeconds = 0.0;
        if (passedSeconds > 0.1) passedSeconds = 0.1;
        this->mLastTimestep  = passedSeconds;
        this->mPassedTime   += passedSeconds * this->mTimeScale * this->mTicksPerSecond;
        this->mTicks         = this->mPassedTime;
        this->mPassedTime   -= this->mTicks;
        if (this->mTicks > 10) this->mTicks = 10;
        this->mAlpha = this->mPassedTime;
    }
}
} // namespace timer_fix

LL_REGISTER_MOD(timer_fix::TimerFix, timer_fix::instance);
