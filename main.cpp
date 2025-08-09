// AnimationTestSuite.cpp
#include <CtrlLib/CtrlLib.h>
#include <CtrlLib/Animation.h>
using namespace Upp;

#include "clog.h"
#include <crtdbg.h>  // Windows leak check

// ---- pump helper: run the U++ event loop for ms ----
static void PumpFor(int ms) {
    int64 until = msecs() + ms;
    while (msecs() < until) {
        Ctrl::ProcessEvents();
        Sleep(1);
    }
}

// ---- memory leak detector (Windows only) ----
struct MemCheck {
#ifdef _WIN32
    _CrtMemState state1, state2, state3;
    MemCheck() { _CrtMemCheckpoint(&state1); }
    ~MemCheck() {
        _CrtMemCheckpoint(&state2);
        if(_CrtMemDifference(&state3, &state1, &state2)) {
            CLOG << "MEMORY LEAK DETECTED!";
            _CrtMemDumpStatistics(&state3);
        }
    }
#else
    MemCheck() {}
    ~MemCheck() {}
#endif
};

// ---- Test window (not shown, but gives a Ctrl owner) ----
class TestWindow : public TopWindow {
public:
    typedef TestWindow CLASSNAME;

    int   test_value = 0;
    Color test_color = Black();
    Rect  test_rect  = Rect(0, 0, 100, 100);
    Point test_point = Point(0, 0);
    Size  test_size  = Size(100, 100);

    bool  callback_fired = false;
    int   update_count = 0;

    TestWindow() {
        Title("Animation Test Window");
        // We do not call Run(); tests are headless.
        SetRect(Rect(0, 0, 400, 300));
        BackPaint();
        // keep window hidden but constructed so it has a HWND/host as needed
    }

    void Reset() {
        test_value = 0;
        test_color = Black();
        test_rect  = Rect(0, 0, 100, 100);
        test_point = Point(0, 0);
        test_size  = Size(100, 100);
        callback_fired = false;
        update_count = 0;
    }

    void OnStartTest()           { callback_fired = true; }
    void OnFinishTest()          { callback_fired = true; }
    void OnCancelTest()          { callback_fired = true; }
    void OnUpdateTest(double)    { update_count++; }
    void SetTestColor(const Color& c) { test_color = c; }
    void SetTestRect(const Rect& r)   { test_rect  = r; }
};

// ---- shared state for callback-based tests ----
struct TestState {
    bool finish_fired = false;
    bool cancel_fired = false;
    bool update_fired = false;
    int  tick_count   = 0;
    int  cycle_count  = 0;
    Vector<double> values;
    bool completed = false;
    int count1 = 0, count2 = 0, count3 = 0;
};
static TestState* g_state = nullptr;

// ---- free callbacks to satisfy Callback/Callback1 signatures ----
void GlobalOnFinish()        { if(g_state) g_state->finish_fired = true; }
void GlobalOnCancel()        { if(g_state) g_state->cancel_fired = true; }
void GlobalOnUpdate(double)  { if(g_state) g_state->update_fired = true; }

// ---- mini test runner ----
class TestRunner {
    String current_test;
    int passed = 0, failed = 0;
    bool test_started = false;
public:
    void StartTest(const String& name) {
        current_test = name;
        test_started = true;
        std::fprintf(stderr, "TESTING [%s] [", ~name);
        std::fflush(stderr);
    }
    void Progress() { if(test_started) { std::fprintf(stderr, "."); std::fflush(stderr);} }
    void Pass()     { if(test_started) { std::fprintf(stderr, "] PASS\n"); std::fflush(stderr); passed++; test_started=false; } }
    void Fail(const String& reason) {
        if(test_started){ std::fprintf(stderr, "] FAIL - %s\n", ~reason); std::fflush(stderr); failed++; test_started=false; }
    }
    void Summary() {
        std::fprintf(stderr, "\n=======================\n");
        std::fprintf(stderr, "TESTS PASSED: %d\n", passed);
        std::fprintf(stderr, "TESTS FAILED: %d\n", failed);
        std::fprintf(stderr, "TOTAL: %d\n", passed + failed);
        std::fprintf(stderr, "=======================\n");
        std::fflush(stderr);
    }
};

// ---- tests ----
bool TestBasicPlay(TestWindow& w, TestRunner& r) {
    r.StartTest("Basic Play"); MemCheck mem;
    try {
        r.Progress();
        TestState st; g_state = &st;

        Animation anim(w);
        anim([](double) -> bool { if(g_state) g_state->tick_count++; return true; })
            .Duration(100)
            .Play();

        r.Progress();
        PumpFor(160); // > duration

        if(st.tick_count == 0) { r.Fail("Tick function not called"); return false; }
        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestPauseResume(TestWindow& w, TestRunner& r) {
    r.StartTest("Pause/Resume"); MemCheck mem;
    try {
        r.Progress();
        TestState st; g_state = &st;

        Animation anim(w);
        anim([](double) -> bool { if(g_state) g_state->tick_count++; return true; })
            .Duration(220)
            .Play();

        r.Progress();
        PumpFor(60);
        anim.Pause();

        int at_pause = st.tick_count;
        PumpFor(80);          // while paused, count must not change
        if(st.tick_count != at_pause) { r.Fail("Animation continued after pause"); g_state=nullptr; return false; }

        r.Progress();
        anim.Resume();
        PumpFor(200);

        if(st.tick_count == at_pause) { r.Fail("Animation did not resume"); g_state=nullptr; return false; }
        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestCallbacks(TestWindow& w, TestRunner& r) {
    r.StartTest("Callbacks"); MemCheck mem;
    try {
        r.Progress();
        w.callback_fired = false; TestState st; g_state=&st;

        Animation anim(w);
        anim.Duration(100)
            .OnStart(callback(&w, &TestWindow::OnStartTest))
            .OnFinish(callback(GlobalOnFinish))
            .OnUpdate(callback(GlobalOnUpdate))
            .Play();

        r.Progress();
        PumpFor(160);

        if(!w.callback_fired)      { r.Fail("OnStart not fired"); g_state=nullptr; return false; }
        if(!st.finish_fired)       { r.Fail("OnFinish not fired"); g_state=nullptr; return false; }
        if(!st.update_fired)       { r.Fail("OnUpdate not fired"); g_state=nullptr; return false; }

        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestLoop(TestWindow& w, TestRunner& r) {
    r.StartTest("Loop Mode"); MemCheck mem;
    try {
        r.Progress();
        TestState st; g_state=&st;

        Animation anim(w);
        anim([](double p) -> bool {
                if(g_state && p >= 0.99) g_state->cycle_count++;
                return true; // scheduler handles loop_count
            })
            .Duration(50)
            .Loop(3)
            .Play();

        r.Progress();
        PumpFor(220); // 3 * ~50 + overhead

        if(st.cycle_count < 3) { r.Fail(Format("Expected 3 cycles, got %d", st.cycle_count)); g_state=nullptr; return false; }
        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestYoyo(TestWindow& w, TestRunner& r) {
    r.StartTest("Yoyo Mode"); MemCheck mem;
    try {
        r.Progress();
        TestState st; g_state=&st;

        Animation anim(w);
        anim([](double p) -> bool {
                if(g_state) g_state->values.Add(p);
                return true;
            })
            .Duration(120)
            .Yoyo(true)
            .Loop(2)
            .Play();

        r.Progress();
        PumpFor(300);

        bool went_up=false, went_down=false;
        for(int i=1;i<st.values.GetCount();++i) {
            if(st.values[i] > st.values[i-1]) went_up = true;
            if(went_up && st.values[i] < st.values[i-1]) went_down = true;
        }
        if(!went_up || !went_down) { r.Fail("Yoyo pattern not detected"); g_state=nullptr; return false; }

        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestEasing(TestWindow& w, TestRunner& r) {
    r.StartTest("Easing Functions"); MemCheck mem;
    try {
        r.Progress();
        const Easing::Fn* easings[] = {
            &Easing::Linear(),
            &Easing::InQuad(),
            &Easing::OutQuad(),
            &Easing::InOutCubic(),
            &Easing::OutBounce()
        };

        for(auto easing : easings) {
            r.Progress();
            TestState st; g_state=&st;

            Animation anim(w);
            anim([](double p)->bool { if(g_state && p>=0.99) g_state->completed=true; return true; })
                .Duration(60)
                .Ease(*easing)
                .Play();

            PumpFor(120);
            if(!st.completed) { r.Fail("Easing function failed"); g_state=nullptr; return false; }
        }
        r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestValueAnimation(TestWindow& w, TestRunner& r) {
    r.StartTest("Value Animation"); MemCheck mem;
    try {
        r.Progress();

        w.test_color = Black();
        AnimateColor(w, callback(&w, &TestWindow::SetTestColor),
                     Black(), White(), 100);
        PumpFor(160);
        if(w.test_color == Black()) { r.Fail("Color did not animate"); return false; }

        r.Progress();

        Rect start(0,0,100,100), end(50,50,200,200);
        w.test_rect = start;
        AnimateRect(w, callback(&w, &TestWindow::SetTestRect), start, end, 100);
        PumpFor(160);
        if(w.test_rect == start) { r.Fail("Rect did not animate"); return false; }

        r.Progress(); r.Pass(); return true;
    } catch(...) { r.Fail("Exception thrown"); return false; }
}

bool TestMultipleAnimations(TestWindow& w, TestRunner& r) {
    r.StartTest("Multiple Animations"); MemCheck mem;
    try {
        r.Progress();

        TestState st; g_state=&st;

        Animation a1(w); a1([](double){ if(g_state) g_state->count1++; return true; }).Duration(120).Play();
        Animation a2(w); a2([](double){ if(g_state) g_state->count2++; return true; }).Duration(120).Play();
        Animation a3(w); a3([](double){ if(g_state) g_state->count3++; return true; }).Duration(120).Play();

        r.Progress();
        PumpFor(200);

        if(st.count1 == 0 || st.count2 == 0 || st.count3 == 0) { r.Fail("Not all animations ran"); g_state=nullptr; return false; }

        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestCancel(TestWindow& w, TestRunner& r) {
    r.StartTest("Cancel"); MemCheck mem;
    try {
        r.Progress();

        w.callback_fired = false;
        TestState st; g_state=&st;

        Animation anim(w);
        anim.Duration(300)
            .OnCancel(callback(&w, &TestWindow::OnCancelTest))
            .OnFinish(callback(GlobalOnFinish))
            .Play();

        r.Progress();
        PumpFor(80);
        anim.Cancel();
        PumpFor(80);

        if(!w.callback_fired)      { r.Fail("OnCancel not fired"); g_state=nullptr; return false; }
        if(st.finish_fired)        { r.Fail("OnFinish fired after cancel"); g_state=nullptr; return false; }

        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestKillAll(TestWindow& w, TestRunner& r) {
    r.StartTest("KillAll"); MemCheck mem;
    try {
        r.Progress();

        TestState st; g_state=&st;
        int& count = st.tick_count;

        for(int i=0;i<5;i++) {
            Animation a(w);
            a([](double){ if(g_state) g_state->tick_count++; return true; })
                .Duration(500).Play();
        }

        r.Progress();
        PumpFor(60);
        int before = count;

        Animation::KillAll();
        PumpFor(120);

        if(count != before) { r.Fail("Animations continued after KillAll"); g_state=nullptr; return false; }

        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestFPS(TestWindow&, TestRunner& r) {
    r.StartTest("FPS Settings"); MemCheck mem;
    try {
        r.Progress();
        int orig = Animation::GetFPS();

        Animation::SetFPS(30);
        {
            int eff = Animation::GetFPS();
            // expect ~30 (+/-2)
            if(!(eff >= 28 && eff <= 32)) { r.Fail(Format("FPS ~30 expected, got %d", eff)); return false; }
        }

        r.Progress();

        Animation::SetFPS(120);
        {
            int eff = Animation::GetFPS();
            // with 1ms granularity, 8ms -> 125 fps. Accept 115–130.
            if(!(eff >= 115 && eff <= 130)) { r.Fail(Format("FPS ~120 expected, got %d", eff)); return false; }
        }

        // restore
        Animation::SetFPS(orig);
        r.Progress(); r.Pass(); return true;
    } catch(...) { r.Fail("Exception thrown"); return false; }
}


bool TestDelay(TestWindow& w, TestRunner& r) {
    r.StartTest("Delay"); MemCheck mem;
    try {
        r.Progress();

        TestState st; g_state=&st;
        int64 start_time = msecs();

        Animation anim(w);
        anim([start_time](double) -> bool {
                if(g_state && !g_state->completed) {
                    g_state->completed = true;
                    int64 elapsed = msecs() - start_time;
                    if(elapsed < 90) { g_state->tick_count = -1; return false; }
                }
                return true;
            })
            .Duration(50)
            .Delay(100)
            .Play();

        r.Progress();
        PumpFor(220);

        if(!st.completed)            { r.Fail("Animation never started after delay"); g_state=nullptr; return false; }
        if(st.tick_count == -1)      { r.Fail("Started too early"); g_state=nullptr; return false; }

        r.Progress(); r.Pass(); g_state=nullptr; return true;
    } catch(...) { r.Fail("Exception thrown"); g_state=nullptr; return false; }
}

bool TestMemoryStress(TestWindow& w, TestRunner& r) {
    r.StartTest("Memory Stress"); MemCheck mem;
    try {
        r.Progress();
        for(int i=0;i<100;i++) {
            Animation a(w);
            a([](double){ return true; }).Duration(10).Play();
            if(i % 20 == 0) r.Progress();
        }
        PumpFor(120);
        Animation::KillAll();
        r.Progress(); r.Pass(); return true;
    } catch(...) { r.Fail("Exception thrown"); return false; }
}

// ---- suite entry point ----
GUI_APP_MAIN
{
    std::fprintf(stderr, "\nStarting Animation Test Suite | V02\n");
    std::fprintf(stderr, "=======================\n");
    std::fflush(stderr);

    TestWindow w;   // constructed, but we never call w.Run()

    TestRunner runner;

    TestBasicPlay(w, runner);         w.Reset();
    TestPauseResume(w, runner);       w.Reset();
    TestCallbacks(w, runner);         w.Reset();
    TestLoop(w, runner);              w.Reset();
    TestYoyo(w, runner);              w.Reset();
    TestEasing(w, runner);            w.Reset();
    TestValueAnimation(w, runner);    w.Reset();
    TestMultipleAnimations(w, runner);w.Reset();
    TestCancel(w, runner);            w.Reset();
    TestKillAll(w, runner);           w.Reset();
    TestFPS(w, runner);               w.Reset();
    TestDelay(w, runner);             w.Reset();
    TestMemoryStress(w, runner);

    // Clean shutdown
    Animation::ShutdownScheduler();
    runner.Summary();

    Clog::DisableLogging();
}
