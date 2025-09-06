// CtrlLib/Animation.cpp
//
// Implementation of Animation scheduler and state machine.
// See Animation.h header for high-level overview.
//
// 2025-08-19 — initial test pass / cleaned comments / cache helper
// 2025-08-21 — runtime Tick(n, max_ms_per_tick) manual driver (no #define)
// 2025-08-27 — yoyo fix, naming clarity, optimized scheduler
// 2025-08-27 — adjusted yoyo cycle count in Play, catch all exceptions

#include "Animation.h"

using namespace Upp;

/*==================== Configuration ====================*/
// Target frame pacing (test or app can override via SetFPS)
static int g_step_ms = 1000 / 60; // Milliseconds per frame
static int g_fps = 60;           // Target frames per second

/*==================== Scheduler ====================*/
namespace {

struct Scheduler {
    // Singleton accessor
    static Scheduler& Inst() { static Scheduler* s = new Scheduler; return *s; }

    // State
    Vector<Animation::State*> active;  // Owns State* pointers
    TimeCallback ticker;              // Timer for frame updates
    bool running = false;             // Scheduler active state
    int timer_id = 0;                // Timer identifier for validation
    int64 manual_last_now = 0;       // Monotonic time for manual ticking

    // Destroying a state if present
    void DeleteState(Animation::State* s) { if (s) delete s; }

    // Finalizing scheduler: stop and purge all animations
	void Finalize() {
	    running = false;
	    ++timer_id;
	    ticker.Kill();
	    for (Animation::State* s : active) DeleteState(s);
	    active.Clear();
	    manual_last_now = 0;   // <— add this
	}

    // Starting timer loop
    void Start() {
        if (running) return;
        running = true;
        int current_id = ++timer_id;
        ticker.Set(g_step_ms, callback1(this, &Scheduler::TickTimer, current_id));
    }

    // Stopping timer loop
    void Stop() {
        if (!running) return;
        running = false;
        ++timer_id;    // Invalidate queued ticks
        ticker.Kill();
    }

    // Adding a new active state and ensuring scheduling
    void Add(Animation::State* s) {
        active.Add(s);
        Start();
    }

    // Removing a state by pointer and stopping if idle
    void Remove(Animation::State* st) {
        for (int i = 0; i < active.GetCount(); ++i) {
            if (active[i] == st) {
                DeleteState(active[i]);
                active.Remove(i);
                break;
            }
        }
        if (active.IsEmpty())
            Stop();
    }

    // Killing all animations for a given Ctrl or dead owners
    void KillFor(Ctrl* c) {
        for (int i = active.GetCount() - 1; i >= 0; --i) {
            Animation::State* s = active[i];
            if (!s->owner || s->owner == c) {
                DeleteState(s);
                active.Remove(i);
            }
        }
        if (active.IsEmpty())
            Stop();
    }

    // Processing all active animations for the current frame
    void RunFrame(int64 now) {
        Vector<int> to_remove; // Collect indices for removal

        // Iterating over active animations
        for (int i = 0; i < active.GetCount(); ++i) {
            Animation::State* s = active[i];
            bool cont = false;
            if (s) {
                try {
                    cont = s->Step(now);
                } catch (...) {
                    // Logging any exception (replace with your logging mechanism)
                    Cerr() << "Exception in Animation::State::Step\n";
                    cont = false;
                }
            }

            if (!cont) {
                // Updating progress cache before removal
                if (s && s->anim) {
                    s->anim->_SetProgressCache(s->finished_flag ? 1.0 : s->last_progress);
                    s->anim = nullptr; // Prevent use-after-free
                }
                to_remove.Add(i);
            }
        }

        // Removing completed animations in reverse order
        for (int i = to_remove.GetCount() - 1; i >= 0; --i) {
            DeleteState(active[to_remove[i]]);
            active.Remove(to_remove[i]);
        }

        // Stopping scheduler if no animations remain
        if (active.IsEmpty())
            Stop();
    }

    // Handling timer-driven frame updates
    void TickTimer(int current_id) {
        if (current_id != timer_id || !running) return;
        RunFrame(msecs());
        if (!active.IsEmpty()) {
            ticker.Set(g_step_ms, callback1(this, &Scheduler::TickTimer, current_id));
        }
    }

    // Processing a single manual tick
    void TickManualOnce(int max_ms_per_tick) {
        int64 wall_now = msecs();
        if (manual_last_now == 0)
            manual_last_now = wall_now;

        int64 dt = wall_now - manual_last_now;
        if (max_ms_per_tick > 0 && dt > max_ms_per_tick)
            dt = max_ms_per_tick;
        if (dt < 0) dt = 0; // Guard against clock skew

        manual_last_now += dt;
        RunFrame(manual_last_now);
    }
};

} // namespace

/*==================== Animation::State::Step ====================*/
// Advance time, compute eased value, run callbacks, and manage loop/yoyo bookkeeping
bool Animation::State::Step(int64 now)
{
    // Checking ownership and pause state
    if (!owner) return false;  // Owner died -> stop
    if (paused) return true;   // Keep scheduled but do not advance

    // Computing elapsed time since leg start
    const int64 local = now - start_ms + elapsed_ms;
    if (local < spec.delay_ms)
        return true;  // Still in delay window

    // Calculating raw progress of current leg (0 to 1)
    const int dur = max(1, spec.duration_ms);
    double leg_progress = double(local - spec.delay_ms) / dur;

    leg_progress = clamp(leg_progress, 0.0, 1.0);

    // Adjusting progress for yoyo direction (forward: 0->1, reverse: 1->0)
    double t = reverse ? (1.0 - leg_progress) : leg_progress;

    // Caching forward progress for cancellation
    last_progress = leg_progress;

    // Applying easing to adjusted progress
    const double e = spec.easing ? spec.easing(t) : t;

    // Running update and tick callbacks
    if (spec.on_update) spec.on_update(e);
    if (spec.tick && !spec.tick(e))
        return false; // User requested stop

    // Checking leg completion
    if (leg_progress >= 1.0) {
        if (spec.yoyo) {
            // Flipping direction for yoyo
            reverse = !reverse;

            // Counting cycle on return to forward
            if (!reverse) {
                if (spec.loop_count >= 0 && --cycles <= 0) {
                    if (spec.on_finish) spec.on_finish();
                    finished_flag = true;
                    return false;
                }
            }
            // Restarting timing for next leg
            start_ms = now;
            elapsed_ms = 0;
        } else {
            // Handling linear loop
            if (spec.loop_count >= 0 && --cycles <= 0) {
                if (spec.on_finish) spec.on_finish();
                finished_flag = true;
                return false;
            }
            // Restarting timing for next loop
            start_ms = now;
            elapsed_ms = 0;
        }
    }
    return true;
}

/*==================== Animation Implementation ====================*/
// Initializing animation with owner Ctrl
Animation::Animation(Ctrl& owner)
    : owner_(&owner)
{
    spec_box_.Create();
    spec_ = ~spec_box_;
}

// Detaching from live state safely
Animation::~Animation()
{
    if (live_)
        live_->anim = nullptr; // Prevent scheduler write-back
    live_ = nullptr;
}

#define RET(e) do { e; return *this; } while (0)

/*---------------- Fluent Configuration ----------------*/
Animation& Animation::Duration(int ms) { RET(spec_->duration_ms = ms); }
Animation& Animation::Ease(const Easing::Fn& fn) { RET(spec_->easing = fn); }
Animation& Animation::Ease(Easing::Fn&& fn) { RET(spec_->easing = pick(fn)); }
Animation& Animation::Loop(int n) { RET(spec_->loop_count = n); }
Animation& Animation::Yoyo(bool b) { RET(spec_->yoyo = b); }
Animation& Animation::Delay(int ms) { RET(spec_->delay_ms = ms); }
Animation& Animation::OnStart(const Callback& cb) { RET(spec_->on_start = cb); }
Animation& Animation::OnStart(Callback&& cb) { RET(spec_->on_start = pick(cb)); }
Animation& Animation::OnFinish(const Callback& cb) { RET(spec_->on_finish = cb); }
Animation& Animation::OnFinish(Callback&& cb) { RET(spec_->on_finish = pick(cb)); }
Animation& Animation::OnCancel(const Callback& cb) { RET(spec_->on_cancel = cb); }
Animation& Animation::OnCancel(Callback&& cb) { RET(spec_->on_cancel = pick(cb)); }
Animation& Animation::OnUpdate(const Callback1<double>& c) { RET(spec_->on_update = c); }
Animation& Animation::OnUpdate(Callback1<double>&& c) { RET(spec_->on_update = pick(c)); }
Animation& Animation::operator()(const Function<bool(double)>& f) { RET(spec_->tick = f); }
Animation& Animation::operator()(Function<bool(double)>&& f) { RET(spec_->tick = pick(f)); }

#undef RET

/*---------------- Control Methods ----------------*/
// Starting animation with staged spec
void Animation::Play()
{
    if (!spec_) return;
    live_ = new State;
    live_->anim = this;
    live_->owner = owner_;
    live_->spec = pick(*spec_);
    spec_ = nullptr;

    _SetProgressCache(0.0); // Reset on (re)start
    live_->start_ms = msecs();
    live_->cycles = (live_->spec.loop_count < 0)
                  ? INT_MAX
                  : (live_->spec.yoyo ? (live_->spec.loop_count + 1) / 2
                                      : live_->spec.loop_count);
    if (live_->spec.on_start) live_->spec.on_start();
    Scheduler::Inst().Add(live_);
}

// Pausing time accumulation
void Animation::Pause()
{
    if (live_ && !live_->paused) {
        live_->elapsed_ms += msecs() - live_->start_ms;
        live_->paused = true;
    }
}

// Resuming time accumulation
void Animation::Resume()
{
    if (live_ && live_->paused) {
        live_->start_ms = msecs();
        live_->paused = false;
    }
}

// Completing animation immediately
void Animation::Stop()
{
    if (!live_) return;
    if (live_->spec.tick) live_->spec.tick(live_->reverse ? 0.0 : 1.0);
    if (live_->spec.on_finish) live_->spec.on_finish();
    _SetProgressCache(1.0);
    Cancel(false); // Do not fire on_cancel
}

// Aborting animation with optional cancel callback
void Animation::Cancel(bool fire_cancel)
{
    if (!live_) return;
    if (fire_cancel && live_->spec.on_cancel)
        live_->spec.on_cancel();

    // Preserving last computed progress
    _SetProgressCache(live_->last_progress);

    // Preventing scheduler write-back
    live_->anim = nullptr;
    Scheduler::Inst().Remove(live_);
    live_ = nullptr;
}

// Checking if animation is playing
bool Animation::IsPlaying() const
{
    return live_ && !live_->paused;
}

// Checking if animation is paused
bool Animation::IsPaused() const
{
    return live_ && live_->paused;
}

// Computing current progress [0..1]
double Animation::Progress() const
{
    if (!live_) return progress_cache_;
    int64 run = live_->elapsed_ms + (live_->paused ? 0 : (msecs() - live_->start_ms));
    run = max<int64>(0, run - live_->spec.delay_ms);
    return clamp(double(run) / max(1, live_->spec.duration_ms), 0.0, 1.0);
}

/*---------------- Manual Ticking ----------------*/
// Advancing scheduler by n frames with optional time clamp
void Animation::Tick(int n, int max_ms_per_tick)
{
    if (n <= 0) return;
    Scheduler& s = Scheduler::Inst();
    for (int i = 0; i < n; ++i)
        s.TickManualOnce(max_ms_per_tick);
}

/*---------------- FPS Control ----------------*/
// Setting target FPS and updating step time
void Animation::SetFPS(int fps)
{
    g_fps = clamp(fps, 1, 240);
    g_step_ms = max(1, 1000 / g_fps);
}

// Getting current target FPS
int Animation::GetFPS()
{
    return g_fps;
}

/*---------------- Global Helpers ----------------*/
// Terminating all animations for a Ctrl
void Animation::KillAllFor(Ctrl& c)
{
    Scheduler::Inst().KillFor(&c);
}

// Finalizing scheduler and all animations
void Animation::Finalize()
{
    Scheduler::Inst().Finalize();
}