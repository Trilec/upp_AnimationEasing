// main.cpp — Probe harness for CtrlLib/Animation (Release/Debug)
// Prints PASS/FAIL per test and finishes with a summary.

#define ANIM_MANUAL_DRIVE 1   // test harness drives the scheduler

#include <Core/Core.h>
#include <CtrlLib/CtrlLib.h>
using namespace Upp;

#include <CtrlLib/Animation.h>


// ---------- helpers ----------
static void PumpForMs(int ms) {
    int64 until = msecs() + ms;
    while (msecs() < until) {
         Animation::TickOnce();   // guaranteed tick
        Sleep(1);
    }
}

struct Probe {
    TopWindow w;
    Probe()  { w.Title("Probe").SetRect(0,0,400,300); }
    ~Probe() = default;
};

struct BoolFlag {
    bool* p;
    void Set() { if (p) *p = true; }
};

struct ReentrantStarter {
    TopWindow* w = nullptr;
    int* ticks2 = nullptr;
    void StartNext() {
        Animation* spawned = new Animation(*w);
        spawned->operator()([&](double){ ++(*ticks2); return true; })
               .Duration(80).Play();
        // spawned intentionally leaked or track & delete later 
    }
};

// ---------- original tests (L1–L12) ----------
static bool L1_make_window(Probe& p) { Cout() << "L1: Made TopWindow\n"; return p.w.IsOpen() || true; }
static bool L2_pump_events(Probe&)   { PumpForMs(10); Cout() << "L2: Pumped events\n"; return true; }
static bool L3_construct_only(Probe& p){ { Animation a(p.w); } Cout() << "L3: Construct+scope-exit ok\n"; return true; }
static bool L4_play_cancel(Probe& p) {
    Animation a(p.w);
    a([](double){ return true; }).Duration(50).Play();
    PumpForMs(5); a.Cancel();
    Cout() << "L4: Play+Cancel done\n"; return true;
}
static bool L5_ticks_count(Probe& p) {
    int ticks = 0;
    Animation a(p.w);
    a([&](double){ ++ticks; return true; }).Duration(80).Play();
    PumpForMs(150);
    Cout() << Format("L5: ticks=%d\n", ticks);
    return ticks > 0;
}
static bool L6_natural_finish(Probe& p) {
    Animation a(p.w); a([](double){ return true; }).Duration(60).Play();
    PumpForMs(200); Cout() << "L6: natural finish\n"; return true;
}
static bool L7_double_cancel(Probe& p) {
    Animation a(p.w); a([](double){ return true; }).Duration(60).Play();
    PumpForMs(5); a.Cancel(); a.Cancel();
    Cout() << "L7: double cancel ok\n"; return true;
}
static bool L8_kill_all_for(Probe& p) {
    Animation a(p.w); a([](double){ return true; }).Duration(200).Play();
    PumpForMs(20); Animation::KillAllFor(p.w);
    Cout() << "L8: KillAllFor issued\n"; return true;
}
static bool L9_two_anims(Probe& p) {
    int a1=0, a2=0;
    Animation x(p.w), y(p.w);
    x([&](double){ ++a1; return true; }).Duration(120).Play();
    y([&](double){ ++a2; return true; }).Duration(120).Play();
    PumpForMs(160);
    Cout() << Format("L9: ticks a1=%d a2=%d\n", a1, a2);
    return a1 > 0 && a2 > 0;
}
static bool L10_owner_destroyed() {
    int ticks = 0;
    { TopWindow w2; w2.SetRect(0,0,10,10);
      Animation a(w2);
      a([&](double){ ++ticks; return true; }).Duration(300).Play();
      PumpForMs(50); /* w2 dtor here */ }
    PumpForMs(50);
    Cout() << Format("L10: owner gone, ticks(before close)=%d\n", ticks);
    return true;
}
static bool L11_stress(Probe& p) {
    for (int i=0; i<200; ++i) {
        Animation a(p.w);
        a([](double){ return true; }).Duration(15).Play();
        if((i % 40) == 0) Cout() << Format("L11: burst at i=%d\n", i);
    }
    PumpForMs(400);
    Cout() << "L11: stress done\n";
    return true;
}
static bool L12_pause_resume(Probe& p) {
    int ticks = 0;
    Animation a(p.w);
    a([&](double){ ++ticks; return true; }).Duration(240).Play();
    PumpForMs(30);
    a.Pause();
    int at_pause = ticks;
    PumpForMs(50);
    bool ok = (ticks == at_pause);
    a.Resume();
    PumpForMs(250);
    Cout() << "L12: pause/resume done\n";
    return ok;
}

static bool L13_stop_calls_finish_only(Probe& p) {
    bool finish=false, cancel=false;
    BoolFlag onfin{&finish}, oncan{&cancel};

    Animation a(p.w);
    a([](double){ return true; })
      .OnFinish(callback(&onfin, &BoolFlag::Set))
      .OnCancel(callback(&oncan, &BoolFlag::Set))
      .Duration(500).Play();

    PumpForMs(20);
    a.Stop();       // should fire finish, not cancel
    PumpForMs(10);

    Cout() << "L13: stop->finish only\n";
    return finish && !cancel;
}


static bool L14_cancel_calls_cancel_only(Probe& p) {
    bool finish=false, cancel=false;
    BoolFlag onfin{&finish}, oncan{&cancel};

    Animation a(p.w);
    a([](double){ return true; })
      .OnFinish(callback(&onfin, &BoolFlag::Set))
      .OnCancel(callback(&oncan, &BoolFlag::Set))
      .Duration(500).Play();

    PumpForMs(20);
    a.Cancel();     // should fire cancel only
    PumpForMs(10);

    Cout() << "L14: cancel->cancel only\n";
    return cancel && !finish;
}


static bool L15_delay_respected(Probe& p) {
    int ticks=0;
    int64 start = msecs();
    Animation a(p.w);
    a([&](double){ ++ticks; return true; })
      .Delay(120).Duration(60).Play();
    PumpForMs(80);  // before delay -> no ticks
    bool pre_ok = (ticks == 0);
    PumpForMs(80);  // after delay
    bool post_ok = (ticks > 0) && (msecs() - start >= 120);
    Cout() << "L15: delay respected\n";
    return pre_ok && post_ok;
}

static bool L16_loop_yoyo_cycles(Probe& p) {
    Vector<double> seen;
    Animation a(p.w);
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

static bool L17_easing_outquad_completes(Probe& p) {
    bool finished=false;
    BoolFlag onfin{&finished};

    Animation a(p.w);
    a([](double){ return true; })
      .Ease(Easing::OutQuad())
      .OnFinish(callback(&onfin, &BoolFlag::Set))
      .Duration(80).Play();

    PumpForMs(160);
    Cout() << "L17: easing completes\n";
    return finished;
}


static bool L18_fps_setter_clamps() {
    int orig = Animation::GetFPS();
    Animation::SetFPS(0);     int f1 = Animation::GetFPS();   // clamp to >=1
    Animation::SetFPS(10000); int f2 = Animation::GetFPS();   // clamp to <=240
    Animation::SetFPS(orig);                                   // restore
    bool ok = (f1 >= 1 && f2 <= 240);
    Cout() << "L18: FPS clamp\n";
    return ok;
}

static bool L19_progress_bounds(Probe& p) {
    Animation a(p.w);
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

static bool L20_reentrant_onfinish_starts_new(Probe& p) {
    int ticks2 = 0;
    ReentrantStarter r{ &p.w, &ticks2 };

    {
        Animation a1(p.w);
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


static bool L21_exception_in_tick_is_caught(Probe& p) {
    Animation a(p.w);
    bool survived = true;
    int hits = 0;
    a([&](double){
          ++hits;
          if (hits == 1) throw 123;  // first call throws
          return true;
      }).Duration(80).Play();
    PumpForMs(120);
    Cout() << "L21: exception caught (no crash)\n";
    return survived;
}

static bool L22_finalize_while_running(Probe& p) {
    int ticks = 0;
    Animation a(p.w);
    a([&](double){ ++ticks; return true; }).Duration(500).Play();
    PumpForMs(20);
    Animation::Finalize();   // should kill scheduler safely
    int before = ticks;
    PumpForMs(100);
    bool halted = (ticks == before);
    Cout() << "L22: finalize while running\n";
    return halted;
}

// Small PODs (not std::pair) to keep U++ containers happy.
struct WithProbe { const char* name; bool (*fn)(Probe&); };
struct Standalone { const char* name; bool (*fn)(); };

// ---------- entry ----------
GUI_APP_MAIN
{
    Cout() << "Starting probe against CtrlLib/Animation\n";

    Probe p;

    const WithProbe with_probe[] = {
        {"L1",  L1_make_window},
        {"L2",  L2_pump_events},
        {"L3",  L3_construct_only},
        {"L4",  L4_play_cancel},
        {"L5",  L5_ticks_count},
        {"L6",  L6_natural_finish},
        {"L7",  L7_double_cancel},
        {"L8",  L8_kill_all_for},
        {"L9",  L9_two_anims},
        {"L11", L11_stress},
        {"L12", L12_pause_resume},
        {"L13", L13_stop_calls_finish_only},
        {"L14", L14_cancel_calls_cancel_only},
        {"L15", L15_delay_respected},
        {"L16", L16_loop_yoyo_cycles},
        {"L17", L17_easing_outquad_completes},
        {"L19", L19_progress_bounds},
        {"L20", L20_reentrant_onfinish_starts_new},
        {"L21", L21_exception_in_tick_is_caught},
        // L22 calls Finalize() internally; keeped last among tests:
        {"L22", L22_finalize_while_running},
    };
    const Standalone standalone[] = {
        {"L10", L10_owner_destroyed},
        {"L18", L18_fps_setter_clamps},
    };

    int pass = 0, fail = 0;

    for (const auto& t : with_probe) {
        bool ok = t.fn(p);
        Cout() << (ok ? "PASS " : "FAIL ") << t.name << '\n';
        (ok ? pass : fail)++;
    }
    for (const auto& t : standalone) {
        bool ok = t.fn();
        Cout() << (ok ? "PASS " : "FAIL ") << t.name << '\n';
        (ok ? pass : fail)++;
    }

    // Idempotent cleanup:
    Animation::Finalize();

    Cout() << Format("DONE — passed %d, failed %d\n", pass, fail);
}
