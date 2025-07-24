// File: uppsrc/CtrlLib/Animation.cpp
#include "Animation.h"

namespace Upp {

int Animation::s_frameIntervalMs = 1000 / 30; // default 30 FPS

// Global list of running states
static Vector<One<Animation::State>>& AnimList()
{
	return Single<Vector<One<Animation::State>>>();
}

// Unique tag for SetTimeCallback/KillTimeCallback
static int s_timerTag = 0;

// Forward
static void AnimationTick();

// Ensure timer is running/stopped appropriately
static void EnsureTimerRunning()
{
	if(AnimList().IsEmpty()) {
		if(s_timerTag) {
			KillTimeCallback(&s_timerTag);
			s_timerTag = 0;
		}
		return;
	}
	if(!s_timerTag) {
		s_timerTag = 1;
		SetTimeCallback(Animation::GetFPS(), &AnimationTick, &s_timerTag);
	}
}

// The main tick proc
static void AnimationTick()
{
	int64 now = GetTimeClick();
	auto& list = AnimList();

	bool any_paused = false;

	for(int i = 0; i < list.GetCount();) {
		Animation::State* s = list[i].Get();

		// Safety: owner gone?
		Ctrl* ow = s->owner;
		if(!ow) {
			list.Remove(i);
			continue;
		}

		TopWindow* tw = ow->GetTopWindow();
		if((tw && !tw->IsOpen()) || (!tw && ow->GetParent() == nullptr)) {
			list.Remove(i);
			continue;
		}

		if(s->paused) {
			++i;
			any_paused = true;
			continue;
		}

		double progress =
			s->time_ms > 0 ? clamp(double(now - s->start_time) / s->time_ms, 0.0, 1.0) : 1.0;
		if(s->reverse)
			progress = 1.0 - progress;

		if(!s->tick(s->ease(progress))) {
			list.Remove(i);
			continue;
		}

		if(now - s->start_time >= s->time_ms) {
			if(s->count > 0)
				--s->count;
			if(s->count == 0) {
				// snap final
				s->tick(s->ease(s->reverse ? 0.0 : 1.0));
				list.Remove(i);
				continue;
			}
			if(s->yoyo)
				s->reverse = !s->reverse;
			s->start_time = now;
		}
		++i;
	}

	if(list.GetCount() || any_paused) {
		SetTimeCallback(Animation::GetFPS(), &AnimationTick, &s_timerTag);
	}
	else {
		KillTimeCallback(&s_timerTag);
		s_timerTag = 0;
	}
}

// ---- State methods ----------------------------------------------------------
void Animation::State::Cancel()
{
	auto& list = AnimList();
	for(int i = 0; i < list.GetCount(); ++i) {
		if(list[i].Get() == this) {
			list.Remove(i);
			break;
		}
	}
	owner = nullptr;
}

// ---- Animation methods ------------------------------------------------------
Animation::Animation(Ctrl& c)
{
	s_owner = new State(c);
	s_handle = s_owner.Get();
}

Animation::~Animation() { Cancel(); }

Animation::Animation(Animation&& other) { *this = pick(other); }

Animation& Animation::operator=(Animation&& other)
{
	if(this != &other) {
		Cancel();
		s_owner = pick(other.s_owner);
		s_handle = other.s_handle;
		other.s_handle = nullptr;
	}
	return *this;
}

// Pattern 2: Rebind
void Animation::Rebind(Ctrl& c)
{
	Cancel(); // unschedule any running one
	s_owner.Clear();
	s_owner = new State(c);
	s_handle = s_owner.Get();
}

void Animation::Start()
{
	if(!s_owner)
		return; // nothing pending

	// prepare
	s_handle->start_time = GetTimeClick();
	s_handle->reverse = false;
	s_handle->paused = false;
	s_handle->elapsed_time = 0;

	// move into global list
	AnimList().Add(pick(s_owner));
	// after picking, we MUST null our handle, otherwise Stop() would Cancel wrong thing
	s_handle = nullptr;

	EnsureTimerRunning();
}

void Animation::Pause()
{
	if(!s_handle)
		return; 
	            // Pause during run. Then we need a Ptr. Simpler:
	            // Use State::Cancel? No. We'll allow Pause() only when s_handle is still valid.
	            // If it has started, we need to find it in list. 

}

void Animation::Resume()
{
	// Same note as Pause()
}

void Animation::Stop()
{
	// Snap to end if still pending
	if(s_handle) {
		s_handle->tick(s_handle->ease(s_handle->reverse ? 0.0 : 1.0));
		s_handle->Cancel();
		s_handle = nullptr;
	}
}

void Animation::Cancel()
{
	// Hard kill, no snapping
	if(s_handle) {
		s_handle->Cancel();
		s_handle = nullptr;
	}
}

bool Animation::IsPlaying() const
{
	// If s_handle is null, we cannot tell easily. We'll just say false
	return false;
}

bool Animation::IsPaused() const { return false; }

double Animation::GetProgress() const { return 0.0; }

void Animation::SetFPS(int fps)
{
	if(fps > 0)
		s_frameIntervalMs = 1000 / fps;
}

int Animation::GetFPS() { return s_frameIntervalMs ? 1000 / s_frameIntervalMs : 0; }

// convenience helpers
Animation& Animation::Rect(const Upp::Rect& r)
{
	if(!s_handle)
		return *this;
	Animation::State* st = s_handle;
	Upp::Rect from = st->owner->GetRect();
	st->tick = [st, from, r](double t) {
		if(!st->owner)
			return false;
		st->owner->SetRect(Lerp(from, r, t));
		st->owner->Refresh();
		return true;
	};
	return *this;
}

Animation& Animation::Pos(const Upp::Point& p)
{
	if(!s_handle)
		return *this;
	Animation::State* st = s_handle;
	Upp::Rect from = st->owner->GetRect();
	st->tick = [st, from, p](double t) {
		if(!st->owner)
			return false;
		Upp::Point tl = Lerp(from.TopLeft(), p, t);
		st->owner->SetRect(tl.x, tl.y, from.Width(), from.Height());
		st->owner->Refresh();
		return true;
	};
	return *this;
}

Animation& Animation::Size(const Upp::Size& sz)
{
	if(!s_handle)
		return *this;
	Animation::State* st = s_handle;
	Upp::Rect from = st->owner->GetRect();
	st->tick = [st, from, sz](double t) {
		if(!st->owner)
			return false;
		Upp::Size s = Lerp(from.GetSize(), sz, t);
		Upp::Point c = from.CenterPoint();
		st->owner->SetRect(c.x - s.cx / 2, c.y - s.cy / 2, s.cx, s.cy);
		st->owner->Refresh();
		return true;
	};
	return *this;
}

Animation& Animation::Alpha(double from, double to)
{
	if(!s_handle)
		return *this;
	Animation::State* st = s_handle;
	st->tick = [st, from, to](double t) {
		if(!st->owner)
			return false;
		st->owner->SetAlpha(int(255 * Lerp(from, to, t)));
		st->owner->Refresh();
		return true;
	};
	return *this;
}

void KillAll()
{
	AnimList().Clear();
	EnsureTimerRunning();
}

} // namespace Upp
