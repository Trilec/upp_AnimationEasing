// CtrlLib/Animation.h
//
// Animation system for U++ controls.
// Time-based scheduler driving per-frame updates with easing,
// looping, yoyo support, and simple lifecycle callbacks.
//
// 2025-08-19 — initial test pass / cleaned header & comments
//
// Usage (sketch):
//   Animation a(ctrl);
//   a([](double p){ /* draw with eased p (0..1) */ return true; })
//     .Ease(Easing::InOutCubic())
//     .Duration(300)
//     .Loop(1)
//     .Yoyo(false)
//     .OnFinish(callback(...))
//     .Play();
//
// Notes:
// - Progress() always returns [0..1]. After Stop() it is 1.0, after Cancel()
//   it remains the last seen forward progress.

#ifndef _Animation_Animation_h_
#define _Animation_Animation_h_

#include <Core/Core.h>
#include <CtrlCore/CtrlCore.h>

/*---------------- Easing helpers (cubic-bezier) ----------------*/
namespace Easing {
using Fn = Upp::Function<double(double)>;
namespace detail {
inline double BX(double x1,double x2,double t){ double u=1-t; return 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t; }
inline double BY(double y1,double y2,double t){ double u=1-t; return 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t; }
inline double Solve(double x1,double y1,double x2,double y2,double x){
	if(x<=0) return 0; if(x>=1) return 1;
	double lo=0, hi=1, t=x;
	for(int i=0;i<8;++i){ double cx=BX(x1,x2,t); (cx<x?lo:hi)=t; t=0.5*(lo+hi); }
	return BY(y1,y2,t);
}}
inline Fn Bezier(double x1,double y1,double x2,double y2){ return [=](double t){ return detail::Solve(x1,y1,x2,y2,t); }; }

inline const Fn& Linear()        { static auto f = Upp::MakeOne<Fn>(Bezier(0.000, 0.000, 1.000, 1.000));  return *f; }
inline const Fn& OutBounce()     { static auto f = Upp::MakeOne<Fn>(Bezier(0.680, -0.550, 0.265, 1.550)); return *f; }
inline const Fn& InQuad()        { static auto f = Upp::MakeOne<Fn>(Bezier(0.550, 0.085, 0.680, 0.530));  return *f; }
inline const Fn& OutQuad()       { static auto f = Upp::MakeOne<Fn>(Bezier(0.250, 0.460, 0.450, 0.940));  return *f; }
inline const Fn& InOutQuad()     { static auto f = Upp::MakeOne<Fn>(Bezier(0.455, 0.030, 0.515, 0.955));  return *f; }
inline const Fn& InCubic()       { static auto f = Upp::MakeOne<Fn>(Bezier(0.550, 0.055, 0.675, 0.190));  return *f; }
inline const Fn& OutCubic()      { static auto f = Upp::MakeOne<Fn>(Bezier(0.215, 0.610, 0.355, 1.000));  return *f; }
inline const Fn& InOutCubic()    { static auto f = Upp::MakeOne<Fn>(Bezier(0.645, 0.045, 0.355, 1.000));  return *f; }
inline const Fn& InQuart()       { static auto f = Upp::MakeOne<Fn>(Bezier(0.895, 0.030, 0.685, 0.220));  return *f; }
inline const Fn& OutQuart()      { static auto f = Upp::MakeOne<Fn>(Bezier(0.165, 0.840, 0.440, 1.000));  return *f; }
inline const Fn& InOutQuart()    { static auto f = Upp::MakeOne<Fn>(Bezier(0.770, 0.000, 0.175, 1.000));  return *f; }
inline const Fn& InQuint()       { static auto f = Upp::MakeOne<Fn>(Bezier(0.755, 0.050, 0.855, 0.060));  return *f; }
inline const Fn& OutQuint()      { static auto f = Upp::MakeOne<Fn>(Bezier(0.230, 1.000, 0.320, 1.000));  return *f; }
inline const Fn& InOutQuint()    { static auto f = Upp::MakeOne<Fn>(Bezier(0.860, 0.000, 0.070, 1.000));  return *f; }
inline const Fn& InSine()        { static auto f = Upp::MakeOne<Fn>(Bezier(0.470, 0.000, 0.745, 0.715));  return *f; }
inline const Fn& OutSine()       { static auto f = Upp::MakeOne<Fn>(Bezier(0.390, 0.575, 0.565, 1.000));  return *f; }
inline const Fn& InOutSine()     { static auto f = Upp::MakeOne<Fn>(Bezier(0.445, 0.050, 0.550, 0.950));  return *f; }
inline const Fn& InExpo()        { static auto f = Upp::MakeOne<Fn>(Bezier(0.950, 0.050, 0.795, 0.035));  return *f; }
inline const Fn& OutExpo()       { static auto f = Upp::MakeOne<Fn>(Bezier(0.190, 1.000, 0.220, 1.000));  return *f; }
inline const Fn& InOutExpo()     { static auto f = Upp::MakeOne<Fn>(Bezier(1.000, 0.000, 0.000, 1.000));  return *f; }
inline const Fn& InElastic()     { static auto f = Upp::MakeOne<Fn>(Bezier(0.600, -0.280, 0.735, 0.045)); return *f; }
inline const Fn& OutElastic()    { static auto f = Upp::MakeOne<Fn>(Bezier(0.175, 0.885, 0.320, 1.275));  return *f; }
inline const Fn& InOutElastic()  { static auto f = Upp::MakeOne<Fn>(Bezier(0.680, -0.550, 0.265, 1.550)); return *f; }
} // namespace Easing


namespace Upp {

class Animation {
public:
	/*---------------- Spec describes an animation program ----------------*/
	struct Spec {
		int  duration_ms = 400;                 // total time of a single leg (ms)
		int  loop_count  = 1;                   // -1 == infinite
		int  delay_ms    = 0;                   // start delay (ms)
		bool yoyo        = false;               // forward then reverse per cycle
		Easing::Fn easing = Easing::InOutCubic();// easing function (t in 0..1)

		Function<bool(double)> tick;            // tick(eased_t) -> continue?
		Callback on_start, on_finish, on_cancel;// lifecycle hooks
		Callback1<double> on_update;            // on_update(eased_t)
	};

	/*---------------- State is scheduled runtime instance ----------------*/
	struct State : Pte<State> {
		Ptr<Ctrl> owner;         // safe watcher to owning Ctrl
		Spec  spec;              // copy of staged Spec
		int64 start_ms = 0;      // wallclock when current leg started
		int64 elapsed_ms = 0;    // accumulated time when paused
		bool  paused  = false;   // pause gate
		bool  reverse = false;   // direction (true == reverse leg)
		int   cycles  = 1;       // remaining cycles (when loop_count >= 0)

		Animation* anim = nullptr;   // back-pointer (non-owning)
		bool       finished_flag = false; // natural finish marker
		double     last_progress = 0.0;   // raw forward progress cache [0..1]

		// Advance to 'now'. Returns true to keep scheduling, false to stop.
		bool Step(int64 now);
	};

	/*---------------- Lifecycle ----------------*/
	explicit Animation(Ctrl& owner); // bind to owner Ctrl
	~Animation();                    // detaches from scheduler safely

	Animation(Animation&&) = default;
	Animation& operator=(Animation&&) = default;
	Animation(const Animation&) = delete;
	Animation& operator=(const Animation&) = delete;

	/*---------------- Fluent configuration (staging) ----------------*/
	Animation& Duration(int ms);                   // set duration per leg
	Animation& Ease(const Easing::Fn& fn);         // set easing by ref
	Animation& Ease(Easing::Fn&& fn);              // set easing by move
	Animation& Loop(int n = -1);                   // set loop count (-1 inf)
	Animation& Yoyo(bool b = true);                // enable/disable yoyo
	Animation& Delay(int ms);                      // set initial delay
	Animation& OnStart(const Callback& cb);        // set on_start
	Animation& OnStart(Callback&& cb);             // set on_start (move)
	Animation& OnFinish(const Callback& cb);       // set on_finish
	Animation& OnFinish(Callback&& cb);            // set on_finish (move)
	Animation& OnCancel(const Callback& cb);       // set on_cancel
	Animation& OnCancel(Callback&& cb);            // set on_cancel (move)
	Animation& OnUpdate(const Callback1<double>& c);// set on_update
	Animation& OnUpdate(Callback1<double>&& c);    // set on_update (move)
	Animation& operator()(const Function<bool(double)>& f); // set tick
	Animation& operator()(Function<bool(double)>&& f);      // set tick (move)

	/*---------------- Control ----------------*/
	void Play();                       // commit staged spec and schedule
	void Pause();                      // pause time accumulation
	void Resume();                     // resume time accumulation
	void Stop();                       // complete to 1.0, fire finish, no cancel
	void Cancel(bool fire_cancel = true); // abort; optionally fire cancel

	bool   IsPlaying() const;          // scheduled and not paused
	bool   IsPaused()  const;          // scheduled and paused
	double Progress()  const;          // current progress [0..1]

	/*---------------- Global helpers ----------------*/
	static void SetFPS(int fps);       // clamp to [1..240], affects scheduler
	static int  GetFPS();              // current target FPS
	static void KillAllFor(Ctrl& c);   // abort all animations for this Ctrl
	static void Finalize();            // stop scheduler, free all states

    // n = number of frames to advance; max_ms_per_tick clamps each dt (0 = no clamp).
    static void Tick(int n = 1, int max_ms_per_tick = 0);
    
    // For debugging and Test-only: tick scheduler once (no timer / arm checks).
	static inline void TickOnce() { Tick(1, 0); }

	void _SetProgressCache(double v) { progress_cache_ = v; }
private:
	// Owner and staging
	Ctrl*      owner_ = nullptr;   // non-owning
	One<Spec>  spec_box_;
	Spec*      spec_ = nullptr;    // points into spec_box_ while staging
	Ptr<State> live_;              // scheduler-owned; Ptr protects against UAF

	// Progress cache that persists after Stop()/Cancel()
	double     progress_cache_ = 0.0;

	// Internal helper: centralizes cache writes

};

/*---------------- Convenience helpers for animating values ----------------*/
template <class T>
inline Animation AnimateValue(Ctrl& ctrl, Callback1<const T&> set, T from, T to,
                              int ms, Easing::Fn ease = Easing::InOutCubic())
{
	Animation a(ctrl);
	a([ctrlPtr = Ptr<Ctrl>(&ctrl), set, from, to](double p) -> bool {
		if(!ctrlPtr) return false;
		if constexpr(std::is_same_v<T, Color>)
			set(Blend(from, to, int(255 * p)));
		else if constexpr(std::is_same_v<T, Point>)
			set(Point(int(from.x + (to.x - from.x) * p + .5),
			          int(from.y + (to.y - from.y) * p + .5)));
		else if constexpr(std::is_same_v<T, Size>)
			set(Size(int(from.cx + (to.cx - from.cx) * p + .5),
			         int(from.cy + (to.cy - from.cy) * p + .5)));
		else if constexpr(std::is_same_v<T, Rect>)
			set(Rect(Point(int(from.left + (to.left - from.left) * p + .5),
			               int(from.top  + (to.top  - from.top ) * p + .5)),
			         Size(int(from.Width()  + (to.Width()  - from.Width() ) * p + .5),
			              int(from.Height() + (to.Height() - from.Height()) * p + .5))));
		else
			set(from + (to - from) * p);
		ctrlPtr->Refresh();
		return true;
	})
	.Duration(ms).Ease(ease).Play();
	return pick(a);
}

inline Animation AnimateColor(Ctrl& c, Callback1<const Color&> cb, Color f, Color t,
                              int ms, Easing::Fn e = Easing::InOutCubic())
{ return AnimateValue<Color>(c, cb, f, t, ms, e); }

inline Animation AnimateRect (Ctrl& c, Callback1<const Rect&>  cb, Rect  f, Rect  t,
                              int ms, Easing::Fn e = Easing::InOutCubic())
{ return AnimateValue<Rect>(c, cb, f, t, ms, e); }

} // namespace Upp

#endif // _GUIAnim_GUIAnim_h_
