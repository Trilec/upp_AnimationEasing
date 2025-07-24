// File: uppsrc/CtrlLib/Animation.h
#ifndef _Animation_h_
#define _Animation_h_

#include <CtrlCore/CtrlCore.h>
#include <cmath>
#include <functional>

namespace Upp {

// -----------------------------------------------------------------------------
// Easing
// -----------------------------------------------------------------------------
namespace Easing {
using Fn = std::function<double(double)>;

namespace Detail {
inline double BezierX(double x1, double x2, double t) noexcept
{
	double u = 1.0 - t;
	return 3.0 * u * u * t * x1 + 3.0 * u * t * t * x2 + t * t * t;
}
} // namespace Detail

inline double DeCasteljau(double x1, double y1, double x2, double y2, double x) noexcept
{
	if(x <= 0.0)
		return 0.0;
	if(x >= 1.0)
		return 1.0;
	double t_min = 0.0, t_max = 1.0, t = x;
	for(int i = 0; i < 8; ++i) {
		double cx = Detail::BezierX(x1, x2, t);
		if(std::abs(x - cx) < 1e-6)
			break;
		(cx < x ? t_min : t_max) = t;
		t = (t_min + t_max) * 0.5;
	}
	double u = 1.0 - t;
	return 3.0 * u * u * t * y1 + 3.0 * u * t * t * y2 + t * t * t;
}

inline Fn Bezier(double x1, double y1, double x2, double y2) noexcept
{
	return [=](double t) noexcept { return DeCasteljau(x1, y1, x2, y2, t); };
}

inline const Fn Linear = Bezier(0.0, 0.0, 1.0, 1.0);
inline const Fn OutBounce = Bezier(0.68, -0.55, 0.265, 1.55);
inline const Fn InQuad = Bezier(0.55, 0.085, 0.68, 0.53);
inline const Fn OutQuad = Bezier(0.25, 0.46, 0.45, 0.94);
inline const Fn InOutQuad = Bezier(0.455, 0.03, 0.515, 0.955);
inline const Fn InCubic = Bezier(0.55, 0.055, 0.675, 0.19);
inline const Fn OutCubic = Bezier(0.215, 0.61, 0.355, 1.0);
inline const Fn InOutCubic = Bezier(0.645, 0.045, 0.355, 1.0);
inline const Fn InQuart = Bezier(0.895, 0.03, 0.685, 0.22);
inline const Fn OutQuart = Bezier(0.165, 0.84, 0.44, 1.0);
inline const Fn InOutQuart = Bezier(0.77, 0.0, 0.175, 1.0);
inline const Fn InQuint = Bezier(0.755, 0.05, 0.855, 0.06);
inline const Fn OutQuint = Bezier(0.23, 1.0, 0.32, 1.0);
inline const Fn InOutQuint = Bezier(0.86, 0.0, 0.07, 1.0);
inline const Fn InSine = Bezier(0.47, 0.0, 0.745, 0.715);
inline const Fn OutSine = Bezier(0.39, 0.575, 0.565, 1.0);
inline const Fn InOutSine = Bezier(0.445, 0.05, 0.55, 0.95);
inline const Fn InExpo = Bezier(0.95, 0.05, 0.795, 0.035);
inline const Fn OutExpo = Bezier(0.19, 1.0, 0.22, 1.0);
inline const Fn InOutExpo = Bezier(1.0, 0.0, 0.0, 1.0);
inline const Fn InElastic = Bezier(0.6, -0.28, 0.735, 0.045);
inline const Fn OutElastic = Bezier(0.175, 0.885, 0.32, 1.275);
inline const Fn InOutElastic = Bezier(0.68, -0.55, 0.265, 1.55);
} // namespace Easing

enum AnimMode { Once, Loop, Yoyo };

// -----------------------------------------------------------------------------
// Animation
// -----------------------------------------------------------------------------
class Animation {
public:
	struct State : public Pte<State> {
		Ctrl* owner = nullptr;
		Function<bool(double)> tick;
		int time_ms = 500;
		int count = 1;
		bool yoyo = false;
		Easing::Fn ease = Easing::InOutQuad;
		int64 start_time = 0;
		bool reverse = false;
		bool paused = false;
		int64 elapsed_time = 0;

		explicit State(Ctrl& c)
			: owner(&c)
		{
		}
		void Cancel();
	};

	Animation() = default;
	explicit Animation(Ctrl& c);
	~Animation();

	Animation(Animation&& other);
	Animation& operator=(Animation&& other);

	Animation(const Animation&) = delete;
	Animation& operator=(const Animation&) = delete;

	// ---- Pattern 2 helpers ----
	void Rebind(Ctrl& c); // cancel current, bind to new Ctrl
	template <class F>
	void Rebuild(Ctrl& c, F builder)
	{ // convenience wrapper
		Rebind(c);
		builder(*this);
	}

	// Control
	void Start();
	void Pause();
	void Resume();
	void Stop();   // snap to end & unschedule
	void Cancel(); // hard kill, no final snap

	bool IsPlaying() const;
	bool IsPaused() const;
	double GetProgress() const;

	// Global FPS
	static void SetFPS(int fps);
	static int GetFPS();

	// Fluent setters
	Animation& Time(int ms)
	{
		if(s_handle)
			s_handle->time_ms = ms;
		return *this;
	}
	Animation& Ease(Easing::Fn fn)
	{
		if(s_handle)
			s_handle->ease = fn;
		return *this;
	}
	Animation& Count(int cnt)
	{
		if(s_handle)
			s_handle->count = cnt;
		return *this;
	}
	Animation& Yoyo(int cnt = -1)
	{
		if(s_handle) {
			s_handle->yoyo = true;
			s_handle->count = cnt < 0 ? -1 : 2 * cnt;
		}
		return *this;
	}
	Animation& operator()(Function<bool(double)> f)
	{
		if(s_handle)
			s_handle->tick = pick(f);
		return *this;
	}

	// Convenience helpers
	Animation& Rect(const Upp::Rect& r);
	Animation& Pos(const Upp::Point& p);
	Animation& Size(const Upp::Size& sz);
	Animation& Alpha(double from, double to);

private:
	One<State> s_owner;           // pending state before Start()
	State* s_handle = nullptr;    // pointer to pending state; null after Start()
	static int s_frameIntervalMs; // ms per tick
};

void KillAll();

// -----------------------------------------------------------------------------
// Generic helpers
// -----------------------------------------------------------------------------
template <typename T>
inline Animation AnimateValue(Ctrl& c, Gate<T> setter, const T& from, const T& to, int ms,
                              Easing::Fn ease = Easing::InOutQuad, AnimMode mode = Once)
{
	Animation a(c);
	a([=](double t) {
		if constexpr(std::is_same_v<T, Color>)
			return setter(Blend(from, to, (float)t));
		else if constexpr(std::is_same_v<T, Point>)
			return setter(Point(int(from.x + (to.x - from.x) * t + 0.5),
			                    int(from.y + (to.y - from.y) * t + 0.5)));
		else if constexpr(std::is_same_v<T, Size>)
			return setter(Size(int(from.cx + (to.cx - from.cx) * t + 0.5),
			                   int(from.cy + (to.cy - from.cy) * t + 0.5)));
		else if constexpr(std::is_same_v<T, Rect>)
			return setter(Rect(Lerp(from.TopLeft(), to.TopLeft(), t),
			                   Lerp(from.GetSize(), to.GetSize(), t)));
		else
			return setter(from + (to - from) * t);
	})
		.Time(ms)
		.Ease(ease);
	if(mode == Loop)
		a.Count(-1);
	else if(mode == Yoyo)
		a.Yoyo();
	a.Start();
	return a;
}

inline Animation AnimateDouble(Ctrl& c, Gate<double> g, double f, double t, int ms,
                               Easing::Fn e = Easing::InOutQuad, AnimMode m = Once)
{
	return AnimateValue(c, g, f, t, ms, e, m);
}
inline Animation AnimateInteger(Ctrl& c, Gate<int> g, int f, int t, int ms,
                                Easing::Fn e = Easing::InOutQuad, AnimMode m = Once)
{
	return AnimateValue(c, g, f, t, ms, e, m);
}
inline Animation AnimateColor(Ctrl& c, Gate<Color> g, Color f, Color t, int ms,
                              Easing::Fn e = Easing::InOutQuad, AnimMode m = Once)
{
	return AnimateValue(c, g, f, t, ms, e, m);
}
inline Animation AnimatePoint(Ctrl& c, Gate<Point> g, Point f, Point t, int ms,
                              Easing::Fn e = Easing::InOutQuad, AnimMode m = Once)
{
	return AnimateValue(c, g, f, t, ms, e, m);
}
inline Animation AnimateSize(Ctrl& c, Gate<Size> g, Size f, Size t, int ms,
                             Easing::Fn e = Easing::InOutQuad, AnimMode m = Once)
{
	return AnimateValue(c, g, f, t, ms, e, m);
}
inline Animation AnimateRect(Ctrl& c, Gate<Rect> g, Rect f, Rect t, int ms,
                             Easing::Fn e = Easing::InOutQuad, AnimMode m = Once)
{
	return AnimateValue(c, g, f, t, ms, e, m);
}
inline Animation AnimateAlpha(Ctrl& c, double from, double to, int ms,
                              Easing::Fn e = Easing::InOutQuad, AnimMode m = Once)
{
	return Animation(c).Alpha(from, to).Time(ms).Ease(e).Start(), Animation(c);
}

} // namespace Upp
#endif
