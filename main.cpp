#include <CtrlLib/CtrlLib.h>
#include <CtrlLib/Animation.h>

using namespace Upp;

// CurvePlotter is unchanged and correct.
struct CurvePlotter : public Ctrl {
    Easing::Fn ease;
    void Paint(Draw& w) override {
        Size sz = GetSize();
        w.DrawRect(sz, SColorPaper());
        if (!ease) return;
        w.DrawLine(0, sz.cy / 2, sz.cx, sz.cy / 2, 1, LtGray());
        Point p1(0, sz.cy - int(sz.cy * ease(0.0)));
        for (int i = 1; i < sz.cx; i++) {
            double t = (double)i / (sz.cx - 1);
            Point p2(i, sz.cy - int(sz.cy * ease(t)));
            w.DrawLine(p1, p2, 2, SColorHighlight());
            if (i % 15 == 0) w.DrawEllipse(i - 2, p2.y - 2, 5, 5, Red(), 1, Red());
            p1 = p2;
        }
    }
    void SetEasing(Easing::Fn fn) { ease = fn; Refresh(); }
};

// HoverBox is correct.
struct HoverBox : public Ctrl {
    String label;
    Animation hover_anim;
    HoverBox(const String& lbl) : label(lbl) { BackPaint(); }
    void Paint(Draw& w) override {
        w.DrawRect(GetSize(), Blend(Blue, White, 150));
        DrawFrame(w, GetSize(), SColorText());
        Font f = Arial(GetSize().cy / 3).Bold();
        Size tsz = GetTextSize(label, f);
        w.DrawText((GetSize().cx - tsz.cx) / 2, (GetSize().cy - tsz.cy) / 2, label, f, SColorText());
    }
    void MouseEnter(Point, dword) override {
        hover_anim.Stop();
        hover_anim = pick(Animation(*this).Size(Size(90, 90)).Time(300).Ease(Easing::OutBounce));
        hover_anim.Start();
    }
    void MouseLeave() override {
        hover_anim.Stop();
        hover_anim = pick(Animation(*this).Size(Size(80, 80)).Time(300).Ease(Easing::OutBounce));
        hover_anim.Start();
    }
};

// Data arrays are correct.
const static struct { const char* name; Easing::Fn fn; } EasingFunctions[] = {
    { "Linear", Easing::Linear }, { "InQuad", Easing::InQuad }, { "OutQuad", Easing::OutQuad },
    { "InOutQuad", Easing::InOutQuad }, { "OutBounce", Easing::OutBounce },
    { "InCubic", Easing::InCubic }, { "OutCubic", Easing::OutCubic }, { "InOutCubic", Easing::InOutCubic },
    { "InQuart", Easing::InQuart }, { "OutQuart", Easing::OutQuart }, { "InOutQuart", Easing::InOutQuart },
    { "InQuint", Easing::InQuint }, { "OutQuint", Easing::OutQuint }, { "InOutQuint", Easing::InOutQuint },
    { "InSine", Easing::InSine }, { "OutSine", Easing::OutSine }, { "InOutSine", Easing::InOutSine },
    { "InExpo", Easing::InExpo }, { "OutExpo", Easing::OutExpo }, { "InOutExpo", Easing::InOutExpo },
    { "InElastic", Easing::InElastic }, { "OutElastic", Easing::OutElastic }, { "InOutElastic", Easing::InOutElastic },
};
const static struct { const char* name; AnimMode mode; } AnimationModes[] = {
    { "Once", Once }, { "Loop", Loop }, { "Yoyo", Yoyo }
};

class AnimationDemo : public TopWindow {
public:
    typedef AnimationDemo CLASSNAME;
    AnimationDemo();
private:
    void AnimateSlider();
    void PauseSlider();
    void KillAllHandler();
    void UpdateDemos();

    DropList easingList, modeList;
    Button animateButton, pauseButton, killAllButton;
    StaticRect previewBar;
    CurvePlotter curvePlot;
    StaticRect slider, startMarker, endMarker;
    Array<One<HoverBox>> hoverBoxes;
    Animation sliderAnim;
    bool isPaused = false;
};

AnimationDemo::AnimationDemo() {
    Title("U++ Animation Showcase").Sizeable().Zoomable().SetRect(0, 0, 700, 600);

    Add(easingList.TopPos(10, 25).LeftPos(10, 150));
    for(const auto& e : EasingFunctions) easingList.Add(e.name);
    easingList.SetIndex(0);

    Add(modeList.TopPos(10, 25).LeftPos(170, 100));
    for(const auto& m : AnimationModes) modeList.Add(m.name);
    modeList.SetIndex(0);

    Add(animateButton.TopPos(10, 25).LeftPos(280, 120));
    animateButton.SetLabel("Animate Slider");
    Add(pauseButton.TopPos(10, 25).LeftPos(410, 120));
    pauseButton.SetLabel("Pause");
    Add(killAllButton.TopPos(10, 25).LeftPos(540, 120));
    killAllButton.SetLabel("Kill All");

    previewBar.Color(Blend(Green, White, 100));
    Add(previewBar.TopPos(45, 10).HSizePos(10, 10));
    
    Add(curvePlot.TopPos(65, 120).HSizePos(10, 10));

    int slider_y = 200, slider_h = 50;
    startMarker.Color(LtGray());
    endMarker.Color(LtGray());
    slider.Color(Blue());
    Add(startMarker.TopPos(slider_y, slider_h).LeftPos(50, 1));
    Add(endMarker.TopPos(slider_y, slider_h).RightPos(50, 1));
    Add(slider.TopPos(slider_y, slider_h).LeftPos(50, 50));

    for(int i = 0; i < 6; i++) {
        One<HoverBox>& box_owner = hoverBoxes.Add(new HoverBox(AsString(i+1)));
        Add((*box_owner).BottomPos(10, 80).LeftPos(10 + i * 95, 80));
    }

    easingList.WhenAction << THISBACK(UpdateDemos);
    modeList.WhenAction   << THISBACK(UpdateDemos);
    animateButton         << THISBACK(AnimateSlider);
    pauseButton           << THISBACK(PauseSlider);
    killAllButton         << THISBACK(KillAllHandler);

    UpdateDemos();
}

void AnimationDemo::UpdateDemos() {
    KillAll();
    
    Easing::Fn ease = EasingFunctions[easingList.GetIndex()].fn;
    AnimMode mode = AnimationModes[modeList.GetIndex()].mode;
    curvePlot.SetEasing(ease);
    
    Rect r = previewBar.GetRect();
    Rect from = r; from.right = from.left;
    
    Animation a(previewBar);
    a.Rect(r).Time(1500).Ease(ease);
    if(mode == Loop) a.Count(-1);
    else if(mode == Yoyo) a.Yoyo();
    a.Start();
    
    KillAllHandler();
}

void AnimationDemo::AnimateSlider() {
    sliderAnim.Stop();
    Easing::Fn ease = EasingFunctions[easingList.GetIndex()].fn;
    AnimMode mode = AnimationModes[modeList.GetIndex()].mode;
    Rect from = startMarker.GetRect();
    Rect to = endMarker.GetRect();
    to.SetSize(slider.GetSize());
    slider.SetRect(from);
    sliderAnim = pick(Animation(slider));
    sliderAnim.Rect(to).Time(1200).Ease(ease);
    if(mode == Loop) sliderAnim.Count(-1);
    else if(mode == Yoyo) sliderAnim.Yoyo();
    sliderAnim.Start();
    isPaused = false;
    
    pauseButton.SetLabel("Pause");
    // FIX: Reset the button to its default look using the standard style.
    pauseButton.SetStyle(Button::StyleNormal());
}

void AnimationDemo::PauseSlider() {
    isPaused = !isPaused;
    if (isPaused) {
        sliderAnim.Pause();
        pauseButton.SetLabel("Continue");

        // FIX: Create a custom style to change the button's color.
        Button::Style st = Button::StyleNormal(); // Get a copy of the normal style.
        st.look[0] = LtRed(); // Normal state
        st.look[1] = Blend(LtRed, White, 40); // Hot/Hover state
        st.look[2] = Blend(LtRed, Black, 40); // Pushed state
        pauseButton.SetStyle(st); // Apply the custom style.

    } else {
        sliderAnim.Resume();
        pauseButton.SetLabel("Pause");
        // FIX: Reset the button to its default look.
        pauseButton.SetStyle(Button::StyleNormal());
    }
}

void AnimationDemo::KillAllHandler() {
    KillAll();
    slider.SetRect(startMarker.GetRect());
    isPaused = false;
    pauseButton.SetLabel("Pause");
    // FIX: Reset the button to its default look.
    pauseButton.SetStyle(Button::StyleNormal());
}

GUI_APP_MAIN
{
    AnimationDemo().Run();
    LOG("Application finished cleanly.");
}