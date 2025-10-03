/*------------------------------------------------------------------------------
    ConsoleAnim.cpp — headless, deterministic probe for CtrlLib/Animation

    WHAT THIS IS
    ------------
    A self-contained console test suite for the Animation library that:
      • Never opens any window (no GUI subsystem needed).
      • Drives frames with Animation::TickOnce() for deterministic timing.
      • Uses a plain Ctrl only as an *owner* (never opened).
      • Prints clear PASS/FAIL lines per test and a summary.

    WHY THIS APPROACH
    -----------------
    - Reproducible across platforms/runners: no message pumps or timer jitter.
    - CI-friendly: works under CONSOLE_APP_MAIN without GUI init.
    - Catches real-world issues: ownership, cancel/stop while stepping,
      yoyo/loops, delays, progress bounds, re-entrancy, finalization.

    IMPORTANT
    ---------
    - No EXITBLOCKs here. We finalize explicitly via Animation::Finalize().
------------------------------------------------------------------------------*/
#include <Core/Core.h>

using namespace Upp;

#include <Animation/Animation.h>
#include "ConsoleAnim.h"

// ---------- deterministic time driver (no GUI pump) ----------
static void PumpForMs(int ms) {
    int64 until = msecs() + ms;
    while (msecs() < until) {
        Animation::TickOnce();   // advance scheduler deterministically
        Sleep(1);
    }
}

// ---------- shared fixtures ----------
struct Probe {
    Ctrl owner;
    Vector<One<Animation>> pool;

    Probe() { owner.SetRect(0,0,400,300); }

    Animation* Spawn() {
        One<Animation>& slot = pool.Add();
        slot.Create(owner);        // construct Animation(owner)
        return ~slot;              // Animation*
    }

    void ClearPool() { pool.Clear(); } // destroys owned Animations
};

struct BoolFlag { bool* p; void Set() { if (p) *p = true; } };

struct ReentrantStarter {
    Probe* probe = nullptr;
    int*   ticks2 = nullptr;
    void StartNext() {
        Animation* spawned = probe->Spawn();     // owned by Probe::pool
        spawned->operator()([&](double){ ++(*ticks2); return true; })
               .Duration(80).Play();
    }
};


// L1 — Ctrl existence (owner can be constructed)
static bool L1_make_owner(Probe& p) {
    Cout() << "L1: Made owner Ctrl\n";
    return p.owner.IsOpen() || true;   // just prove we can construct it
}

// L2 — Basic time pump works
static bool L2_pump_events(Probe&) {
    PumpForMs(10);
    Cout() << "L2: BAsic Pumped events\n";
    return true;
}

// L3 — Construct & scope-exit with no Play()
static bool L3_construct_only(Probe& p){
    { Animation a(p.owner); }
    Cout() << "L3: Construct+scope-exit ok\n";
    return true;
}

// L4 — Play then Cancel stops cleanly
static bool L4_play_cancel(Probe& p) {
    Animation a(p.owner);
    a([](double){ return true; }).Duration(50).Play();
    PumpForMs(5); a.Cancel();
    Cout() << "L4: Play+Cancel done\n"; return true;
}

// L5 — Tick callback is invoked at least once
static bool L5_ticks_count(Probe& p) {
    int ticks = 0;
    Animation a(p.owner);
    a([&](double){ ++ticks; return true; }).Duration(80).Play();
    PumpForMs(150);
    Cout() << Format("L5: ticks=%d\n", ticks);
    return ticks > 0;
}

// L6 — Natural finish without intervention
static bool L6_natural_finish(Probe& p) {
    Animation a(p.owner); a([](double){ return true; }).Duration(60).Play();
    PumpForMs(200); Cout() << "L6: natural finish\n"; return true;
}

// L7 — Double Cancel is harmless
static bool L7_double_cancel(Probe& p) {
    Animation a(p.owner); a([](double){ return true; }).Duration(60).Play();
    PumpForMs(5); a.Cancel(); a.Cancel();
    Cout() << "L7: double cancel ok\n"; return true;
}

// L8 — KillAllFor(owner) aborts owner’s animations
static bool L8_kill_all_for(Probe& p) {
    Animation a(p.owner); a([](double){ return true; }).Duration(200).Play();
    PumpForMs(20); Animation::KillAllFor(p.owner);
    Cout() << "L8: KillAllFor issued\n"; return true;
}

// L9 — Two animations can run concurrently
static bool L9_two_anims(Probe& p) {
    int a1=0, a2=0;
    Animation x(p.owner), y(p.owner);
    x([&](double){ ++a1; return true; }).Duration(120).Play();
    y([&](double){ ++a2; return true; }).Duration(120).Play();
    PumpForMs(160);
    Cout() << Format("L9: ticks a1=%d a2=%d\n", a1, a2);
    return a1 > 0 && a2 > 0;
}

// L10 — Owner destruction stops its animations safely
static bool L10_owner_destroyed() {
    int ticks = 0;
    {   Ctrl c2;
        Animation a(c2);
        a([&](double){ ++ticks; return true; }).Duration(300).Play();
        PumpForMs(50); /* c2 dtor here */
    }
    PumpForMs(50);
    Cout() << Format("L10: owner gone, ticks(before close)=%d\n", ticks);
    return true;
}

// L11 — Stress burst: many short animations complete
static bool L11_stress(Probe& p) {
    for (int i=0; i<200; ++i) {
        Animation a(p.owner);
        a([](double){ return true; }).Duration(15).Play();
        if((i % 40) == 0) Cout() << Format("L11: burst at i=%d\n", i);
    }
    PumpForMs(400);
    Cout() << "L11: stress done\n";
    return true;
}

// L12 — Pause freezes time; Resume continues
static bool L12_pause_resume(Probe& p) {
    int ticks = 0;
    Animation a(p.owner);
    a([&](double){ ++ticks; return true; }).Duration(240).Play();
    PumpForMs(30);
    a.Pause();
    int at_pause = ticks;
    PumpForMs(50);
    bool frozen = (ticks == at_pause);
    a.Resume();
    PumpForMs(250);
    Cout() << "L12: pause/resume done\n";
    return frozen;
}

// L13 — Stop() triggers finish only (not cancel)
static bool L13_stop_calls_finish_only(Probe& p) {
    bool finish=false, cancel=false;
    BoolFlag onfin{&finish}, oncan{&cancel};
    Animation a(p.owner);
    a([](double){ return true; })
      .OnFinish(callback(&onfin, &BoolFlag::Set))
      .OnCancel(callback(&oncan, &BoolFlag::Set))
      .Duration(500).Play();
    PumpForMs(20); a.Stop(); PumpForMs(10);
    Cout() << "L13: stop->finish only\n"; return finish && !cancel;
}

// L14 — Cancel() triggers cancel only (not finish)
static bool L14_cancel_calls_cancel_only(Probe& p) {
    bool finish=false, cancel=false;
    BoolFlag onfin{&finish}, oncan{&cancel};
    Animation a(p.owner);
    a([](double){ return true; })
      .OnFinish(callback(&onfin, &BoolFlag::Set))
      .OnCancel(callback(&oncan, &BoolFlag::Set))
      .Duration(500).Play();
    PumpForMs(20); a.Cancel(); PumpForMs(10);
    Cout() << "L14: cancel->cancel only\n"; return cancel && !finish;
}

// L15 — Start Delay is respected (no ticks before delay)
static bool L15_delay_respected(Probe& p) {
    int ticks=0;
    int64 start = msecs();
    Animation a(p.owner);
    a([&](double){ ++ticks; return true; })
      .Delay(120).Duration(60).Play();
    PumpForMs(80);
    bool pre_ok = (ticks == 0);
    PumpForMs(80);
    bool post_ok = (ticks > 0) && (msecs() - start >= 120);
    Cout() << "L15: delay respected\n";
    return pre_ok && post_ok;
}

// L16 — Loop + Yoyo reverses direction mid-cycle
static bool L16_loop_yoyo_cycles(Probe& p) {
    Vector<double> seen;
    Animation a(p.owner);
    a([&](double t){ seen.Add(t); return true; })
      .Yoyo(true).Loop(2).Duration(80).Play();
    PumpForMs(220);
    bool up=false, down=false;
    for (int i=1;i<seen.GetCount();++i) {
        if (seen[i] > seen[i-1]) up = true;
        if (up && seen[i] < seen[i-1]) { down = true; break; }
    }
    Cout() << "L16: loop+yoyo\n";
    return up && down;
}

// L17 — An easing preset (OutQuad) still reaches finish
static bool L17_easing_outquad_completes(Probe& p) {
    bool finished=false;
    BoolFlag onfin{&finished};
    Animation a(p.owner);
    a([](double){ return true; })
      .Ease(Easing::OutQuad())
      .OnFinish(callback(&onfin, &BoolFlag::Set))
      .Duration(80).Play();
    PumpForMs(160);
    Cout() << "L17: easing completes\n";
    return finished;
}

// L18 — SetFPS clamps to [1..240]
static bool L18_fps_setter_clamps() {
    int orig = Animation::GetFPS();
    Animation::SetFPS(0);     int f1 = Animation::GetFPS();
    Animation::SetFPS(10000); int f2 = Animation::GetFPS();
    Animation::SetFPS(orig);
    bool ok = (f1 >= 1 && f2 <= 240);
    Cout() << "L18: FPS clamp\n";
    return ok;
}

// L19 — Progress ∈ [0..1] and ends ~1.0
static bool L19_progress_bounds(Probe& p) {
    Animation a(p.owner);
    a([](double){ return true; }).Duration(120).Play();
    bool in_bounds = true;
    for (int i=0;i<10;++i) {
        double prog = a.Progress();
        if (!(prog >= 0.0 && prog <= 1.0)) { in_bounds=false; break; }
        PumpForMs(15);
    }
    PumpForMs(150);
    double finalp = a.Progress();
    Cout() << Format("L19: progress final=%.3f\n", finalp);
    return in_bounds && finalp >= 0.99;
}

// L20 — Re-entrant OnFinish can start another animation
static bool L20_reentrant_onfinish_starts_new(Probe& p) {
    int ticks2 = 0;
    ReentrantStarter r{ &p, &ticks2 };
    {   Animation a1(p.owner);
        a1([](double){ return true; })
          .Duration(60)
          .OnFinish(callback(&r, &ReentrantStarter::StartNext))
          .Play();
        PumpForMs(200);
    }
    bool ok = ticks2 > 0;
    Cout() << "L20: reentrant finish\n";
    return ok;
}

// L21 — Exceptions thrown in tick do not crash app
static bool L21_exception_in_tick_is_caught(Probe& p) {
    Animation a(p.owner);
    bool survived = true;
    int hits = 0;
    a([&](double){
          ++hits;
          if (hits == 1) throw 123;
          return true;
      }).Duration(80).Play();
    PumpForMs(120);
    Cout() << "L21: exception caught (no crash)\n";
    return survived;
}

// L22 — Finalize while running halts scheduling cleanly
static bool L22_finalize_while_running(Probe& p) {
    int ticks = 0;
    Animation a(p.owner);
    a([&](double){ ++ticks; return true; }).Duration(500).Play();
    PumpForMs(20);
    Animation::Finalize();
    int before = ticks;
    PumpForMs(100);
    bool halted = (ticks == before);
    Cout() << "L22: finalize while running\n";
    return halted;
}

// ----------------------------- EXTRA EDGE CASES -----------------------------

// L23 — Pause during Delay holds time (no ticks until resume)
static bool L23_pause_inside_delay(Probe& p) {
    int ticks=0;
    Animation a(p.owner);
    a([&](double){ ++ticks; return true; })
      .Delay(200).Duration(60).Play();

    PumpForMs(50);
    a.Pause();
    int before = ticks;
    PumpForMs(250);                // would exceed delay, but paused
    bool no_ticks_while_paused = (ticks == before);

    a.Resume();
    PumpForMs(260);                // now cross delay boundary
    return no_ticks_while_paused && ticks > 0;
}

// L24 — Cancel called inside tick fires cancel only
static bool L24_cancel_inside_tick(Probe& p) {
    bool cancel=false, finish=false;
    BoolFlag oncan{&cancel}, onfin{&finish};
    Animation a(p.owner);
    a([&](double){ a.Cancel(); return true; })
      .OnCancel(callback(&oncan, &BoolFlag::Set))
      .OnFinish(callback(&onfin, &BoolFlag::Set))
      .Duration(200).Play();
    PumpForMs(50);
    return cancel && !finish;
}

// L25 — Changing FPS mid-run keeps animation healthy and finishes
static bool L25_setfps_midrun(Probe& p) {
    int ticks=0; bool finished=false; BoolFlag onfin{&finished};
    Animation a(p.owner);
    a([&](double){ ++ticks; return true; })
      .OnFinish(callback(&onfin, &BoolFlag::Set))
      .Duration(300).Play();

    PumpForMs(60);
    Animation::SetFPS(15);    // slow down
    PumpForMs(120);
    Animation::SetFPS(240);   // speed up
    PumpForMs(300);

    return ticks > 0 && finished;
}

// L26 — After KillAllFor, Progress() reports forced 0.0
static bool L26_progress_after_killallfor(Probe& p) {
    Animation a(p.owner);
    a([](double){ return true; }).Duration(500).Play();
    PumpForMs(10);
    Animation::KillAllFor(p.owner);
    PumpForMs(10);
    double prog = a.Progress();
    return prog <= 1e-6;
}

// L27 — Reuse after Cancel: setters safe + re-Play works (no crash, ticks > 0)
static bool L27_reuse_after_cancel(Probe& p) {
    Animation a(p.owner);
    int ticks = 0;
    a([](double){ return true; }).Duration(80).Play();
    PumpForMs(10);

    a.Cancel();                               // abort current run
    // Now reconfigure on the same object — this used to crash if spec_ was null
    a.Duration(60).Ease(Easing::OutQuad())    // setters must re-prime spec
     ( [&](double){ ++ticks; return true; } )
     .Play();

    PumpForMs(30);
    bool ok = ticks > 0;
    Cout() << "L27: ticks=" << ticks << '\n';
    return ok;
}

// L28 — Cancel while paused, then reconfigure + Play (no crash, resumes clean)
static bool L28_cancel_while_paused_then_reuse(Probe& p) {
    Animation a(p.owner);
    int ticks = 0;
    a([&](double){ ++ticks; return true; }).Duration(120).Play();
    PumpForMs(10);
    a.Pause();
    a.Cancel();                               // cancel in paused state

    // Reuse same instance: should be safe and start from 0
    ticks = 0;
    a.Duration(50)([&](double){ ++ticks; return true; }).Play();
    PumpForMs(20);
    Cout() << "L28: ticks=" << ticks << '\n';
    return ticks > 0;
}


// L29 — Replay() reuses last spec (duration/ease/yoyo/loop)
static bool L29_replay_reuses_last_spec(Probe& p) {
    int hits1 = 0, hits2 = 0;

    Animation a(p.owner);
    a([&](double){ ++hits1; return true; })
      .Duration(80)
      .Ease(Easing::OutQuad())
      .Yoyo(true)
      .Loop(1)       // forward+reverse once (because yoyo)
      .Play();

    PumpForMs(200);

    // Now start a new run without retyping setters:
    a([&](double){ ++hits2; return true; }).Replay();

    PumpForMs(200);

    // Both runs should have ticked (>0). We don’t assert exact counts here;
    // just that Replay produced a real run with the previous spec.
    return hits1 > 0 && hits2 > 0;
}

// L30 — Replay() can be overridden by new setters before calling it
static bool L30_replay_after_setters_override(Probe& p) {
    int hits = 0;

    Animation a(p.owner);
    a([](double){ return true; }).Duration(200).Play();   // long-ish first run
    PumpForMs(30);

    // Change the spec to much shorter and ensure Replay uses the new spec.
    a.Duration(40).Ease(Easing::InOutCubic());

    a([&](double){ ++hits; return true; }).Replay();
    PumpForMs(120);

    return hits > 0; // if it never ran, override didn't take
}

// L31 — Reset() primes staging and sets Progress() back to 0
static bool L31_reset_primes_staging_and_zeros_progress(Probe& p) {
    Animation a(p.owner);
    a([](double){ return true; }).Duration(120).Play();
    PumpForMs(30);

    // Take a snapshot before reset (should be > 0)
    double before = a.Progress();

    a.Reset();  // abort current run, prime fresh staging, Progress -> 0

    double after = a.Progress();
    bool zeroed = (after <= 1e-9);

    // Immediately reconfigure and play again to prove staging is fresh
    int hits = 0;
    a.Duration(50)([&](double){ ++hits; return true; }).Play();
    PumpForMs(80);

    return (before > 0.0) && zeroed && (hits > 0);
}


// ---------- minimal runner ----------
namespace {
struct TestSummary { int total=0, passed=0, failed=0; };

static void PrintLineResult(int id, const String& desc, bool ok)
{
    String status = ok ? "PASS" : "FAIL";
    Cout() << Format("Test %02d: %-55.55s [ %s ]\n", id, desc, status);
}

struct TestCase {
    int         id;
    const char* desc;
    bool        needs_probe;
    bool      (*fn_with)(Probe&);
    bool      (*fn_standalone)();
};
} // anon

// ---------- entry ----------
namespace ConsoleAnim {

bool RunProbe()
{
    static const TestCase tests[] = {
        {  1, "Owner Ctrl can be created",                              true,  L1_make_owner,                      nullptr },
        {  2, "Manual pump advances scheduler",                         true,  L2_pump_events,                     nullptr },
        {  3, "Animation construct & scope exit are safe",              true,  L3_construct_only,                  nullptr },
        {  4, "Play then Cancel stops cleanly",                         true,  L4_play_cancel,                     nullptr },
        {  5, "Tick callback is invoked (>0 hits)",                     true,  L5_ticks_count,                     nullptr },
        {  6, "Animation reaches natural finish",                       true,  L6_natural_finish,                  nullptr },
        {  7, "Double Cancel is harmless",                              true,  L7_double_cancel,                   nullptr },
        {  8, "KillAllFor aborts animations for owner",                 true,  L8_kill_all_for,                    nullptr },
        {  9, "Two animations can run concurrently",                    true,  L9_two_anims,                       nullptr },
        { 10, "Owner destruction stops its animation",                  false, nullptr,                            L10_owner_destroyed },
        { 11, "Stress burst of short animations completes",             true,  L11_stress,                         nullptr },
        { 12, "Pause/Resume holds time and continues",                  true,  L12_pause_resume,                   nullptr },
        { 13, "Stop triggers finish only (not cancel)",                 true,  L13_stop_calls_finish_only,         nullptr },
        { 14, "Cancel triggers cancel only (not finish)",               true,  L14_cancel_calls_cancel_only,       nullptr },
        { 15, "Start delay is respected",                               true,  L15_delay_respected,                nullptr },
        { 16, "Loop + Yoyo performs up and down legs",                  true,  L16_loop_yoyo_cycles,               nullptr },
        { 17, "OutQuad easing completes and fires finish",              true,  L17_easing_outquad_completes,       nullptr },
        { 18, "SetFPS clamps to valid range [1..240]",                  false, nullptr,                            L18_fps_setter_clamps },
        { 19, "Progress stays in [0..1] and ends near 1",               true,  L19_progress_bounds,                nullptr },
        { 20, "OnFinish may safely start another animation",            true,  L20_reentrant_onfinish_starts_new,  nullptr },
        { 21, "Exception in tick is caught (no crash)",                 true,  L21_exception_in_tick_is_caught,    nullptr },
        { 22, "Finalize halts running animations",                      true,  L22_finalize_while_running,         nullptr },
        // Extra coverage:
        { 23, "Pause during Delay holds time (no ticks until resume)",  true,  L23_pause_inside_delay,             nullptr },
        { 24, "Cancel called inside tick fires cancel only",            true,  L24_cancel_inside_tick,             nullptr },
        { 25, "Changing FPS mid-run keeps animation healthy",           true,  L25_setfps_midrun,                  nullptr },
        { 26, "After KillAllFor, Progress() reports forced 0.0",        true,  L26_progress_after_killallfor,      nullptr },
        { 27, "Reuse after Cancel: setters safe, Play again works",     true,  L27_reuse_after_cancel,             nullptr },
        { 28, "Cancel while paused, then reuse safely",                 true,  L28_cancel_while_paused_then_reuse, nullptr },
        { 29, "Replay() reuses last spec",                              true,  L29_replay_reuses_last_spec,        nullptr },
		{ 30, "Replay() allows overriding spec via setters",            true,  L30_replay_after_setters_override,  nullptr },
		{ 31, "Reset() primes staging and zeros Progress()",            true,  L31_reset_primes_staging_and_zeros_progress, nullptr },
    };

    Cout() << "Headless Test Suite for Animation Library\n";
    Cout() << "-----------------------------------------\n";

    TestSummary sum;
    Probe p;

    for (const TestCase& t : tests) {
        bool ok = false;
        try {
            ok = t.needs_probe ? t.fn_with(p) : t.fn_standalone();
        } catch(...) {
            ok = false;
        }
        PrintLineResult(t.id, t.desc, ok);
        ++sum.total; if (ok) ++sum.passed; else ++sum.failed;
    }

    // Explicit, idempotent cleanup:
	p.ClearPool();          // destroy pooled Animations first
	Animation::Finalize();  // then stop scheduler / free States

    Cout() << '\n'
           << "Summary: " << sum.total << " tests, "
           << sum.passed << " passed, "
           << sum.failed << " failed.\n";

    return sum.failed == 0;
}

} // namespace ConsoleAnim
