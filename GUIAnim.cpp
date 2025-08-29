// GUIAnim.cpp — “Animation Lab” with interactive easing curve editor + UI-ish textbox demo.
// Native U++ UI, conservative API usage.
//
// Demos (each in its own canvas):
//   1) Ball (Side to Side)       2) Pulsing Text
//   3) Fading Element            4) Resize Text (UI-ish)
//   5) Pulsing Points            6) Color Change
//   7) Rotating Square           8) Hovering Boxes (interactive)
//
// 2025-08-19 — initial GUI lab; fixes: RGBA alpha, no WinAPI SetCursor,
//              in-place Vector construction, Box Moveable, caption lifetime,
//              One<Animation> call chain split.
// 2025-08-27 — interactive easing curve editor with “User Curve”; full easing list;
//              curve formula label; reset curve on preset change.
// 2025-08-28 — resize-text demo tile; curve editor mouse-capture fix (drag stops on release);
//              left-column layout polish; hover-boxes tidy.
//
// NOTE: requires #include <CtrlLib/Animation.h> and Animation::Finalize() in main.

#include <CtrlLib/Animation.h>
#include <CtrlLib/CtrlLib.h>
using namespace Upp;

namespace GUIAnim {

// ---------- Easing map ----------
struct EaseItem {
    const char* name;
    Easing::Fn fn;          // callable easing
    Pointf p0, p1, p2, p3;  // cubic Bezier points for display text (P0=(0,0), P3=(1,1))
};

static const EaseItem kEases[] = {
    {"Linear",        Easing::Linear(),        Pointf(0,0), Pointf(0.000, 0.000), Pointf(1.000, 1.000), Pointf(1,1)},
    {"OutBounce",     Easing::OutBounce(),     Pointf(0,0), Pointf(0.680,-0.550), Pointf(0.265, 1.550), Pointf(1,1)},
    {"InQuad",        Easing::InQuad(),        Pointf(0,0), Pointf(0.550, 0.085), Pointf(0.680, 0.530), Pointf(1,1)},
    {"OutQuad",       Easing::OutQuad(),       Pointf(0,0), Pointf(0.250, 0.460), Pointf(0.450, 0.940), Pointf(1,1)},
    {"InOutQuad",     Easing::InOutQuad(),     Pointf(0,0), Pointf(0.455, 0.030), Pointf(0.515, 0.955), Pointf(1,1)},
    {"InCubic",       Easing::InCubic(),       Pointf(0,0), Pointf(0.550, 0.055), Pointf(0.675, 0.190), Pointf(1,1)},
    {"OutCubic",      Easing::OutCubic(),      Pointf(0,0), Pointf(0.215, 0.610), Pointf(0.355, 1.000), Pointf(1,1)},
    {"InOutCubic",    Easing::InOutCubic(),    Pointf(0,0), Pointf(0.645, 0.045), Pointf(0.355, 1.000), Pointf(1,1)},
    {"InQuart",       Easing::InQuart(),       Pointf(0,0), Pointf(0.895, 0.030), Pointf(0.685, 0.220), Pointf(1,1)},
    {"OutQuart",      Easing::OutQuart(),      Pointf(0,0), Pointf(0.165, 0.840), Pointf(0.440, 1.000), Pointf(1,1)},
    {"InOutQuart",    Easing::InOutQuart(),    Pointf(0,0), Pointf(0.770, 0.000), Pointf(0.175, 1.000), Pointf(1,1)},
    {"InQuint",       Easing::InQuint(),       Pointf(0,0), Pointf(0.755, 0.050), Pointf(0.855, 0.060), Pointf(1,1)},
    {"OutQuint",      Easing::OutQuint(),      Pointf(0,0), Pointf(0.230, 1.000), Pointf(0.320, 1.000), Pointf(1,1)},
    {"InOutQuint",    Easing::InOutQuint(),    Pointf(0,0), Pointf(0.860, 0.000), Pointf(0.070, 1.000), Pointf(1,1)},
    {"InSine",        Easing::InSine(),        Pointf(0,0), Pointf(0.470, 0.000), Pointf(0.745, 0.715), Pointf(1,1)},
    {"OutSine",       Easing::OutSine(),       Pointf(0,0), Pointf(0.390, 0.575), Pointf(0.565, 1.000), Pointf(1,1)},
    {"InOutSine",     Easing::InOutSine(),     Pointf(0,0), Pointf(0.445, 0.050), Pointf(0.550, 0.950), Pointf(1,1)},
    {"InExpo",        Easing::InExpo(),        Pointf(0,0), Pointf(0.950, 0.050), Pointf(0.795, 0.035), Pointf(1,1)},
    {"OutExpo",       Easing::OutExpo(),       Pointf(0,0), Pointf(0.190, 1.000), Pointf(0.220, 1.000), Pointf(1,1)},
    {"InOutExpo",     Easing::InOutExpo(),     Pointf(0,0), Pointf(1.000, 0.000), Pointf(0.000, 1.000), Pointf(1,1)},
    {"InElastic",     Easing::InElastic(),     Pointf(0,0), Pointf(0.600,-0.280), Pointf(0.735, 0.045), Pointf(1,1)},
    {"OutElastic",    Easing::OutElastic(),    Pointf(0,0), Pointf(0.175, 0.885), Pointf(0.320, 1.275), Pointf(1,1)},
    {"InOutElastic",  Easing::InOutElastic(),  Pointf(0,0), Pointf(0.680,-0.550), Pointf(0.265, 1.550), Pointf(1,1)},
    {"User Curve",    Easing::Fn(),            Pointf(0,0), Pointf(0.330, 0.330), Pointf(0.660, 0.660), Pointf(1,1)},
};

// ---------- helpers ----------
static inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
static inline double Seg01(double p, double a0, double a1) {
    if (p <= a0) return 0.0;
    if (p >= a1) return 1.0;
    return (p - a0) / max(1e-9, (a1 - a0));
}

// General cubic Bezier in Y for display/easing override
static double CubicBezierY(double t, Pointf p0, Pointf p1, Pointf p2, Pointf p3)
{
    double u = 1.0 - t;
    double tt = t * t;
    double uu = u * u;
    double uuu = uu * u;
    double ttt = tt * t;
    double y = uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y;
    return clamp(y, 0.0, 1.0);
}

// ---------- Curve editor Ctrl ----------
class CurveEditor : public Ctrl {
public:
    Pointf p0, p1, p2, p3;                    // control points (P0.x=0, P3.x=1)
    enum Drag { NONE, P0, P1, P2, P3 } dragging = NONE;
    Function<void()> on_change;

    CurveEditor() : p0(0,0), p1(0.33,0.33), p2(0.66,0.66), p3(1,1) {}

    virtual void Paint(Draw& w) override {
        Size sz = GetSize();
        w.DrawRect(sz, White());

        const int inset = 6; // leave room for handles
        double sx = (sz.cx - 2*inset) / 200.0;
        double sy = (sz.cy - 2*inset) / 200.0;

        // grid
        w.DrawLine(inset, sz.cy/2, sz.cx - inset, sz.cy/2, 1, Gray());
        w.DrawLine(sz.cx/2, inset, sz.cx/2, sz.cy - inset, 1, Gray());

        // curve polyline
        Vector<Point> poly;
        for (double t=0; t<=1.00001; t+=0.01) {
            double y = CubicBezierY(t, p0, p1, p2, p3);
            int px = int(inset + t*200*sx + 0.5);
            int py = int(sz.cy - inset - y*200*sy + 0.5);
            poly.Add(Point(px, py));
        }
        w.DrawPolyline(poly, 2, Blue());

        auto draw_handle = [&](Pointf P, Color c){
            int x = int(inset + P.x*200*sx + 0.5);
            int y = int(sz.cy - inset - P.y*200*sy + 0.5);
            w.DrawEllipse(x-5, y-5, 10, 10, c, 1, c);
        };
        draw_handle(p0, Green());
        draw_handle(p1, Red());
        draw_handle(p2, Red());
        draw_handle(p3, Green());
    }

    // map pixel to normalized [0,1] Bezier editor coords
    Pointf ToNorm(Point pt) const {
        Size sz = GetSize();
        const int inset = 6;
        double sx = 200.0 / (sz.cx - 2*inset);
        double sy = 200.0 / (sz.cy - 2*inset);
        return Pointf(clamp((pt.x - inset) * sx / 200.0, 0.0, 1.0),
                      clamp(1.0 - (pt.y - inset) * sy / 200.0, 0.0, 1.0));
    }

	 Drag Hit(Pointf nf) const {
	    const auto is_near = [&](Pointf a){ return fabs(nf.x - a.x) < 0.06 && fabs(nf.y - a.y) < 0.06; };
	    if (is_near(p1)) return P1;
	    if (is_near(p2)) return P2;
	    if (fabs(nf.x - 0.0) < 0.06 && fabs(nf.y - p0.y) < 0.06) return P0;
	    if (fabs(nf.x - 1.0) < 0.06 && fabs(nf.y - p3.y) < 0.06) return P3;
	    return NONE;
	}

    virtual void LeftDown(Point pt, dword) override {
        SetCapture(); // start capture so releases outside still end drag
        Pointf nf = ToNorm(pt);
        dragging = Hit(nf);
        if (dragging == P1) p1 = nf;
        else if (dragging == P2) p2 = nf;
        else if (dragging == P0) p0.y = nf.y; // lock x=0
        else if (dragging == P3) p3.y = nf.y; // lock x=1
        if (on_change) on_change();
        Refresh();
    }

    virtual void MouseMove(Point pt, dword) override {
        if (dragging == NONE) return;
        // if user released the button outside, stop drag
        if (!GetMouseLeft()) { dragging = NONE; ReleaseCapture(); Refresh(); return; }
        Pointf nf = ToNorm(pt);
        switch (dragging) {
        case P1: p1 = nf; break;
        case P2: p2 = nf; break;
        case P0: p0.y = nf.y; break;
        case P3: p3.y = nf.y; break;
        default: break;
        }
        if (on_change) on_change();
        Refresh();
    }

    virtual void LeftUp(Point, dword) override {
        dragging = NONE;
        ReleaseCapture();
        if (on_change) on_change();
        Refresh();
    }

    virtual void MouseLeave() override {
        // safety: if button got released while off-control
        if (!GetMouseLeft() && dragging != NONE) {
            dragging = NONE;
            ReleaseCapture();
            if (on_change) on_change();
            Refresh();
        }
    }
};

// ---------- Demo kinds ----------
enum DemoKind {
    DEMO_BALL = 0,
    DEMO_TEXT,
    DEMO_FADE,
    DEMO_UI_SCENE,   // UI-ish tile that scales with canvas
    DEMO_POINTS,
    DEMO_COLOR,
    DEMO_ROTATE,
    DEMO_HOVER_BOXES,
};

// ---------- Simple draw surface ----------
class CanvasCtrl : public Ctrl {
public:
    DemoKind   kind = DEMO_BALL;
    double     eased = 0.0;                 // 0..1 from Animation
    Easing::Fn ease  = Easing::InOutCubic();

    // --- Resize TextBox state (only for DEMO_RESIZE_TEXTBOX)
    Rect    r_from, r_to;
    double  r_t = 1.0;
    Size    last_canvas_sz = Size(0,0);
    One<Animation> resize_anim;

    // --- Hover boxes
    struct Box : Moveable<Box> { Rect r; double scale = 1.0; };
    Vector<Box> hover_boxes;
    bool  hover_anim_running = false;
    int   hover_idx = -1;
    int64 hover_start_ms = 0;

    void SetKind(DemoKind k)             { kind = k; Refresh(); }
    void Set(double e)                   { eased = clamp(e, 0.0, 1.0); Refresh(); }
    void SetEasing(const Easing::Fn& fn) { ease = fn; Refresh(); }

    static Rect LerpRect(const Rect& a, const Rect& b, double t) {
        return RectC(int(lerp(a.left,   b.left,   t)+0.5),
                     int(lerp(a.top,    b.top,    t)+0.5),
                     int(lerp(a.Width(), b.Width(), t)+0.5),
                     int(lerp(a.Height(),b.Height(),t)+0.5));
    }


    void BuildHoverBoxes() {
        hover_boxes.Clear();
        Size sz = GetSize();
        int num = 5, size = 60, gap = 15;
        int total = num * size + (num - 1) * gap;
        int x0 = (sz.cx - total) / 2;
        int y  = (sz.cy - size) / 2;
        if (x0 < 0) { size = 40; gap = 10; total = num*size + (num-1)*gap; x0 = (sz.cx-total)/2; }
        for (int i=0;i<num;i++) {
            Rect r = RectC(x0 + i*(size+gap), y, size, size);
            Box b; b.r = r; b.scale = 1.0; hover_boxes.Add(b);
        }
    }

    virtual void Layout() override {
       if (kind == DEMO_HOVER_BOXES)     BuildHoverBoxes();
    }

    virtual void MouseMove(Point p, dword) override {
        if (kind != DEMO_HOVER_BOXES) return;
        int hit = -1;
        for (int i=0;i<hover_boxes.GetCount();++i)
            if (hover_boxes[i].r.Contains(p)) { hit = i; break; }

        if (hit >= 0 && (hover_idx != hit || !hover_anim_running)) {
            hover_idx = hit; hover_anim_running = true; hover_start_ms = msecs(); Refresh();
        } else if (hit < 0 && hover_anim_running) {
            hover_anim_running = false; hover_idx = -1; Refresh();
        }
    }

    virtual void Paint(Draw& w) override
    {
        const Size sz = GetSize();
        w.DrawRect(sz, SColorFace());

        auto get_eased = [&](double t){ return ease ? ease(t) : t; };
        const double e = get_eased(eased);

        switch (kind) {

        case DEMO_BALL: {
            int margin = 20;
            int path_width = sz.cx - 2*margin;
            int x = margin + int(path_width * e + 0.5);

            // tick marks
            int yb = sz.cy/2 + 24;
            auto tick = [&](int xx, int h){ w.DrawLine(xx, yb, xx, yb+h, 1, Black()); };
            tick(margin, 10); tick(margin + path_width/2, 10); tick(margin + path_width, 10);
            for (int i=1;i<=4;++i) { tick(margin + i*path_width/10, 5); tick(margin + (i+5)*path_width/10, 5); }

            w.DrawEllipse(x-16, sz.cy/2 - 16, 32, 32, LtGreen());
        } break;

        case DEMO_TEXT: {
            int fs = 24 + int(e * 30 + 0.5);
            String s = "Animation";
            Font fnt = StdFont().Bold().Height(fs);
            Size ts = GetTextSize(s, fnt);
            int x = (sz.cx - ts.cx)/2;
            int y = (sz.cy - ts.cy)/2;
            w.DrawText(x, y, s, fnt, LtMagenta());
        } break;

        case DEMO_FADE: {
            Color bg = SColorFace();
            Color target = Color(220, 38, 38);
            Color col = Blend(bg, target, int(255 * e));
            int s = 100;
            w.DrawRect((sz.cx - s)/2, (sz.cy - s)/2, s, s, col);
        } break;

case DEMO_UI_SCENE: {
    // ---- helpers ----
    auto seg     = [](double t, double a, double b) { if (b <= a) return 0.0; return clamp((t - a) / (b - a), 0.0, 1.0); };
    auto lerp_d  = [](double a, double b, double t){ return a + (b - a) * t; };
    auto lerp_i  = [&](int a, int b, double t){ return int(a + (b - a) * t + 0.5); };

    // Clip everything to this canvas
    w.Clipoff(0, 0, sz.cx, sz.cy);

    // ---- layout metrics (responsive) ----
    const int pad       = max(4,  sz.cx / 80);
    const int centerX   = sz.cx / 2;

    // Anchor for the whole scene (adjust this to move everything up/down)
    const int topY      = max(4, sz.cy / 100); // <— Offset the height here

    // Heading font (responsive)
    const int headingPt = clamp(sz.cy / 10, 16, 42);
    const String heading = "Animation with U++";
    const Font fntHead = StdFont().Bold().Height(headingPt);
    const Size headSz = GetTextSize(heading, fntHead);

    // Cards sizing (responsive)
    const int cardW   = clamp(sz.cx / 3, 150, 150);
    const int cardH   = clamp(sz.cy / 3, 60, 100);
    const int cardGap = max(4, sz.cx / 100);

    // ---- phase splits (based on eased global progress 'e') ----

    double p_head  = seg(e, 0.01, 0.30); // heading fade/slide
    double p_uline = seg(e, 0.35, .8); // underline expand
    double p_cards = seg(e, 0.45, 1.00); // cards slide

    auto ease_sub = [&](double t){ return ease ? ease(t) : t; };
    p_head  = ease_sub(p_head);
    p_uline = ease_sub(p_uline);
    p_cards = ease_sub(p_cards);

    // ---- heading (fade in + slide up) ----
    const int headYOffset = lerp_i(20, 0, p_head);
    

    Color bg = SColorFace();
    Color target = Color(50, 50, 50);
    Color col = Blend(bg, target, int(255 * e));

    const int headX = centerX - headSz.cx/2;
    const int headY = topY + headYOffset;

    if (p_head > 0.0)
        w.DrawText(headX, headY, heading, fntHead, col);

    // ---- underline under heading (expand to text width) ----
    const int ulW = lerp_i(0, headSz.cx, p_uline);
    int ulX = centerX - ulW/2;
    const int ulY = headY + headSz.cy + 6;
    if (ulW > 0) {
            w.DrawRect(ulX, ulY, ulW, 2, col);
     }

    // ---- cards slide in from left & right ----
    // Final resting Y for both cards (below underline)
    const int cardsY = ulY + 18;

    // Final resting X positions:
    const int leftFinalX  = centerX - cardGap/2 - cardW;
    const int rightFinalX = centerX + cardGap/2;

    // Start off-canvas
    const int leftStartX  = -cardW - max(40, sz.cx/8);
    const int rightStartX = sz.cx + max(40, sz.cx/8);

    const int leftX  = lerp_i(leftStartX,  leftFinalX,  p_cards);
    const int rightX = lerp_i(rightStartX, rightFinalX, p_cards);

    auto draw_card = [&](int x, int y, const char* title, const char* body){
        // panel
        w.DrawRect(x, y, cardW, cardH, White());
        // outer stroke
        Color c1(220, 225, 235);
        w.DrawRect(x, y, cardW, 1, c1);
        w.DrawRect(x, y+cardH-1, cardW, 1, c1);
        w.DrawRect(x, y, 1, cardH, c1);
        w.DrawRect(x+cardW-1, y, 1, cardH, c1);
        // inner stroke
        Color c2(205, 210, 230);
        w.DrawRect(x+1, y+1, cardW-2, 1, c2);
        w.DrawRect(x+1, y+cardH-2, cardW-2, 1, c2);
        w.DrawRect(x+1, y+1, 1, cardH-2, c2);
        w.DrawRect(x+cardW-2, y+1, 1, cardH-2, c2);
        // title & body
        Font tfont = StdFont().Bold().Height(clamp(cardH/6, 12, 18));
        Font bfont = StdFont().Height(clamp(cardH/6, 11, 16));
        int tx = x + 14;
        int ty = y + 12;
        w.DrawText(tx, ty, title, tfont, Color(55, 65, 81));
        ty += GetTextSize(title, tfont).cy + 6;
        w.DrawText(tx, ty, body, bfont, Color(88, 96, 108));
    };

    if (p_cards > 0.0) {
        draw_card(leftX,  cardsY, "From the Left",  "U++ Animation for all.");
        draw_card(rightX, cardsY, "From the Right", "U++ Animation for all.");
    }

    w.End(); // end clip
} break;


        case DEMO_POINTS: {
            int num = max(3, sz.cx / 50);
            int startX = (sz.cx - (num - 1) * 50)/2;
            int r = 5 + int(e * 5 + 0.5);
            for (int i=0;i<num;i++)
                w.DrawEllipse(startX + i*50 - r, sz.cy/2 - r, 2*r, 2*r, LtGreen());
        } break;

        case DEMO_COLOR: {
            int s = 100;
            int hue = int(360 * e + 0.5);
            Color col;
            if (hue < 120) { double k = hue/120.0;    col = Color(int(255*(1-k)), int(255*k), 100); }
            else if (hue < 240) { double k=(hue-120)/120.0; col = Color(100, int(255*(1-k)), int(255*k)); }
            else { double k=(hue-240)/120.0;          col = Color(int(255*k), 100, int(255*(1-k))); }
            w.DrawRect((sz.cx - s)/2, (sz.cy - s)/2, s, s, col);
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
            for (int i = 0; i < 4; ++i) {
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
            if (hover_anim_running) {
                double t = (now - hover_start_ms) / 500.0; // 0..1 over 500ms
                if (t >= 1.0) { hover_anim_running = false; t = 1.0; }
                double b = (t < 0.5) ? (t*2.0) : (1.0 - (t-0.5)*2.0);
                double eased_b = ease ? ease(b) : b;
                local_scale = 1.0 + 0.4 * eased_b;
                Refresh();
            }
            for (int i=0;i<hover_boxes.GetCount();++i) {
                const Rect& r = hover_boxes[i].r;
                double s = (i == hover_idx && hover_anim_running) ? local_scale : 1.0;
                int w2 = int(r.Width()  * s * 0.5);
                int h2 = int(r.Height() * s * 0.5);
                int cx = r.CenterPoint().x, cy = r.CenterPoint().y;
                w.DrawRect(cx - w2, cy - h2, 2*w2, 2*h2, Color(20,60,160));
            }
        } break;

        } // switch
    }
};

// Per-demo bundle
struct Demo {
    String       name;
    StaticText   caption;
    CanvasCtrl   canvas;
    One<Animation> anim;
};

// ---------- Main window ----------
class AnimLab : public TopWindow {
public:
    // Global controls
    DropList   dd_playback;
    DropList   dd_easing;
    EditInt    ed_duration;
    Button     bt_start, bt_pause, bt_reset;
    StaticText lb_status;

    // Curve editor + label
    CurveEditor curve;
    StaticText  lb_curve_formula;

    // Demos
    Array<Demo> demos;

    // FPS readout (approx)
    TimeStop fps_ts;
    int      fps_frames = 0;

    AnimLab() {
        Title("Animation Lab").Sizeable().Zoomable();
        SetRect(0,0,1000,740);

        // Controls column (left)
        int x = 10, w = 180, y = 10, h = 24, gap = 8;

        Add(dd_playback.LeftPos(x, w).TopPos(y, h));
        dd_playback.Add(0, "Single");
        dd_playback.Add(1, "Loop");
        dd_playback.Add(2, "Yoyo");
        dd_playback <<= 0;  y += h + gap;

        Add(dd_easing.LeftPos(x, w).TopPos(y, h));
        for (int i=0;i<int(__countof(kEases));++i) dd_easing.Add(i, kEases[i].name);
        dd_easing <<= 7; // InOutCubic by default
        dd_easing.WhenAction = [=]{ ApplyEasingToAll(); };
        y += h + gap;

        Add(ed_duration.LeftPos(x, w).TopPos(y, h));
        ed_duration.MinMax(1, 100000);
        ed_duration <<= 1200;  y += h + gap;

        Add(bt_start.LeftPos(x, w).TopPos(y, h));
        bt_start.SetLabel("Start All");
        bt_start.WhenPush = [=]{ StartAll(); };
        y += h + gap;

        Add(bt_pause.LeftPos(x, (w-8)/2).TopPos(y, h));
        bt_pause.SetLabel("Pause");
        bt_pause.WhenPush = [=]{ TogglePauseContinue(); };

        Add(bt_reset.LeftPos(x + (w+8)/2, (w-8)/2).TopPos(y, h));
        bt_reset.SetLabel("Reset");
        bt_reset.WhenPush = [=]{ ResetAll(); };
        y += h + gap;

        Add(lb_status.LeftPos(x, w).TopPos(y, h));
        lb_status.SetText("Idle");
        y += h + gap;

        // Curve editor (bigger box), then formula directly below
        int curve_h = 180;
        Add(curve.LeftPos(x, w).TopPos(y, curve_h));
        curve.on_change = [=]{
            dd_easing <<= int(__countof(kEases) - 1); // select "User Curve"
            ApplyEasingToAll();
            UpdateCurveFormula();
        };
        y += curve_h + 6;

        Add(lb_curve_formula.LeftPos(x, w).TopPos(y, h));
        lb_curve_formula.SetText("[ Bezier(0.645, 0.045, 0.355, 1.000) ]"); // matches InOutCubic

        // Grid of canvases (2 columns x 4 rows) to the right
        const int col_x = 210;
        const int gapx  = 10;
        const int cw    = 360, ch = 150;

        auto add_demo = [&](int col, int row, const char* label, DemoKind kind){
            Demo& d = demos.Add();
            d.name = label;
            d.canvas.SetKind(kind);
            int dx = col_x + col*(cw + gapx);
            int dy = 10 + row*(ch + gapx);

            Add(d.caption.LeftPos(dx, cw).TopPos(dy, 18));
            d.caption.SetText(label);

            Add(d.canvas.LeftPos(dx, cw).TopPos(dy + 20, ch));
        };

        add_demo(0,0,"Ball (Side to Side)", DEMO_BALL);
        add_demo(1,0,"Pulsing Text",        DEMO_TEXT);
        add_demo(0,1,"Fading Element",      DEMO_FADE);
        add_demo(1,1,"UI Scene (Cards)",    DEMO_UI_SCENE);
        add_demo(0,2,"Pulsing Points",      DEMO_POINTS);
        add_demo(1,2,"Color Change",        DEMO_COLOR);
        add_demo(0,3,"Rotating Square",     DEMO_ROTATE);
        add_demo(1,3,"Hovering Boxes",      DEMO_HOVER_BOXES);

        ApplyEasingToAll();
        ResetAll();
    }

virtual void Layout() override {
    const int leftX = 10;
    const int leftW = 180;
    const int leftTop = 10;
    const int leftGap = 8;

    // Left column is already anchored by LeftPos/TopPos; nothing to do here.

    // Right pane bounds
    const int rightX = 210; // matches your col_x
    const int gap = 10;

    const Size sz = GetSize();
    const int rightW = max(240, sz.cx - rightX - gap);
    const int rightH = max(200, sz.cy - 20);

    // 2 columns × 4 rows
    const int cols = 2, rows = 4;

    // Compute dynamic tile size
    int tileW = max(220, (rightW - (cols - 1) * gap) / cols);
    int tileH = max(120, (rightH - (rows - 1) * gap) / rows) - 20; // minus caption height

    // Lay out the 8 demos in row-major order
    for (int i = 0; i < demos.GetCount(); ++i) {
        int row = i / cols;
        int col = i % cols;
        int cx = rightX + col * (tileW + gap);
        int cy = 10 + row * (tileH + 20 + gap); // 20 = caption height

        demos[i].caption.LeftPos(cx, tileW).TopPos(cy, 18);
        demos[i].canvas.LeftPos(cx, tileW).TopPos(cy + 20, tileH);
    }
}


private:
    void UpdateCurveFormula() {
        String s = Format("[ Bezier(%.3f, %.3f, %.3f, %.3f) ]",
                          curve.p1.x, curve.p1.y, curve.p2.x, curve.p2.y);
        lb_curve_formula.SetText(s);
    }

    Easing::Fn CurrentEase() const {
        int idx = ~dd_easing;
        if (idx < 0 || idx >= int(__countof(kEases))) idx = 0;
        if (idx == int(__countof(kEases) - 1)) // User Curve
            return [this](double t){ return CubicBezierY(t, curve.p0, curve.p1, curve.p2, curve.p3); };
        return kEases[idx].fn;
    }

    void ApplyEasingToAll() {
        Easing::Fn ef = CurrentEase();
        for (Demo& d : demos)
            d.canvas.SetEasing(ef);

        // if preset (not User Curve), sync editor points & label
        int idx = ~dd_easing;
        if (idx >= 0 && idx < int(__countof(kEases)) - 1) {
            curve.p0 = kEases[idx].p0;
            curve.p1 = kEases[idx].p1;
            curve.p2 = kEases[idx].p2;
            curve.p3 = kEases[idx].p3;
            curve.Refresh();
            UpdateCurveFormula();
        }
    }

    void TogglePauseContinue() {
        bool any_paused = false;
        for (const Demo& d : demos) if (d.anim && d.anim->IsPaused()) { any_paused = true; break; }

        if (any_paused) {
            for (Demo& d : demos) if (d.anim) d.anim->Resume();
            bt_pause.SetLabel("Pause");
            lb_status.SetText("Running…");
        } else {
            for (Demo& d : demos) if (d.anim) d.anim->Pause();
            bt_pause.SetLabel("Continue");
            lb_status.SetText("Paused");
        }
    }

    void StartAll() {
        for (Demo& d : demos) { if (d.anim) { d.anim->Cancel(); d.anim.Clear(); } }

        int ms = max(1, int(~ed_duration));
        Easing::Fn ef = CurrentEase();

        for (int i=0;i<demos.GetCount();++i) {
            Demo& d = demos[i];
            d.anim.Create(d.canvas);
            Animation& A = *d.anim;

            A([this, &d, i](double e){
                d.canvas.Set(e);
                if (i == 0) { // lightweight FPS readout
                    ++fps_frames;
                    double s = fps_ts.Seconds();
                    if (s >= 0.25) {
                        int fps = int(fps_frames / s + 0.5);
                        fps_frames = 0; fps_ts.Reset();
                        lb_status.SetText(Format("Running — FPS ~ %d", fps));
                    }
                }
                return true;
            });

            A.Duration(ms).Ease(ef);

            switch (int(~dd_playback)) {
            case 1: A.Loop(-1); break;              // loop forever
            case 2: A.Yoyo(true).Loop(-1); break;   // yoyo forever
            default: break;                         // single
            }

            A.OnFinish(callback(this, &AnimLab::OnAnyFinish));
            A.OnCancel(callback(this, &AnimLab::OnAnyCancel));
            A.Play();
        }

        fps_frames = 0; fps_ts.Reset();
        lb_status.SetText("Running…");
        bt_pause.SetLabel("Pause");
    }

    void ResetAll() {
        for (Demo& d : demos) {
            if (d.anim) { d.anim->Cancel(); d.anim.Clear(); }
            d.canvas.Set(0.0);
        }
        lb_status.SetText("Idle");
        bt_pause.SetLabel("Pause");
    }

    void OnAnyFinish() { lb_status.SetText("Finished (singles may end before loops)"); }
    void OnAnyCancel() { /* no-op */ }
};

// public entry
void RunLab() {
    AnimLab w;
    w.Run(); // blocks until closed
}

} // namespace GUIAnim
