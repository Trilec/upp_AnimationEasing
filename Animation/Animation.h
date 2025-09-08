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

/*---------------- Easing helpers (constexpr cubic-bézier) ----------------*/
namespace Easing {

using Fn = Upp::Function<double(double)>;

namespace detail {
// Unit-time cubic Bézier: P0=(0,0), P3=(1,1)
constexpr double BX(double x1, double x2, double t) noexcept {
    double u = 1.0 - t;
    return 3.0*u*u*t*x1 + 3.0*u*t*t*x2 + t*t*t;
}
constexpr double BY(double y1, double y2, double t) noexcept {
    double u = 1.0 - t;
    return 3.0*u*u*t*y1 + 3.0*u*t*t*y2 + t*t*t;
}
constexpr double Solve(double x1, double y1, double x2, double y2, double x) noexcept {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    double lo = 0.0, hi = 1.0, t = x;
    // 8 steps is plenty for UI precision
    for (int i = 0; i < 8; ++i) {
        double cx = BX(x1, x2, t);
        (cx < x ? lo : hi) = t;
        t = 0.5 * (lo + hi);
    }
    return BY(y1, y2, t);
}
} // namespace detail

// Factory: returns a tiny callable to evaluate the curve
constexpr auto Bezier(double x1, double y1, double x2, double y2) {
    return [x1, y1, x2, y2](double t) noexcept -> double {
        return detail::Solve(x1, y1, x2, y2, t);
    };
}

// Presets (match CSS-ish names/feel). Usage: .Ease(Easing::OutCubic())
inline constexpr auto Linear()        { return Bezier(0.000, 0.000, 1.000, 1.000); }
inline constexpr auto InQuad()        { return Bezier(0.550, 0.085, 0.680, 0.530); }
inline constexpr auto OutQuad()       { return Bezier(0.250, 0.460, 0.450, 0.940); }
inline constexpr auto InOutQuad()     { return Bezier(0.455, 0.030, 0.515, 0.955); }
inline constexpr auto InCubic()       { return Bezier(0.550, 0.055, 0.675, 0.190); }
inline constexpr auto OutCubic()      { return Bezier(0.215, 0.610, 0.355, 1.000); }
inline constexpr auto InOutCubic()    { return Bezier(0.645, 0.045, 0.355, 1.000); }
inline constexpr auto InQuart()       { return Bezier(0.895, 0.030, 0.685, 0.220); }
inline constexpr auto OutQuart()      { return Bezier(0.165, 0.840, 0.440, 1.000); }
inline constexpr auto InOutQuart()    { return Bezier(0.770, 0.000, 0.175, 1.000); }
inline constexpr auto InQuint()       { return Bezier(0.755, 0.050, 0.855, 0.060); }
inline constexpr auto OutQuint()      { return Bezier(0.230, 1.000, 0.320, 1.000); }
inline constexpr auto InOutQuint()    { return Bezier(0.860, 0.000, 0.070, 1.000); }
inline constexpr auto InSine()        { return Bezier(0.470, 0.000, 0.745, 0.715); }
inline constexpr auto OutSine()       { return Bezier(0.390, 0.575, 0.565, 1.000); }
inline constexpr auto InOutSine()     { return Bezier(0.445, 0.050, 0.550, 0.950); }
inline constexpr auto InExpo()        { return Bezier(0.950, 0.050, 0.795, 0.035); }
inline constexpr auto OutExpo()       { return Bezier(0.190, 1.000, 0.220, 1.000); }
inline constexpr auto InOutExpo()     { return Bezier(1.000, 0.000, 0.000, 1.000); }
inline constexpr auto InElastic()     { return Bezier(0.600, -0.280, 0.735, 0.045); }
inline constexpr auto OutElastic()    { return Bezier(0.175, 0.885, 0.320, 1.275); }
inline constexpr auto InOutElastic()  { return Bezier(0.680, -0.550, 0.265, 1.550); }
// “Bounce” styled as a single cubic segment (intentional overshoot)
inline constexpr auto OutBounce()     { return Bezier(0.680, -0.550, 0.265, 1.550); }

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
	    Spec  spec;              // staged spec snapshot
	    int64 start_ms   = 0;    // when current leg started
	    int64 elapsed_ms = 0;    // accumulated time when paused
	    bool  paused     = false;
	    bool  reverse    = false;
	    int   cycles     = 1;    // remaining cycles (if loop_count >= 0)
	
	    Animation* anim  = nullptr; // back-pointer (non-owning)
	    bool  dying      = false;   // <- mark for deferred removal
	
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
	
	//  called by the scheduler when a State is removed.
    // - Finish path: Progress() becomes 1.0
    // - Cancel/kill path: Progress() becomes provided forward-progress snapshot
	void _OnStateRemovedFinish();
	void _OnStateRemovedCancel(double forward_progress_snapshot);
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

#endif // _Animation_Animation_h_