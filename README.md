# Animation (U++ Animation & Easing Engine)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A modern animation and easing engine for the [U++](https://www.ultimatepp.org/) C++ framework.
It provides a fluent, easy-to-use interface for creating rich, performant animations on `Ctrl`-based widgets, with deterministic memory management aligned with U++ philosophy.

This project demonstrates how a centralized animation system can be integrated into U++, driven by a single `TimeCallback`.

---

## Features

* **Fluent Interface** – Chain methods to describe animations:
  `Animation(widget).Duration(500).Ease(Easing::OutCubic) ... .Play();`
* **Deterministic & Leak-Free** – Built on U++’s ownership primitives (`One<>`), ensuring safe automatic cleanup.
* **Robust Lifecycle** – Supports `Pause`, `Cancel`, `Reset`, and now `Replay`, each with clear semantics.
* **Rich Easing Library** – Includes 20+ standard easing curves (Quad, Cubic, Bounce, Elastic, etc.), plus custom cubic-Bézier.
* **Core Modes** – `Once`, `Loop`, `Yoyo` playback.
* **Global Control** – Stop all animations at once via `KillAll()` or per-control with `KillAllFor(ctrl)`.
* **Console & GUI Examples** – Demonstrations for both headless testing and live UI animation.

---

## Repository Layout

```
Animation/                  # Core library package
 ├─ Animation.cpp
 ├─ Animation.h
 └─ Animation.upp

examples/
 ├─ ConsoleAnim/            # Console probe suite
 │   ├─ ConsoleAnim.cpp
 │   ├─ ConsoleAnim.h
 │   ├─ ConsoleAnim.upp
 │   └─ main.cpp
 └─ GUIAnim/                # GUI demo with interactive widgets
     ├─ GUIAnim.cpp
     ├─ GUIAnim.h
     ├─ GUIAnim.upp
     └─ main.cpp

LICENSE
README.md
```

To use in TheIDE, put the repo root into your Assembly’s *package nests*, alongside `uppsrc`. Then add `Animation` as a dependency to your main app.

---

## Quick Start

### Animate a Button on Click

```cpp
#include <CtrlLib/CtrlLib.h>
#include <Animation/Animation.h>
using namespace Upp;

struct MyApp : TopWindow {
    Button b;
    One<Animation> anim;

    MyApp() {
        Title("Animation demo");
        b.SetLabel("Click Me!");
        Add(b.CenterPos(Size(100, 40)));
        b << [=] {
            if(!anim.IsEmpty()) anim->Cancel();
            anim.Create(b);
            anim->Duration(500).Ease(Easing::OutBounce())
                ([&](double e) {
                    b.SetRect(40 + int(100*e), 40, 100, 40);
                    return true;
                })
                .OnFinish(callback([&]{ anim.Clear(); }))
                .Play();
        };
    }
};

GUI_APP_MAIN { MyApp().Run(); }
```

---

## Lifecycle Semantics

* **Pause** – reversible freeze. Animation remains scheduled and can `Resume()`.
* **Cancel** – aborts run, fires `OnCancel`, and preserves last forward progress snapshot (so `Progress()` still reports how far it got).
* **Reset** – aborts run, re-primes spec, sets `Progress=0`. This makes the same `Animation` instance immediately reusable.
* **Replay** – starts a fresh run using the *last committed spec* (the same settings you passed before the previous `Play()`). Useful for repeating an animation without re-typing setters.

`Progress()` always reports **time-normalized progress in [0..1]**.
The per-frame lambda you pass to `operator()(Function<bool(double)>)` receives the **eased value**.

---

## API Overview

### Core Methods

* `Animation(Ctrl& owner)` – construct animation bound to a control.
* `Play()` – start the animation.
* `Stop()` – stop and snap to end (Progress=1.0).
* `Pause()` / `Resume()` – reversible freeze/unfreeze.
* `Cancel(bool fire_cancel = true)` – abort current run, unschedule, preserve snapshot.
* `Reset(bool fire_cancel = false)` – abort + re-prime spec, ready to re-use.
* `Replay(bool interrupt = true, bool fire_cancel = true)` – run again with the last-used spec. If setters were called before `Replay()`, those take priority.

### Fluent Setters

* `.Duration(int ms)` – duration in milliseconds.
* `.Ease(Easing::Fn)` – easing curve (`Easing::OutQuad()`, or custom `Bezier(...)`).
* `.Loop(int n)` – loop count (`-1` for infinite).
* `.Yoyo(bool)` – reverse direction on each loop.
* `.Delay(int ms)` – start after delay.
* `.OnStart(...)`, `.OnFinish(...)`, `.OnCancel(...)`, `.OnUpdate(...)` – lifecycle hooks.
* `operator()(Function<bool(double)>)` – per-frame tick, gets eased `[0..1]`.

### Global Functions

* `KillAll()` – stop all animations in app.
* `KillAllFor(Ctrl&)` – stop all animations targeting a specific control.

---

## Examples

* **ConsoleAnim** – automated probe suite, checks edge cases (reuse after Cancel, Reset behavior, Replay semantics, etc.).
   <img width="783" height="455" alt="image" src="https://github.com/user-attachments/assets/6b9fd893-cc56-4d42-9211-9dae361e820b" />

* **GUIAnim** – interactive demo: animate buttons, flashing ellipses, easing curve editor.
   <img width="998" height="619" alt="image" src="https://github.com/user-attachments/assets/4345f79d-78d5-448b-b17b-c3150b5ff521" />

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).

---

👉 Would you like me to also write a **shorter “What’s New” changelog snippet** (for the forum or release notes) that just highlights Replay + callback cleanup, instead of the full README rewrite?
