// CtrlLib/Animation.cpp
//
// Implementation of Animation scheduler and state machine.
// See Animation.h header for high-level overview.
//
// 2025-08-19 — initial test pass / cleaned comments / cache helper
// 2025-08-21 — runtime Tick(n, max_ms_per_tick) manual driver (no #define)

#include <CtrlLib/Animation.h>

using namespace Upp;

/*==================== configuration ====================*/
// Target frame pacing (test or app can override via SetFPS)
static int g_step_ms = 1000 / 60;
static int g_fps     = 60;

/*==================== Scheduler ====================*/
namespace {

struct Scheduler {
	// Singleton accessor
	static Scheduler& Inst() { static Scheduler* s = new Scheduler; return *s; }

	// State
	Vector<Animation::State*> active;  // we own State*
	TimeCallback ticker;
	bool running   = false;
	int  arm_id    = 0;

	// Manual tick monotonic time (for Animation::Tick)
	int64 manual_last_now = 0;

	// Destroy a state if present
	void DeleteState(Animation::State* s) { if(s) delete s; }

	// Stop and purge everything,but allow future restarts
    void Finalize() {
        running = false;
        ++arm_id;      // invalidate queued ticks
        ticker.Kill(); // stop timer
        for (Animation::State* s : active) DeleteState(s);
        active.Clear();
    }

	// Start timer loop
	void Start() {
		if (running) return;
		running = true;
		int my = ++arm_id;
		ticker.Set(g_step_ms, callback1(this, &Scheduler::TickTimer, my));
	}

	// Stop timer loop
	void Stop() {
		if(!running) return;
		running = false;
		++arm_id;     // invalidate already-queued tick
		ticker.Kill();
	}

	// Add a new active state and ensure scheduling
	void Add(Animation::State* s) {
		active.Add(s);
		Start();
	}

	// Remove a state (by pointer) and stop if idle
	void Remove(Animation::State* st) {
		for(int i=0;i<active.GetCount();++i)
			if(active[i] == st) {
				DeleteState(active[i]);
				active.Remove(i);
				break;
			}
		if(active.IsEmpty())
			Stop();
	}

	// Kill all animations for a given Ctrl (or dead owners)
	void KillFor(Ctrl* c) {
		for(int i=active.GetCount()-1;i>=0;--i){
			Animation::State* s = active[i];
			if(!s->owner || s->owner == c){
				DeleteState(s);
				active.Remove(i);
			}
		}
		if(active.IsEmpty())
			Stop();
	}

	// ---- Core frame runner (shared by timer/manual) ----
	void RunFrame(int64 now) {
	//	if(finalized) return;  **remove ??

		for(int i=0;i<active.GetCount();){
			Animation::State* s = active[i];
			bool cont = false;
			if(s) {
				try { cont = s->Step(now); }
				catch(...) { cont = false; } // safety: kill on exception
			}

			if(!cont){
				// Before deleting, persist user-visible progress on the owner Animation
				if(s && s->anim) {
					// natural finish -> 1.0, otherwise the last forward progress
					s->anim->_SetProgressCache(s->finished_flag ? 1.0 : s->last_progress);
					// avoid use-after-free paths if Animation is destroyed during removal
					s->anim = nullptr;
				}
				DeleteState(s);
				active.Remove(i);
			} else {
				++i;
			}
		}

		if(active.IsEmpty())
			Stop();
	}

// inside Scheduler::TickTimer(int my)
void TickTimer(int my) {
    if(my != arm_id || !running) return;
    RunFrame(msecs());
    if(!active.IsEmpty())
        ticker.Set(g_step_ms, callback1(this, &Scheduler::TickTimer, my));
}

	// ---- Manual-driven path for Animation::Tick ----
	void TickManualOnce(int max_ms_per_tick) {
		int64 wall_now = msecs();
		if (manual_last_now == 0)
			manual_last_now = wall_now;

		int64 dt = wall_now - manual_last_now;
		if (max_ms_per_tick > 0 && dt > max_ms_per_tick)
			dt = max_ms_per_tick;
		if (dt < 0) dt = 0; // guard against clock skew

		manual_last_now += dt;
		RunFrame(manual_last_now);
	}
};

} // namespace

/*==================== State::Step (out-of-line) ====================*/
// Advance time, compute eased value, run callbacks, and loop/yoyo bookkeeping.
bool Animation::State::Step(int64 now)
{
    if(!owner)  return false;       // owner died -> stop
    if(paused)   return true;       // keep scheduled but do not advance

    const int64 local = now - start_ms + elapsed_ms;
    if(local < spec.delay_ms)
        return true;                // still in delay window

    double t = double(local - spec.delay_ms) / spec.duration_ms;
    t = clamp(t, 0.0, 1.0);
    double t_raw = t;               // forward progress before direction flip

    if(reverse)
        t = 1.0 - t;

    last_progress = reverse ? (1.0 - t) : t;

    const double e = spec.easing ? spec.easing(t) : t;

    if(spec.on_update) spec.on_update(e);
    if(spec.tick && !spec.tick(e))
        return false; // user requested stop

    if(t_raw >= 1.0) {
        if(spec.yoyo) {
            reverse = !reverse;

            // only decrement after completing backward leg
            if(!reverse) {
                if(spec.loop_count >= 0 && --cycles <= 0) {
                    if(spec.on_finish) spec.on_finish();
                    finished_flag = true;
                    return false;
                }
            }
            start_ms = now;
            elapsed_ms = 0;
        } else {
            // linear loop
            if(spec.loop_count >= 0 && --cycles <= 0) {
                if(spec.on_finish) spec.on_finish();
                finished_flag = true;
                return false;
            }
            start_ms = now;
            elapsed_ms = 0;
        }
    }
    return true;
}


/*==================== Animation impl ====================*/
// Bind to owner and create staging box.
Animation::Animation(Ctrl& owner)
	: owner_(&owner)
{
	spec_box_.Create();
	spec_ = ~spec_box_;
}

// Detach from live state safely; scheduler owns deletion.
Animation::~Animation()
{
	if(live_)
		live_->anim = nullptr; // guard: scheduler must not write back to us
	live_ = nullptr;
}

#define RET(e) do{ e; return *this; }while(0)

/*---------------- Fluent configuration ----------------*/
Animation& Animation::Duration(int ms)               { RET(spec_->duration_ms = ms); }
Animation& Animation::Ease(const Easing::Fn& fn)     { RET(spec_->easing = fn); }
Animation& Animation::Ease(Easing::Fn&& fn)          { RET(spec_->easing = pick(fn)); }
Animation& Animation::Loop(int n)                    { RET(spec_->loop_count = n); }
Animation& Animation::Yoyo(bool b)                   { RET(spec_->yoyo = b); }
Animation& Animation::Delay(int ms)                  { RET(spec_->delay_ms = ms); }
Animation& Animation::OnStart(const Callback& cb)    { RET(spec_->on_start = cb); }
Animation& Animation::OnStart(Callback&& cb)         { RET(spec_->on_start = pick(cb)); }
Animation& Animation::OnFinish(const Callback& cb)   { RET(spec_->on_finish = cb); }
Animation& Animation::OnFinish(Callback&& cb)        { RET(spec_->on_finish = pick(cb)); }
Animation& Animation::OnCancel(const Callback& cb)   { RET(spec_->on_cancel = cb); }
Animation& Animation::OnCancel(Callback&& cb)        { RET(spec_->on_cancel = pick(cb)); }
Animation& Animation::OnUpdate(const Callback1<double>& c) { RET(spec_->on_update = c); }
Animation& Animation::OnUpdate(Callback1<double>&& c)      { RET(spec_->on_update = pick(c)); }
Animation& Animation::operator()(const Function<bool(double)>& f) { spec_->tick = f; return *this; }
Animation& Animation::operator()(Function<bool(double)>&& f)      { spec_->tick = pick(f); return *this; }

#undef RET

/*---------------- Control ----------------*/
// Commit staged spec and schedule state.
void Animation::Play() {
	if(!spec_) return;
	live_ = new State;
	live_->anim   = this;
	live_->owner  = owner_;
	live_->spec   = pick(*spec_);
	spec_ = nullptr;
	
	// DEBUG: confirm what was actually staged
Cout() << "Play(): yoyo=" << (live_->spec.yoyo ? 1 : 0)
       << " loop_count=" << live_->spec.loop_count
       << " duration=" << live_->spec.duration_ms << '\n';

	_SetProgressCache(0.0);             // reset on (re)start
	live_->start_ms = msecs();
	live_->cycles   = (live_->spec.loop_count < 0) ? INT_MAX : live_->spec.loop_count;
	if(live_->spec.on_start) live_->spec.on_start();
	Scheduler::Inst().Add(live_);
}

// Pause time accumulation.
void Animation::Pause() {
	if(live_ && !live_->paused) {
		live_->elapsed_ms += msecs() - live_->start_ms;
		live_->paused = true;
	}
}

// Resume time accumulation.
void Animation::Resume() {
	if(live_ && live_->paused) {
		live_->start_ms = msecs();
		live_->paused = false;
	}
}

// Complete immediately, fire finish, and keep progress at 1.0 (no cancel).
void Animation::Stop() {
	if(!live_) return;
	if(live_->spec.tick)      live_->spec.tick(live_->reverse ? 0.0 : 1.0);
	if(live_->spec.on_finish) live_->spec.on_finish();
	_SetProgressCache(1.0);
	Cancel(/*fire_cancel=*/false); // do NOT fire on_cancel
}

// Abort; optionally fire cancel; keep last forward progress.
void Animation::Cancel(bool fire_cancel) {
	if(!live_) return;
	if(fire_cancel && live_->spec.on_cancel)
		live_->spec.on_cancel();

	// keep whatever the last Step() computed
	_SetProgressCache(live_->last_progress);

	// prevent scheduler from writing back into us after we drop
	live_->anim = nullptr;

	Scheduler::Inst().Remove(live_); // scheduler deletes the State
	live_ = nullptr;
}

// True if scheduled and not paused.
bool   Animation::IsPlaying() const { return live_ && !live_->paused; }
// True if scheduled and paused.
bool   Animation::IsPaused()  const { return live_ &&  live_->paused; }

// Progress in [0..1]; uses cache when not live.
double Animation::Progress() const {
	if(!live_) return progress_cache_;
	int64 run = live_->elapsed_ms + (live_->paused ? 0 : (msecs() - live_->start_ms));
	run = max<int64>(0, run - live_->spec.delay_ms);
	return clamp(double(run) / max(1, live_->spec.duration_ms), 0.0, 1.0);
}

/*---------------- Manual ticking (runtime) ----------------*/
// Advance the scheduler by 'n' frames. Each frame advances time by the
// real elapsed wall time since the last manual tick, optionally clamped
// to 'max_ms_per_tick' (0 = no clamp).
void Animation::Tick(int n, int max_ms_per_tick) {
	if (n <= 0) return;
	Scheduler& S = Scheduler::Inst();
	for (int i = 0; i < n; ++i)
		S.TickManualOnce(max_ms_per_tick);
}

/*---------------- FPS control ----------------*/
// Set target FPS; clamps to [1..240]; derived step in ms.
void Animation::SetFPS(int fps) {
	g_fps = clamp(fps, 1, 240);
	g_step_ms = max(1, 1000 / g_fps);
}

// Return current target FPS.
int Animation::GetFPS() {
	return g_fps;
}

/*---------------- Globals ----------------*/
void Animation::KillAllFor(Ctrl& c) { Scheduler::Inst().KillFor(&c); }
void Animation::Finalize()          { Scheduler::Inst().Finalize(); }
