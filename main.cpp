// File: examples/AnimateEasing/main.cpp
#include <CtrlLib/Animation.h>
#include <CtrlLib/CtrlLib.h>

using namespace Upp;

struct CurvePlotter : public Ctrl {
	Easing::Fn ease;
	void Paint(Draw& w) override
	{
		Size sz = GetSize();
		w.DrawRect(sz, SColorPaper());
		if(!ease)
			return;
		w.DrawLine(0, sz.cy / 2, sz.cx, sz.cy / 2, 1, LtGray());
		Point p1(0, sz.cy - int(sz.cy * ease(0.0)));
		for(int i = 1; i < sz.cx; i++) {
			double t = double(i) / (sz.cx - 1);
			Point p2(i, sz.cy - int(sz.cy * ease(t)));
			w.DrawLine(p1, p2, 2, SColorHighlight());
			if(i % 15 == 0)
				w.DrawEllipse(i - 2, p2.y - 2, 5, 5, Red(), 1, Red());
			p1 = p2;
		}
	}
	void SetEasing(Easing::Fn fn)
	{
		ease = fn;
		Refresh();
	}
};

struct HoverBox : public Ctrl {
	String label;
	Animation hover_anim;
	HoverBox(const String& lbl)
		: label(lbl)
	{
		BackPaint();
	}

	void Paint(Draw& w) override
	{
		w.DrawRect(GetSize(), Blend(Blue, White, 150));
		DrawFrame(w, GetSize(), SColorText());
		Font f = Arial(GetSize().cy / 3).Bold();
		Size tsz = GetTextSize(label, f);
		w.DrawText((GetSize().cx - tsz.cx) / 2, (GetSize().cy - tsz.cy) / 2, label, f,
		           SColorText());
	}

	void MouseEnter(Point, dword) override
	{
		hover_anim.Stop();
		hover_anim.Rebuild(*this, [&](Animation& a) {
			a.Size(Upp::Size(90, 90)).Time(300).Ease(Easing::OutBounce);
		});
		hover_anim.Start();
	}

	void MouseLeave() override
	{
		hover_anim.Stop();
		hover_anim.Rebuild(*this, [&](Animation& a) {
			a.Size(Upp::Size(80, 80)).Time(300).Ease(Easing::OutBounce);
		});
		hover_anim.Start();
	}
};

// Easing / modes tables
static const struct {
	const char* name;
	Easing::Fn fn;
} EasingFunctions[] = {
	{"Linear", Easing::Linear},
	{"InQuad", Easing::InQuad},
	{"OutQuad", Easing::OutQuad},
	{"InOutQuad", Easing::InOutQuad},
	{"OutBounce", Easing::OutBounce},
	{"InCubic", Easing::InCubic},
	{"OutCubic", Easing::OutCubic},
	{"InOutCubic", Easing::InOutCubic},
	{"InQuart", Easing::InQuart},
	{"OutQuart", Easing::OutQuart},
	{"InOutQuart", Easing::InOutQuart},
	{"InQuint", Easing::InQuint},
	{"OutQuint", Easing::OutQuint},
	{"InOutQuint", Easing::InOutQuint},
	{"InSine", Easing::InSine},
	{"OutSine", Easing::OutSine},
	{"InOutSine", Easing::InOutSine},
	{"InExpo", Easing::InExpo},
	{"OutExpo", Easing::OutExpo},
	{"InOutExpo", Easing::InOutExpo},
	{"InElastic", Easing::InElastic},
	{"OutElastic", Easing::OutElastic},
	{"InOutElastic", Easing::InOutElastic},
};

static const struct {
	const char* name;
	AnimMode mode;
} AnimationModes[] = {{"Once", Once}, {"Loop", Loop}, {"Yoyo", Yoyo}};

// Demo window
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

	Animation previewAnim;
	Animation sliderAnim;
	bool isPaused = false;
};

AnimationDemo::AnimationDemo()
{
	Title("U++ Animation Showcase").Sizeable().Zoomable().SetRect(0, 0, 700, 600);

	Add(easingList.TopPos(10, 25).LeftPos(10, 150));
	for(auto& e : EasingFunctions)
		easingList.Add(e.name);
	easingList.SetIndex(0);

	Add(modeList.TopPos(10, 25).LeftPos(170, 100));
	for(auto& m : AnimationModes)
		modeList.Add(m.name);
	modeList.SetIndex(0);

	animateButton.SetLabel("Animate Slider");
	Add(animateButton.TopPos(10, 25).LeftPos(280, 120));
	pauseButton.SetLabel("Pause");
	Add(pauseButton.TopPos(10, 25).LeftPos(410, 120));
	killAllButton.SetLabel("Kill All");
	Add(killAllButton.TopPos(10, 25).LeftPos(540, 120));

	previewBar.Color(Blend(Green, White, 100));
	Add(previewBar.TopPos(45, 10).HSizePos(10, 10));

	Add(curvePlot.TopPos(65, 120).HSizePos(10, 10));

	int sy = 200, sh = 50;
	startMarker.Color(LtGray());
	endMarker.Color(LtGray());
	slider.Color(Blue());
	Add(startMarker.TopPos(sy, sh).LeftPos(50, 1));
	Add(endMarker.TopPos(sy, sh).RightPos(50, 1));
	Add(slider.TopPos(sy, sh).LeftPos(50, 50));

	for(int i = 0; i < 6; i++) {
		One<HoverBox>& hb = hoverBoxes.Add(new HoverBox(AsString(i + 1)));
		Add(hb->BottomPos(10, 80).LeftPos(10 + i * 95, 80));
	}

	easingList.WhenAction << THISBACK(UpdateDemos);
	modeList.WhenAction << THISBACK(UpdateDemos);
	animateButton << THISBACK(AnimateSlider);
	pauseButton << THISBACK(PauseSlider);
	killAllButton << THISBACK(KillAllHandler);

	UpdateDemos();
}

void AnimationDemo::UpdateDemos()
{
	Easing::Fn ease = EasingFunctions[easingList.GetIndex()].fn;
	AnimMode mode = AnimationModes[modeList.GetIndex()].mode;

	curvePlot.SetEasing(ease);

	Upp::Rect r = previewBar.GetRect();

	previewAnim.Stop();
	previewAnim.Rebuild(previewBar, [&](Animation& a) {
		a.Rect(r).Time(1500).Ease(ease);
		if(mode == Loop)
			a.Count(-1);
		else if(mode == Yoyo)
			a.Yoyo();
	});
	previewAnim.Start();
}

void AnimationDemo::AnimateSlider()
{
	sliderAnim.Stop();

	Easing::Fn ease = EasingFunctions[easingList.GetIndex()].fn;
	AnimMode mode = AnimationModes[modeList.GetIndex()].mode;

	Upp::Rect sr = startMarker.GetRect();
	Upp::Rect er = endMarker.GetRect();
	Upp::Rect toR(er.TopLeft(), sr.GetSize());
	slider.SetRect(sr);

	sliderAnim.Rebuild(slider, [&](Animation& a) {
		a.Rect(toR).Time(1200).Ease(ease);
		if(mode == Loop)
			a.Count(-1);
		else if(mode == Yoyo)
			a.Yoyo();
	});
	sliderAnim.Start();

	isPaused = false;
	pauseButton.SetLabel("Pause");
	pauseButton.SetStyle(Button::StyleNormal());
}

void AnimationDemo::PauseSlider()
{
	// Because Pause/Resume/IsPlaying were stubbed out, disable the button or implement them
	// later. For now, simply do nothing to avoid crashes.
	PromptOK("Pause/Resume not implemented in this pattern yet.");
}

void AnimationDemo::KillAllHandler()
{
	KillAll();
	slider.SetRect(startMarker.GetRect());
	isPaused = false;
	pauseButton.SetLabel("Pause");
	pauseButton.SetStyle(Button::StyleNormal());
}

GUI_APP_MAIN { AnimationDemo().Run(); }
