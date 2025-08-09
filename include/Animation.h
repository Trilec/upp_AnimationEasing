#pragma once
#include <Core/Core.h> // int64, Upp::Ptr, Upp::Pte, Function/Callback
#include <CtrlCore/CtrlCore.h>

namespace Easing {

using Fn = Upp::Function<double(double)>;

namespace detail {
inline double BX(double x1, double x2, double t)
{
	double u = 1 - t;
	return 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t;
}
inline double BY(double y1, double y2, double t)
{
	double u = 1 - t;
	return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
}
inline double Solve(double x1, double y1, double x2, double y2, double x)
{
	if(x <= 0)
		return 0;
	if(x >= 1)
		return 1;
	double lo = 0, hi = 1, t = x;
	for(int i = 0; i < 8; ++i) {
		double cx = BX(x1, x2, t);
		(cx < x ? lo : hi) = t;
		t = 0.5 * (lo + hi);
	}
	return BY(y1, y2, t);
}
} // namespace detail

inline Fn Bezier(double x1, double y1, double x2, double y2)
{
	return [=](double t) { return detail::Solve(x1, y1, x2, y2, t); };
}

// Accessor functions returning const references to heap-allocated Fn (never destroyed at atexit)
inline const Fn& Linear()        { static Fn* f = new Fn(Bezier(0.000, 0.000, 1.000, 1.000)); return *f; }
inline const Fn& OutBounce()     { static Fn* f = new Fn(Bezier(0.680, -0.550, 0.265, 1.550)); return *f; }
inline const Fn& InQuad()        { static Fn* f = new Fn(Bezier(0.550, 0.085, 0.680, 0.530)); return *f; }
inline const Fn& OutQuad()       { static Fn* f = new Fn(Bezier(0.250, 0.460, 0.450, 0.940)); return *f; }
inline const Fn& InOutQuad()     { static Fn* f = new Fn(Bezier(0.455, 0.030, 0.515, 0.955)); return *f; }
inline const Fn& InCubic()       { static Fn* f = new Fn(Bezier(0.550, 0.055, 0.675, 0.190)); return *f; }
inline const Fn& OutCubic()      { static Fn* f = new Fn(Bezier(0.215, 0.610, 0.355, 1.000)); return *f; }
inline const Fn& InOutCubic()    { static Fn* f = new Fn(Bezier(0.645, 0.045, 0.355, 1.000)); return *f; }
inline const Fn& InQuart()       { static Fn* f = new Fn(Bezier(0.895, 0.030, 0.685, 0.220)); return *f; }
inline const Fn& OutQuart()      { static Fn* f = new Fn(Bezier(0.165, 0.840, 0.440, 1.000)); return *f; }
inline const Fn& InOutQuart()    { static Fn* f = new Fn(Bezier(0.770, 0.000, 0.175, 1.000)); return *f; }
inline const Fn& InQuint()       { static Fn* f = new Fn(Bezier(0.755, 0.050, 0.855, 0.060)); return *f; }
inline const Fn& OutQuint()      { static Fn* f = new Fn(Bezier(0.230, 1.000, 0.320, 1.000)); return *f; }
inline const Fn& InOutQuint()    { static Fn* f = new Fn(Bezier(0.860, 0.000, 0.070, 1.000)); return *f; }
inline const Fn& InSine()        { static Fn* f = new Fn(Bezier(0.470, 0.000, 0.745, 0.715)); return *f; }
inline const Fn& OutSine()       { static Fn* f = new Fn(Bezier(0.390, 0.575, 0.565, 1.000)); return *f; }
inline const Fn& InOutSine()     { static Fn* f = new Fn(Bezier(0.445, 0.050, 0.550, 0.950)); return *f; }
inline const Fn& InExpo()        { static Fn* f = new Fn(Bezier(0.950, 0.050, 0.795, 0.035)); return *f; }
inline const Fn& OutExpo()       { static Fn* f = new Fn(Bezier(0.190, 1.000, 0.220, 1.000)); return *f; }
inline const Fn& InOutExpo()     { static Fn* f = new Fn(Bezier(1.000, 0.000, 0.000, 1.000)); return *f; }
inline const Fn& InElastic()     { static Fn* f = new Fn(Bezier(0.600, -0.280, 0.735, 0.045)); return *f; }
inline const Fn& OutElastic()    { static Fn* f = new Fn(Bezier(0.175, 0.885, 0.320, 1.275)); return *f; }
inline const Fn& InOutElastic()  { static Fn* f = new Fn(Bezier(0.680, -0.550, 0.265, 1.550)); return *f; }


} // namespace Easing

enum AnimMode { Once, Loop, Yoyo };

using namespace Upp;

class Animation {
public:
	struct Spec {
		int duration_ms = 400;
		int loop_count = 1;
		int delay_ms = 0;
		bool yoyo = false;
		Easing::Fn easing = Easing::InOutCubic(); // () since accessors now

		Upp::Function<bool(double)> tick;
		Upp::Callback on_start, on_finish, on_cancel;
		Upp::Callback1<double> on_update;
	};

	struct State : Upp::Pte<State> {
		Upp::Ptr<Upp::Ctrl> owner;
		Spec spec;

		int64 start_ms = 0;
		int64 elapsed_ms = 0;
		bool paused = false;
		bool reverse = false;
		int cycles = 1;

		bool Step(int64 now)
		{
			if(!owner)
				return false;
			if(paused)
				return true;

			int64 local = now - start_ms + elapsed_ms;
			if(local < spec.delay_ms)
				return true;

			double t = double(local - spec.delay_ms) / spec.duration_ms;
			t = clamp(t, 0.0, 1.0);
			if(reverse)
				t = 1.0 - t;

			double e = spec.easing ? spec.easing(t) : t;

			if(spec.on_update)
				spec.on_update(e);
			if(spec.tick && !spec.tick(e))
				return false;

			if(t >= 1.0) {
				if(spec.yoyo)
					reverse = !reverse;
				if(spec.loop_count >= 0 && --cycles <= 0) {
					if(spec.on_finish)
						spec.on_finish();
					return false;
				}
				start_ms = now;
				elapsed_ms = 0;
			}
			return true;
		}
	};

	explicit Animation(Upp::Ctrl& owner);
	~Animation();

static void ShutdownScheduler();


	Animation(Animation&&) = default;
	Animation& operator=(Animation&&) = default;
	Animation(const Animation&) = delete;
	Animation& operator=(const Animation&) = delete;

	/* fluent API */
	Animation& Duration(int ms);

	// Easing overloads (const& and &&)
	Animation& Ease(const Easing::Fn& fn);
	Animation& Ease(Easing::Fn&& fn);

	Animation& Loop(int n = -1);
	Animation& Yoyo(bool b = true);
	Animation& Delay(int ms);

	// Callback overloads (const& and &&)
	Animation& OnStart(const Upp::Callback& cb);
	Animation& OnStart(Upp::Callback&& cb);

	Animation& OnFinish(const Upp::Callback& cb);
	Animation& OnFinish(Upp::Callback&& cb);

	Animation& OnCancel(const Upp::Callback& cb);
	Animation& OnCancel(Upp::Callback&& cb);

	Animation& OnUpdate(const Upp::Callback1<double>& c);
	Animation& OnUpdate(Upp::Callback1<double>&& c);

	// Tick functor overloads (const& and &&)
	Animation& operator()(const Upp::Function<bool(double)>& f);
	Animation& operator()(Upp::Function<bool(double)>&& f);

	/* control */
	void Play();
	void Pause();
	void Resume();
	void Stop();
	void Cancel();

	bool IsPlaying() const;
	bool IsPaused() const;
	double Progress() const;

	/* global helpers */
	static void SetFPS(int fps);
	static int GetFPS();
	static void KillAll();
	static void KillAllFor(Upp::Ctrl& c);

private:
	Upp::Ctrl* owner_; // owner control/window (non-owning)
	Upp::One<Spec> spec_box_;
	Spec* spec_ = nullptr;
	Upp::Ptr<State> live_; // scheduler-owned; Ptr<> avoids UAF if deleted elsewhere
};

/* ---------- inline helpers ---------- */
template <class T>
inline Animation AnimateValue(Upp::Ctrl& ctrl, Upp::Callback1<const T&> set, T from, T to,
                              int ms, Easing::Fn ease = Easing::InOutCubic(),
                              AnimMode mode = Once)
{
	Animation a(ctrl);
	a([ctrlPtr = Upp::Ptr<Upp::Ctrl>(&ctrl), set, from, to](double p) -> bool {
		if(!ctrlPtr)
			return false;
		if constexpr(std::is_same_v<T, Upp::Color>)
			set(Upp::Blend(from, to, int(255 * p)));
		else if constexpr(std::is_same_v<T, Upp::Point>)
			set(Upp::Point(int(from.x + (to.x - from.x) * p + .5),
			               int(from.y + (to.y - from.y) * p + .5)));
		else if constexpr(std::is_same_v<T, Upp::Size>)
			set(Upp::Size(int(from.cx + (to.cx - from.cx) * p + .5),
			              int(from.cy + (to.cy - from.cy) * p + .5)));
		else if constexpr(std::is_same_v<T, Upp::Rect>)
			set(Upp::Rect(
				Upp::Point(int(from.left + (to.left - from.left) * p + .5),
			               int(from.top + (to.top - from.top) * p + .5)),
				Upp::Size(int(from.Width() + (to.Width() - from.Width()) * p + .5),
			              int(from.Height() + (to.Height() - from.Height()) * p + .5))));
		else
			set(from + (to - from) * p);
		ctrlPtr->Refresh();
		return true;
	})
		.Duration(ms)
		.Ease(ease);

	if(mode == Loop)
		a.Loop(-1);
	else if(mode == Yoyo)
		a.Yoyo();

	a.Play();
	return Upp::pick(a);
}

inline Animation AnimateColor(Upp::Ctrl& c, Upp::Callback1<const Upp::Color&> cb, Upp::Color f,
                              Upp::Color t, int ms, Easing::Fn e = Easing::InOutCubic(),
                              AnimMode m = Once)
{
	return AnimateValue<Upp::Color>(c, cb, f, t, ms, e, m);
}

inline Animation AnimateRect(Upp::Ctrl& c, Upp::Callback1<const Upp::Rect&> cb, Upp::Rect f,
                             Upp::Rect t, int ms, Easing::Fn e = Easing::InOutCubic(),
                             AnimMode m = Once)
{
	return AnimateValue<Upp::Rect>(c, cb, f, t, ms, e, m);
}
