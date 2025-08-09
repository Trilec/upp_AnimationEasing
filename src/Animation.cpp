#include "Animation.h"

#define CLOG_ENABLED 1
#include "clog.h"

using namespace Upp;

/*==================== configuration ====================*/
static int g_step_ms = 1000 / 60;
static inline int ClampFPS(int fps) { return clamp(fps, 1, 240); }

/*==================== 2. Scheduler ====================*/
namespace {
struct Scheduler {
    static Scheduler& Inst()
    {
        static Scheduler* s = new Scheduler; // never destroyed
        return *s;
    }

    ~Scheduler() noexcept {}

    Vector<Ptr<Animation::State>> active;
    TimeCallback ticker;
    bool running = false;

    void Start() {
        CLOG << "Scheduler::Start()";
        if(!running) {
            running = true;
    
            ticker.Set(g_step_ms, [=] { Tick(); });
           
        }
    }

    void Stop() {
        CLOG << "Scheduler::Stop()";
        ticker.Kill();
        running = false;
    }

    void Add(Ptr<Animation::State> s) {
       
        active.Add(s);
        
        Start();
    }

    void Remove(Animation::State* st) {
        
        for(int i = 0; i < active.GetCount(); ++i)
            if(active[i] == st) {
                active.Remove(i);
               
                break;
            }
        if(active.IsEmpty()) Stop();
    }

    void Tick() {
      // CLOG << "Scheduler::Tick() begin count=" << active.GetCount();
        int64 now = msecs();
        for(int i = 0; i < active.GetCount();) {
            try {
                Ptr<Animation::State> p = active[i];
                if(!p) { active.Remove(i); continue; }
                bool cont = p->Step(now);
                if(!cont) {  active.Remove(i); }
                else ++i;
            } catch(...) {
               
                active.Remove(i);
            }
        }
        if(!active.IsEmpty()) {
            ticker.Set(g_step_ms, [=] { Tick(); });
            
        } else {
            
            Stop();
        }
    }

    void KillAll() {
        
        Stop();
        active.Clear();
       
    }

    void KillFor(Ctrl* c) {
       
        for(int i = active.GetCount() - 1; i >= 0; --i)
            if(!active[i] || active[i]->owner == c)
                active.Remove(i);
        
        if(active.IsEmpty()) Stop();
    }

    // NEW
    void Shutdown() {
       
        Stop();
        active.Clear();
        
    }
};
} // namespace

/*==================== 3. Animation impl ====================*/
Animation::Animation(Ctrl& owner)
    : owner_(&owner) // plain raw pointer
{
    
    spec_box_.Create();
    spec_ = ~spec_box_;
}

Animation::~Animation()
{
   
    // Do NOT touch the scheduler here. At app shutdown it is already stopped/cleared.
    // Just drop our Ptr. The State will be owned by the scheduler while running,
    live_ = nullptr;
}


void Animation::ShutdownScheduler() {
    
    Scheduler::Inst().Shutdown();
}

#define RET(expr) \
    do { expr; return *this; } while(0)

Animation& Animation::Duration(int ms) { RET(spec_->duration_ms = ms); }

// Easing
Animation& Animation::Ease(const Easing::Fn& fn) { RET(spec_->easing = fn); }
Animation& Animation::Ease(Easing::Fn&& fn)      { RET(spec_->easing = pick(fn)); }

// Loop, Yoyo, Delay
Animation& Animation::Loop(int n)     { RET(spec_->loop_count = n); }
Animation& Animation::Yoyo(bool b)    { RET(spec_->yoyo = b); }
Animation& Animation::Delay(int ms)   { RET(spec_->delay_ms = ms); }

// Callbacks
Animation& Animation::OnStart(const Callback& cb) { RET(spec_->on_start = cb); }
Animation& Animation::OnStart(Callback&& cb)      { RET(spec_->on_start = pick(cb)); }

Animation& Animation::OnFinish(const Callback& cb) { RET(spec_->on_finish = cb); }
Animation& Animation::OnFinish(Callback&& cb)      { RET(spec_->on_finish = pick(cb)); }

Animation& Animation::OnCancel(const Callback& cb) { RET(spec_->on_cancel = cb); }
Animation& Animation::OnCancel(Callback&& cb)      { RET(spec_->on_cancel = pick(cb)); }

Animation& Animation::OnUpdate(const Callback1<double>& c) { RET(spec_->on_update = c); }
Animation& Animation::OnUpdate(Callback1<double>&& c)      { RET(spec_->on_update = pick(c)); }

// Tick function
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
    if(!spec_) {  return; }

    live_ = new State;
    live_->owner = owner_;
    live_->spec = pick(*spec_);
    spec_ = nullptr;

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
    Cancel();
}

void Animation::Cancel()
{
    
    if(!live_)
        return;

    if(live_->spec.on_cancel)
        live_->spec.on_cancel();

    Scheduler::Inst().Remove(live_); // scheduler deletes it exactly once
    live_ = nullptr;                 // we no longer reference it
}

bool Animation::IsPlaying() const { return live_ && !live_->paused; }
bool Animation::IsPaused() const  { return live_ && live_->paused; }

double Animation::Progress() const
{
    if(!live_)
        return 0.0;
    int64 run = live_->elapsed_ms + (live_->paused ? 0 : (msecs() - live_->start_ms));
    run = max<int64>(0, run - live_->spec.delay_ms);
    return clamp(double(run) / live_->spec.duration_ms, 0.0, 1.0);
}

/* fps */
void Animation::SetFPS(int fps) { g_step_ms = 1000 / ClampFPS(fps); }
int  Animation::GetFPS()        { return 1000 / g_step_ms; }

/* global */
void Animation::KillAll()          { Scheduler::Inst().KillAll(); }
void Animation::KillAllFor(Ctrl& c){ Scheduler::Inst().KillFor(&c); }
