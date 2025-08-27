// GUIAnim.cpp — “Animation Lab” showing all demo animations at once.
// Native U++ UI, conservative API usage.
//
// Demos (each in its own canvas):
//   1) Ball (Side to Side)       2) Pulsing Text
//   3) Fading Element            4) Hover Box (Scale)
//   5) Pulsing Points            6) Color Change
//   7) Rotating Square           8) Hovering Boxes (interactive)
//
// 2025-08-19 — initial GUI lab; fixes: RGBA alpha, no WinAPI SetCursor,
//              in-place Vector construction, Box Moveable, caption lifetime,
//              One<Animation> call chain split.

#include <CtrlLib/Animation.h>
#include <CtrlLib/CtrlLib.h>
using namespace Upp;

namespace GUIAnim {

// ---------- Easing map ----------
struct EaseItem {
	const char* name;
	const Easing::Fn* fn;
};
static const EaseItem kEases[] = {
	{"Linear", &Easing::Linear()},       {"InQuad", &Easing::InQuad()},
	{"OutQuad", &Easing::OutQuad()},     {"InOutCubic", &Easing::InOutCubic()},
	{"OutBounce", &Easing::OutBounce()},
};

// ---------- Demo kinds ----------
enum DemoKind {
	DEMO_BALL = 0,
	DEMO_TEXT,
	DEMO_FADE,
	DEMO_BOX_SCALE,
	DEMO_POINTS,
	DEMO_COLOR,
	DEMO_ROTATE,
	DEMO_HOVER_BOXES,
};

// ---------- Simple draw surface ----------
class CanvasCtrl : public Ctrl {
public:
	DemoKind kind = DEMO_BALL;
	double eased = 0.0; // 0..1
	const Easing::Fn* ease = &Easing::InOutCubic();

	// hover-boxes state
	struct Box : Moveable<Box> {
		Rect r;
		double scale = 1.0;
	};
	Vector<Box> hover_boxes;
	bool hover_anim_running = false;
	int hover_idx = -1;
	int64 hover_start_ms = 0;

	void SetKind(DemoKind k)
	{
		kind = k;
		Refresh();
	}
	void Set(double e)
	{
		eased = clamp(e, 0.0, 1.0);
		Refresh();
	}
	void SetEasing(const Easing::Fn* fn)
	{
		ease = fn ? fn : &Easing::Linear();
		Refresh();
	}

	// build hover boxes layout based on current size
	void BuildHoverBoxes()
	{
		hover_boxes.Clear();
		Size sz = GetSize();
		int num = 5, size = 60, gap = 15;
		int total = num * size + (num - 1) * gap;
		int x0 = (sz.cx - total) / 2;
		int y = (sz.cy - size) / 2;
		if(x0 < 0) {
			size = 40;
			gap = 10;
			total = num * size + (num - 1) * gap;
			x0 = (sz.cx - total) / 2;
		}
		for(int i = 0; i < num; i++) {
			Rect r = RectC(x0 + i * (size + gap), y, size, size);
			Box b;
			b.r = r;
			b.scale = 1.0;
			hover_boxes.Add(b);
		}
	}

	virtual void Layout() override
	{
		if(kind == DEMO_HOVER_BOXES)
			BuildHoverBoxes();
	}

	virtual void MouseMove(Point p, dword) override
	{
		if(kind != DEMO_HOVER_BOXES)
			return;
		int hit = -1;
		for(int i = 0; i < hover_boxes.GetCount(); ++i)
			if(hover_boxes[i].r.Contains(p)) {
				hit = i;
				break;
			}
		if(hit >= 0 && !hover_anim_running) {
			hover_anim_running = true;
			hover_idx = hit;
			hover_start_ms = msecs();
			Refresh(); // Paint will advance the micro animation
		}
		// (Cursor change intentionally omitted; use a proper U++ cursor API later.)
	}

	virtual void Paint(Draw& w) override
	{
		const Size sz = GetSize();
		w.DrawRect(sz, SColorFace());

		auto get_eased = [&](double t) { return ease ? (*ease)(t) : t; };
		const double e = get_eased(eased);

		switch(kind) {
		case DEMO_BALL: {
			int margin = 20;
			int x = margin + int((sz.cx - 2 * margin) * e + 0.5);
			w.DrawEllipse(x - 16, sz.cy / 2 - 16, 32, 32, LtGreen());
		} break;

		case DEMO_TEXT: {
			int fs = 24 + int(e * 30 + 0.5);
			String s = "Animation";
			Font fnt = StdFont().Bold().Height(fs);
			Size ts = GetTextSize(s, fnt);
			int x = (sz.cx - ts.cx) / 2;
			int y = (sz.cy - ts.cy) / 2;
			w.DrawText(x, y, s, fnt, LtMagenta());
		} break;

		case DEMO_FADE: {
			Color bg = SColorFace();
			Color target = Color(220, 38, 38); // red-ish
			int a = int(255 * e + 0.5);        // 0..255
			Color col = Blend(bg, target, a);  // manual fade-in over bg
			int s = 100;
			w.DrawRect((sz.cx - s) / 2, (sz.cy - s) / 2, s, s, col);
		} break;

		case DEMO_BOX_SCALE: {
			double scale = 1.0 + 0.5 * e;
			double alpha = 0.5 + 0.5 * e;
			int s = 100;
			int cx = sz.cx / 2, cy = sz.cy / 2;
			int hw = int(0.5 * s * scale + 0.5);
			RGBA a;
			a.r = 220;
			a.g = 38;
			a.b = 38;
			a.a = (byte)clamp(int(255 * alpha), 0, 255);
			Color col(a);
			w.DrawRect(cx - hw, cy - hw, 2 * hw, 2 * hw, col);
		} break;

		case DEMO_POINTS: {
			int num = max(3, sz.cx / 50);
			int startX = (sz.cx - (num - 1) * 50) / 2;
			int r = 5 + int(e * 5 + 0.5);
			for(int i = 0; i < num; i++)
				w.DrawEllipse(startX + i * 50 - r, sz.cy / 2 - r, 2 * r, 2 * r, LtGreen());
		} break;

		case DEMO_COLOR: {
			int s = 100;
			int hue = int(360 * e + 0.5);
			Color col;
			if(hue < 120) { // red→green
				double k = hue / 120.0;
				col = Color(int(255 * (1 - k)), int(255 * k), 100);
			}
			else if(hue < 240) { // green→blue
				double k = (hue - 120) / 120.0;
				col = Color(100, int(255 * (1 - k)), int(255 * k));
			}
			else { // blue→red
				double k = (hue - 240) / 120.0;
				col = Color(int(255 * k), 100, int(255 * (1 - k)));
			}
			w.DrawRect((sz.cx - s) / 2, (sz.cy - s) / 2, s, s, col);
		} break;

		case DEMO_ROTATE: {
			double ang = e * 6.283185307179586; // 2*pi
			double c = cos(ang), s = sin(ang);

			int half = 40; // half-width of the square
			Pointf pts_local[4] = {
				Pointf(-half, -half),
				Pointf(half, -half),
				Pointf(half, half),
				Pointf(-half, half),
			};

			int cx = sz.cx / 2, cy = sz.cy / 2;
			Vector<Point> poly;
			poly.SetCount(4);
			for(int i = 0; i < 4; ++i) {
				double x = pts_local[i].x, y = pts_local[i].y;
				int rx = int(cx + x * c - y * s + 0.5);
				int ry = int(cy + x * s + y * c + 0.5);
				poly[i] = Point(rx, ry);
			}
			w.DrawPolygon(poly, LtMagenta());
		} break;

		case DEMO_HOVER_BOXES: {
			int64 now = msecs();
			double local_scale = 1.0;
			if(hover_anim_running) {
				double t = (now - hover_start_ms) / 500.0; // 0..1 over 500ms
				if(t >= 1.0) {
					hover_anim_running = false;
					t = 1.0;
				}
				double b = t < 0.5 ? (t * 2.0) : (1.0 - (t - 0.5) * 2.0);
				double eased_b = ease ? (*ease)(b) : b;
				local_scale = 1.0 + 0.4 * eased_b;
				Refresh(); // continue micro anim
			}

			for(int i = 0; i < hover_boxes.GetCount(); ++i) {
				const Rect& r = hover_boxes[i].r;
				double s = (i == hover_idx && hover_anim_running) ? local_scale : 1.0;
				int w2 = int(r.Width() * s * 0.5);
				int h2 = int(r.Height() * s * 0.5);
				int cx = r.CenterPoint().x;
				int cy = r.CenterPoint().y;
				w.DrawRect(cx - w2, cy - h2, 2 * w2, 2 * h2, LtYellow());
			}
		} break;
		}
	}
};

// Per-demo bundle: caption + canvas + animation
struct Demo {
	String name;
	StaticText caption;
	CanvasCtrl canvas;
	One<Animation> anim; // created when starting
};

class AnimLab : public TopWindow {
public:
	// Global controls
	DropList dd_playback;
	DropList dd_easing;
	EditInt ed_duration;
	Button bt_start, bt_pause, bt_reset;
	StaticText lb_status;

	// Demos
	Array<Demo> demos;

	// FPS readout (approx)
	TimeStop fps_ts;
	int fps_frames = 0;

	AnimLab()
	{
		Title("Animation Lab").Sizeable().Zoomable();
		SetRect(0, 0, 1000, 720);

		// Controls column (left)
		Add(dd_playback.LeftPos(10, 180).TopPos(10, 24));
		dd_playback.Add(0, "Single");
		dd_playback.Add(1, "Loop");
		dd_playback.Add(2, "Yoyo");
		dd_playback <<= 0;

		Add(dd_easing.LeftPos(10, 180).TopPos(44, 24));
		for(int i = 0; i < int(__countof(kEases)); ++i)
			dd_easing.Add(i, kEases[i].name);
		dd_easing <<= 3; // InOutCubic
		dd_easing.WhenAction = [=] { ApplyEasingToAll(); };

		Add(ed_duration.LeftPos(10, 180).TopPos(78, 24));
		ed_duration.MinMax(1, 600000);
		ed_duration <<= 800;

		Add(bt_start.LeftPos(10, 180).TopPos(112, 24));
		bt_start.SetLabel("Start All");
		bt_start.WhenPush = [=] { StartAll(); };

		Add(bt_pause.LeftPos(10, 85).TopPos(146, 24));
		bt_pause.SetLabel("Pause");
		bt_pause.WhenPush = [=] { PauseAll(); };

		Add(bt_reset.LeftPos(105, 85).TopPos(146, 24));
		bt_reset.SetLabel("Reset");
		bt_reset.WhenPush = [=] { ResetAll(); };

		Add(lb_status.LeftPos(10, 180).TopPos(184, 24));
		lb_status.SetText("Idle");

		// Grid of canvases (2 columns x 4 rows) to the right
		const int col_x = 210;
		const int gap = 10;
		const int cw = 360, ch = 150;

		auto add_demo = [&](int col, int row, const char* label, DemoKind kind) {
			Demo& d = demos.Add(); // in-place default construction
			d.name = label;
			d.canvas.SetKind(kind);
			int x = col_x + col * (cw + gap);
			int y = 10 + row * (ch + gap);
			// caption
			Add(d.caption.LeftPos(x, cw).TopPos(y, 18));
			d.caption.SetText(label);
			// canvas
			Add(d.canvas.LeftPos(x, cw).TopPos(y + 20, ch));
		};

		add_demo(0, 0, "Ball (Side to Side)", DEMO_BALL);
		add_demo(1, 0, "Pulsing Text", DEMO_TEXT);
		add_demo(0, 1, "Fading Element", DEMO_FADE);
		add_demo(1, 1, "Hover Box (Scale)", DEMO_BOX_SCALE);
		add_demo(0, 2, "Pulsing Points", DEMO_POINTS);
		add_demo(1, 2, "Color Change", DEMO_COLOR);
		add_demo(0, 3, "Rotating Square", DEMO_ROTATE);
		add_demo(1, 3, "Hovering Boxes", DEMO_HOVER_BOXES);

		ApplyEasingToAll();
		ResetAll(); // draw e=0 state
	}

private:
	const Easing::Fn* CurrentEase() const
	{
		int idx = ~dd_easing;
		if(idx < 0 || idx >= int(__countof(kEases)))
			idx = 0;
		return kEases[idx].fn;
	}

	void ApplyEasingToAll()
	{
		const Easing::Fn* ef = CurrentEase();
		for(Demo& d : demos)
			d.canvas.SetEasing(ef);
	}

	void StartAll()
	{
		// clear any existing
		for(Demo& d : demos) {
			if(d.anim) {
				d.anim->Cancel();
				d.anim.Clear();
			}
		}

		int ms = max(1, int(~ed_duration));
		const Easing::Fn* ef = CurrentEase();

		for(int i = 0; i < demos.GetCount(); ++i) {
			Demo& d = demos[i];
			d.anim.Create(d.canvas); // owner = canvas
			Animation& A = *d.anim;

			A([this, &d, i](double e) {
				d.canvas.Set(e);
				// Count FPS only from the first demo:
				if(i == 0) {
					++fps_frames;
					double s = fps_ts.Seconds();
					if(s >= 0.25) {
						int fps = int(fps_frames / s + 0.5);
						fps_frames = 0;
						fps_ts.Reset();
						lb_status.SetText(Format("Running — FPS ~ %d", fps));
					}
				}
				return true;
			});

			A.Duration(ms).Ease(*ef);
			
			int mode = int(~dd_playback);
			Cout() << "Playback mode: " << mode << '\n';
			
			switch(mode) {
			case 1:
				A.Loop(-1);
				break; // loop forever
					   
			case 2:    // Yoyo
				A.Yoyo(true);
				//A.Loop(0x7fffffff); // effectively infinite yoyo
				A.Loop(-1); // effectively infinite yoyo
				
				break;
			default:
				break; // single-shot
			}

			A.OnFinish(callback(this, &AnimLab::OnAnyFinish));
			A.OnCancel(callback(this, &AnimLab::OnAnyCancel));
			A.Play();
		}

		fps_frames = 0;
		fps_ts.Reset();
		lb_status.SetText("Running…");
	}

	void PauseAll()
	{
		for(Demo& d : demos)
			if(d.anim)
				d.anim->Pause();
		lb_status.SetText("Paused");
	}

	void ResetAll()
	{
		for(Demo& d : demos) {
			if(d.anim) {
				d.anim->Cancel();
				d.anim.Clear();
			}
			d.canvas.Set(0.0);
		}
		lb_status.SetText("Idle");
	}

	void OnAnyFinish() { lb_status.SetText("Finished (singles may end before loops)"); }
	void OnAnyCancel() { /* Progress caches retained per Animation */ }
};

// public entry
void RunLab()
{
	AnimLab w;
	w.Run(); // blocks until closed
}

} // namespace GUIAnim
