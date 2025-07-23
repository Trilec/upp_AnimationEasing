#include "Animation.h"

namespace Upp {

// The global list of all active animations
static Vector<One<Animation::State>>& sAnimations() {
    return Single<Vector<One<Animation::State>>>();
}

// The single global timer callback that drives all animations
void AnimationTimer() {
    int64 now = GetTimeClick();
    auto& animations = sAnimations();
    bool active = false;

    for (int i = 0; i < animations.GetCount();) {
        Animation::State* s = animations[i].Get();
        if (!s->owner || !s->owner->IsOpen()) {
            animations.Remove(i);
            continue;
        }
        if (s->paused) {
            i++;
            active = true; // Keep timer alive if any animation is paused
            continue;
        }

        double progress = s->time_ms > 0 ? clamp(double(now - s->start_time) / s->time_ms, 0.0, 1.0) : 1.0;
        if (s->reverse) progress = 1.0 - progress;

        if (!s->tick(s->ease(progress))) {
            animations.Remove(i);
            continue;
        }

        if (now - s->start_time >= s->time_ms) {
            if (s->count > 0) s->count--;
            if (s->count == 0) {
                s->tick(s->ease(s->reverse ? 0.0 : 1.0)); // Ensure final state
                animations.Remove(i);
                continue;
            }
            if (s->yoyo) s->reverse = !s->reverse;
            s->start_time = GetTimeClick();
        }
        i++;
    }

    if (animations.GetCount() > 0 || active) {
        SetTimeCallback(0, &AnimationTimer);
    }
}

void Animation::State::Cancel() {
    auto& animations = sAnimations();
    for (int i = 0; i < animations.GetCount(); i++) {
        if (animations[i].Get() == this) {
            animations.Remove(i);
            break;
        }
    }
}

// --- Animation Class Implementation ---

void Animation::Start() {
    if (s_owner) {
        s_handle->start_time = GetTimeClick();
        s_handle->reverse = false;
        s_handle->paused = false;
        s_handle->elapsed_time = 0;
        auto& animations = sAnimations();
        if (animations.IsEmpty()) SetTimeCallback(0, &AnimationTimer);
        animations.Add(pick(s_owner));
    }
}

void Animation::Stop() {
    if (s_handle) {
        s_handle->Cancel();
        s_handle = nullptr;
    }
}

void Animation::Pause() {
    if (s_handle && !s_handle->paused) {
        s_handle->paused = true;
        s_handle->elapsed_time = GetTimeClick() - s_handle->start_time;
    }
}

void Animation::Resume() {
    if (s_handle && s_handle->paused) {
        s_handle->paused = false;
        s_handle->start_time = GetTimeClick() - s_handle->elapsed_time;
    }
}

void KillAll()
{
    sAnimations().Clear(); // Clearing the list stops all timers and cleans up memory.
}

Animation& Animation::Rect(const Upp::Rect& r) {
    if (!s_handle) return *this;
    Upp::Rect from = s_handle->owner->GetRect();
    s_handle->tick = [=](double t) {
        if (!s_handle->owner) return false;
        s_handle->owner->SetRect(Lerp(from, r, t));
        return true;
    };
    return *this;
}

Animation& Animation::Pos(const Upp::Point& p) {
    if (!s_handle) return *this;
    Upp::Rect from = s_handle->owner->GetRect();
    s_handle->tick = [=](double t) {
        if (!s_handle->owner) return false;
        Point new_tl = Lerp(from.TopLeft(), p, t);
        s_handle->owner->SetRect(new_tl.x, new_tl.y, from.Width(), from.Height());
        return true;
    };
    return *this;
}

Animation& Animation::Size(const Upp::Size& sz) {
    if (!s_handle) return *this;
    Upp::Rect from = s_handle->owner->GetRect();
    s_handle->tick = [=](double t) {
        if (!s_handle->owner) return false;
        Upp::Size new_sz = Lerp(from.GetSize(), sz, t);
        Point center = from.CenterPoint();
        s_handle->owner->SetRect(center.x - new_sz.cx / 2, center.y - new_sz.cy / 2, new_sz.cx, new_sz.cy);
        return true;
    };
    return *this;
}

Animation& Animation::Alpha(double from, double to) {
    if (!s_handle) return *this;
    s_handle->tick = [=](double t) {
        if (!s_handle->owner) return false;
        s_handle->owner->SetAlpha(int(255 * Lerp(from, to, t)));
        return true;
    };
    return *this;
}


} // namespace Upp