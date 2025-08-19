#include <CtrlLib/Animation.h>

using namespace Upp;

/*==================== configuration ====================*/
static int g_step_ms = 1000 / 60;
static int g_fps = 60;

/*==================== Scheduler ====================*/
namespace {

struct Scheduler {
	static Scheduler& Inst()
	{
		static Scheduler* s = new Scheduler;
		return *s;
	}

	Vector<Animation::State*> active; // we own State*
	TimeCallback ticker;

	bool running = false;
	bool finalized = false;
	int arm_id = 0;

	void DeleteState(Animation::State* s)
	{
		if(s)
			delete s;
	}

	void Finalize()
	{
		finalized = true;
		running = false;
		++arm_id;      // invalidate queued ticks
		ticker.Kill(); // stop timer
		for(Animation::State* s : active)
			DeleteState(s);
		active.Clear();
	}

	void Start()
	{
#if ANIM_MANUAL_DRIVE
		// no timer – manual TickOnce() drives the loop
#else
		if(finalized || running)
			return;
		running = true;
		int my = ++arm_id;
		ticker.Set(g_step_ms, callback1(this, &Scheduler::Tick, my));
#endif
	}

	void Stop()
	{
#if !ANIM_MANUAL_DRIVE
		if(!running)
			return;
		running = false;
		++arm_id; // invalidate any already-queued tick
		ticker.Kill();
#endif
	}

	void Add(Animation::State* s)
	{
		active.Add(s);
		Start();
	}

	void Remove(Animation::State* st)
	{
		for(int i = 0; i < active.GetCount(); ++i)
			if(active[i] == st) {
				DeleteState(active[i]);
				active.Remove(i);
				break;
			}
		if(active.IsEmpty())
			Stop();
	}

	void KillFor(Ctrl* c)
	{
		for(int i = active.GetCount() - 1; i >= 0; --i) {
			Animation::State* s = active[i];
			if(!s->owner || s->owner == c) {
				DeleteState(s);
				active.Remove(i);
			}
		}
		if(active.IsEmpty())
			Stop();
	}

#if ANIM_MANUAL_DRIVE
	void TickOnce() { Tick(-1); } // no arm check in manual mode
#endif

	void Tick(int my)
	{
		if(finalized)
			return;
#if !ANIM_MANUAL_DRIVE
		if(my != arm_id || !running)
			return; // late/invalid tick
#endif
		int64 now = msecs();

		for(int i = 0; i < active.GetCount();) {
			Animation::State* s = active[i];
			bool cont = false;
			if(s) {
				try {
					cont = s->Step(now);
				}
				catch(...) {
					cont = false;
				}
			}
			if(!cont) {
				if(s && s->anim) {
					s->anim->progress_cache_ = s->finished_flag ? 1.0 : s->last_progress;
				}
				DeleteState(s);
				active.Remove(i);
			}
			else {
				++i;
			}
		}

		if(!active.IsEmpty()) {
#if !ANIM_MANUAL_DRIVE
			// re-arm with the SAME id
			ticker.Set(g_step_ms, callback1(this, &Scheduler::Tick, my));
#endif
		}
		else {
			Stop();
		}
	}
};

} // namespace

/*==================== State::Step (out-of-line) ====================*/
bool Animation::State::Step(int64 now)
{
	if(!owner)
		return false;
	if(paused)
		return true;

	const int64 local = now - start_ms + elapsed_ms;
	if(local < spec.delay_ms)
		return true;

	double t = double(local - spec.delay_ms) / spec.duration_ms;
	t = clamp(t, 0.0, 1.0);
	if(reverse)
		t = 1.0 - t;

	// store raw forward progress (0..1) for cancel cache if ever needed
	last_progress = reverse ? (1.0 - t) : t;

	const double e = spec.easing ? spec.easing(t) : t;

	if(spec.on_update)
		spec.on_update(e);
	if(spec.tick && !spec.tick(e))
		return false;

	if(t >= 1.0) {
		if(spec.yoyo) {
			// flip direction
			reverse = !reverse;

			// count a full cycle only when we come back to forward
			if(!reverse) {
				if(spec.loop_count >= 0 && --cycles <= 0) {
					if(spec.on_finish)
						spec.on_finish();
					finished_flag = true;
					return false;
				}
			}
			// restart timing for next leg
			start_ms = now;
			elapsed_ms = 0;
		}
		else {
			// linear loop
			if(spec.loop_count >= 0 && --cycles <= 0) {
				if(spec.on_finish)
					spec.on_finish();
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
Animation::Animation(Ctrl& owner)
	: owner_(&owner)
{
	spec_box_.Create();
	spec_ = ~spec_box_;
}

Animation::~Animation()
{
    // If our State is still alive in the scheduler, make its back-pointer safe
    if(live_)
        live_->anim = nullptr;
    // scheduler owns/destroys State; just drop our handle
    live_ = nullptr;
}

#define RET(e)                                                                                 \
	do {                                                                                       \
		e;                                                                                     \
		return *this;                                                                          \
	} while(0)

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
Animation& Animation::operator()(const Function<bool(double)>& f)
{
	spec_->tick = f;
	return *this;
}
Animation& Animation::operator()(Function<bool(double)>&& f)
{
	spec_->tick = pick(f);
	return *this;
}

#undef RET

void Animation::Play()
{
	if(!spec_)
		return;
	live_ = new State;
	live_->anim = this;
	live_->owner = owner_;
	live_->spec = pick(*spec_);
	spec_ = nullptr;

	progress_cache_ = 0.0;

	live_->start_ms = msecs();
	live_->cycles = (live_->spec.loop_count < 0) ? INT_MAX : live_->spec.loop_count;
	if(live_->spec.on_start)
		live_->spec.on_start();
	Scheduler::Inst().Add(live_);
}

void Animation::Pause()
{
	if(live_ && !live_->paused) {
		live_->elapsed_ms += msecs() - live_->start_ms;
		live_->paused = true;
	}
}

void Animation::Resume()
{
	if(live_ && live_->paused) {
		live_->start_ms = msecs();
		live_->paused = false;
	}
}

void Animation::Stop()
{
	if(!live_)
		return;
	if(live_->spec.tick)
		live_->spec.tick(live_->reverse ? 0.0 : 1.0);
	if(live_->spec.on_finish)
		live_->spec.on_finish();
	progress_cache_ = 1.0;         // stopping -> completed
	//Defensive nulling in explicit teardown paths
	live_->anim = nullptr;
	Cancel(/*fire_cancel=*/false); // do NOT fire on_cancel
}

void Animation::Cancel(bool fire_cancel)
{
	if(!live_)
		return;
	if(fire_cancel && live_->spec.on_cancel)
		live_->spec.on_cancel();
	// keep whatever the last Step() computed
    progress_cache_ = live_->last_progress;
    //Defensive nulling in explicit teardown paths
     live_->anim = nullptr;
	Scheduler::Inst().Remove(live_); // scheduler deletes the State
	live_ = nullptr;
}

bool Animation::IsPlaying() const { return live_ && !live_->paused; }
bool Animation::IsPaused() const { return live_ && live_->paused; }

double Animation::Progress() const
{
	if(!live_)
		return progress_cache_;
	int64 run = live_->elapsed_ms + (live_->paused ? 0 : (msecs() - live_->start_ms));
	run = max<int64>(0, run - live_->spec.delay_ms);
	return clamp(double(run) / max(1, live_->spec.duration_ms), 0.0, 1.0);
}

/* fps */
void Animation::SetFPS(int fps)
{
	g_fps = clamp(fps, 1, 240);
	g_step_ms = max(1, 1000 / g_fps);
}

int Animation::GetFPS() { return g_fps; }

/* global */
void Animation::KillAllFor(Ctrl& c) { Scheduler::Inst().KillFor(&c); }
void Animation::Finalize() { Scheduler::Inst().Finalize(); }

#if ANIM_MANUAL_DRIVE
void Animation::TickOnce() { Scheduler::Inst().TickOnce(); }
#endif
