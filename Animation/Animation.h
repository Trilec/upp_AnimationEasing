// Animation/Animation.h
//
// Animation system for U++ controls.
// Single-threaded scheduler (TimeCallback) that drives per-frame updates with
// easing, looping, yoyo, pause/resume, and simple lifecycle callbacks.
//
// Design model (two compartments):
//   • Staging  — the parameters being prepared by setters (duration, easing,
//                callbacks, tick function). Setters write here until Play().
//   • State    — a live scheduled run created by moving the Staging config
//                into the scheduler. State is immutable during a run.
//
// On Play():  Staging → State (move), staging_ becomes nullptr.
// After Cancel()/Stop(): staging_ is often null; the next setter will lazily
//                        re-prime it (EnsureStaging_()).
// Reset():    Cancel(false) + EnsureStaging_() + Progress() = 0.0
//
// Progress() always returns [0..1]. After Stop(): 1.0.
// After Cancel(): last forward progress snapshot. KillAllFor(): 0.0.

#ifndef _Animation_Animation_h_
#define _Animation_Animation_h_

#include <Core/Core.h>
#include <CtrlCore/CtrlCore.h>

/*---------------- Easing helpers (constexpr cubic-bézier) ----------------
   Factory and presets for CSS-like cubic Bézier easing. Use presets like
   Easing::OutQuart(), or build custom curves with Bezier(x1,y1,x2,y2).
-----------------------------------------------------------------------------*/
namespace Easing {

using Fn = Upp::Function<double(double)>;

namespace detail {
// Unit-time cubic Bézier with P0=(0,0), P3=(1,1).
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
    // 8 bisection steps are enough for UI precision.
    for (int i = 0; i < 8; ++i) {
        double cx = BX(x1, x2, t);
        (cx < x ? lo : hi) = t;
        t = 0.5 * (lo + hi);
    }
    return BY(y1, y2, t);
}
} // namespace detail

// Factory: returns a tiny callable to evaluate the curve.
constexpr auto Bezier(double x1, double y1, double x2, double y2) {
    return [x1, y1, x2, y2](double t) noexcept -> double {
        return detail::Solve(x1, y1, x2, y2, t);
    };
}

// Presets (CSS-ish feel). Usage: .Ease(Easing::OutCubic())
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
// “Bounce”-like single segment with overshoot.
inline constexpr auto OutBounce()     { return Bezier(0.680, -0.550, 0.265, 1.550); }

} // namespace Easing



namespace Upp {

class Animation {
public:
    /*---------------- Staging describes the next run ("the recipe") ------------
       All setters write here prior to Play(). On Play(), a copy/move of this
       configuration is embedded into a live State for deterministic execution.
    ---------------------------------------------------------------------------*/
    struct Staging {
        int  duration_ms = 400;                 // duration per leg (ms)
        int  loop_count  = 1;                   // number of legs; -1 = infinite
        int  delay_ms    = 0;                   // start delay (ms)
        bool yoyo        = false;               // forward then reverse per cycle
        Easing::Fn easing = Easing::InOutCubic();// easing function (t in 0..1)

        // Per-frame tick. Receives eased t in [0..1]. Return false to stop early.
        Function<bool(double)> tick;

        // Lifecycle hooks. on_update(e) fires every frame with the eased value.
        //Callback on_start, on_finish, on_cancel;
        //Callback1<double> on_update;
        
        Event<>      on_start, on_finish, on_cancel;
		Event<double> on_update;
    };

    /*---------------- State is the live scheduled run ("the execution") --------
       Owned and advanced by the scheduler. Immutable settings copied from
       Staging at Play() time; holds timing/yoyo bookkeeping for the current run.
    ---------------------------------------------------------------------------*/
    struct State : Pte<State> {
        Ptr<Ctrl> owner;         // safe watcher of owning Ctrl
        Staging   spec;          // immutable snapshot of the staging config
        int64     start_ms   = 0;// current leg start wall time
        int64     elapsed_ms = 0;// accumulated time when paused
        bool      paused     = false;
        bool      reverse    = false;
        int       cycles     = 1;// remaining cycles (if loop_count >= 0)

        Animation* anim  = nullptr; // back-pointer (non-owning)
        bool       dying = false;   // deferred removal flag during sweep

        // Step the state to 'now'. Returns true to keep scheduling; false to stop.
        bool Step(int64 now);
    };

    /*---------------- Lifecycle -------------------------------------------------
       Construct an animation bound to a control. Destructor detaches safely.
    ---------------------------------------------------------------------------*/
    explicit Animation(Ctrl& owner);
    ~Animation();

    Animation(Animation&&) = default;
    Animation& operator=(Animation&&) = default;
    Animation(const Animation&) = delete;
    Animation& operator=(const Animation&) = delete;

    /*---------------- Fluent configuration (staging) ----------------------------
       Each setter prepares the next run. If staging is null (e.g., right after
       Play()), we lazily re-prime it so callers can immediately reconfigure.
    ---------------------------------------------------------------------------*/
    Animation& Duration(int ms);                     // set duration per leg (ms)
    Animation& Ease(const Easing::Fn& fn);           // set easing by reference
    Animation& Ease(Easing::Fn&& fn);                // set easing by move
    Animation& Loop(int n = -1);                     // set loop count (-1 infinite)
    Animation& Yoyo(bool b = true);                  // enable/disable yoyo playback
    Animation& Delay(int ms);  
                          // set start delay (ms)
    Animation& OnStart(const  Event<>& cb);          // set on_start hook
    Animation& OnStart( Event<>&& cb);               // set on_start (move)
    Animation& OnFinish(const  Event<>& cb);         // set on_finish hook
    Animation& OnFinish( Event<>&& cb);              // set on_finish (move)
    Animation& OnCancel(const  Event<>& cb);         // set on_cancel hook
    Animation& OnCancel( Event<>&& cb);              // set on_cancel (move)
    Animation& OnUpdate(const Event<double>& c);  // set on_update(eased) hook
    Animation& OnUpdate(Event<double>&& c);       // set on_update (move)
    
    Animation& operator()(const Function<bool(double)>& f); // per-frame tick
    Animation& operator()(Function<bool(double)>&& f);      // per-frame tick (move)

    /*---------------- Control ---------------------------------------------------
       Play() commits the staged config and schedules a new run.
       Pause()/Resume() are reversible. Stop() completes to 1.0 and fires finish.
       Cancel() aborts and preserves forward progress. Reset() aborts and primes
       a clean slate so the same instance is immediately reusable.
    ---------------------------------------------------------------------------*/
    void   Play();                         // commit staging → schedule a run
    void   Pause();                        // reversible freeze; no time accrual
    void   Resume();                       // continue after Pause()
    void   Stop();                         // finish now (Progress=1), fire finish
    void   Cancel(bool fire_cancel = true);// abort run; preserve forward snapshot
    void   Reset(bool fire_cancel = false);// abort + re-prime staging; Progress=0
    
    //   - restart_if_running == true  -> Cancel(fire_cancel_if_cancelled) then start
    //   - restart_if_running == false -> do nothing
    void   Replay(bool restart_if_running = true, bool fire_cancel_if_cancelled = false); // Start a new run using the last-used spec from Play()

    // Returns true if a previous Play() established a spec we can Replay().
    bool   HasReplay() const;

    bool   IsPlaying() const;              // true if scheduled and not paused
    bool   IsPaused()  const;              // true if scheduled and paused
    double Progress()  const;              // normalized time progress [0..1]

    /*---------------- Global helpers -------------------------------------------
       Affect the shared scheduler. FPS changes re-arm the timer if needed.
    ---------------------------------------------------------------------------*/
    static void SetFPS(int fps);           // clamp to [1..240]; set target FPS
    static int  GetFPS();                  // current target FPS
    static void KillAllFor(Ctrl& c);       // abort all animations for this Ctrl
    static void Finalize();                // stop scheduler; free all states

    // Test/diagnostic: step scheduler n frames; clamp each dt to max_ms_per_tick.
    static void Tick(int n = 1, int max_ms_per_tick = 0);
    static inline void TickOnce() { Tick(1, 0); } // convenience single step

    // Called by scheduler when a State is removed; updates our progress cache.
    void _OnStateRemovedFinish();                       // Progress ← 1.0
    void _OnStateRemovedCancel(double forward_snapshot);// Progress ← snapshot

private:
    // Owner and staging
    Ctrl*        owner_ = nullptr;     // non-owning: the target control
    One<Staging> staging_box_;         // storage for staging configuration
    Staging*     staging_ = nullptr;   // points into staging_box_ while staging
    Ptr<State>   live_;                // scheduler-owned state; Ptr guards UAF

    // Progress cache that persists after Stop()/Cancel(), used when !live_.
    double       progress_cache_ = 0.0;

    // Lazily (re)create staging if null so setters always have a target.
    void EnsureStaging_();

    // Internal helper for tests/tools.
    void _SetProgressCache(double v) { progress_cache_ = v; }
    
    // Cached copy of the last spec committed by Play(). Exists only after the
    // first successful Play() and persists across runs until replaced.

	One<Staging> last_spec_box_;
	bool         have_last_spec_ = false;
};

/*---------------- Convenience helpers for animating values --------------------
   AnimateValue<T>: builds a one-shot animation that lerps from 'from' to 'to'
   using the provided setter, refreshing the control each frame.
-----------------------------------------------------------------------------*/
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
    .Duration(ms)
    .Ease(ease)
    .Play();
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
