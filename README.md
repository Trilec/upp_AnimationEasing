# upp_AnimationEasing
U++ Animation &amp; Easing Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A modern, header-friendly animation and easing engine for the U++ C++ framework. This library provides a fluent, easy-to-use interface for creating rich, performant animations on `Ctrl`-based widgets with deterministic, zero-leak memory management.

This project is a proof-of-concept demonstrating how a centralized animation system can be integrated into U++, driven by a single `TimeCallback`.

## Features

-   **Fluent Interface:** Chain commands to build complex animations intuitively: `Animation(widget).Rect(target).Time(500).Ease(Easing::OutCubic).Start();`
-   **Deterministic & Leak-Free:** Built on U++'s ownership principles (`One<>`) to ensure animation states are automatically managed without memory leaks.
-   **Header-Friendly:** Designed to be easily integrated. The core logic is self-contained in `Animation.h` and `Animation.cpp`.
-   **Extensible:** Animate any `Value` type using the generic `AnimateValue<T>` helper.
-   **Rich Easing Library:** Includes a comprehensive set of 20+ standard easing functions (Quad, Cubic, Bounce, Elastic, etc.) based on a precise Bezier solver.
-   **Core Animation Modes:** Supports `Once`, `Loop`, and `Yoyo` (reverse) playback modes.
-   **Global Control:** Stop all active animations at once with a single `KillAll()` call.

## Project Structure

The repository is structured for easy integration with a standard U++ application created in TheIDE.

```
upp_AnimationEasing/
├── CtrlLib/
│   ├── Animation.h      # The main library header
│   └── Animation.cpp    # The library implementation
├── main.cpp             # A comprehensive demo application
├── upp_AnimationEasing.upp
├── README.md            # This file
└── LICENSE              # MIT License
```

## Installation & Setup

1.  Clone this repository to your local machine.
2.  In your U++ project's main package in TheIDE, add the `CtrlLib` directory from this repository as a source folder.
3.  Include the header in your code where needed: `#include <CtrlLib/Animation.h>`
4.  Ensure your application links against the `CtrlLib` package. The demo `main.cpp` can be used as a starting point or reference.

## Quick Start

Animating a widget is straightforward. Create an `Animation` object, describe the animation, and start it. The library handles the rest.

### **Animate a Widget's Position and Size**

```cpp
#include <CtrlLib/CtrlLib.h>
#include <CtrlLib/Animation.h>

using namespace Upp;

GUI_APP_MAIN
{
    TopWindow win;
    Button myButton;
    myButton.SetLabel("Click Me!");
    win.Add(myButton.LeftPos(10, 100).TopPos(10, 30));

    myButton << [&] {
        // Animate the button to a new Rect over 500ms
        Animation(myButton)
            .Rect(Rect(200, 50, 150, 40)) // Target Rect
            .Time(500)                    // Duration in ms
            .Ease(Easing::OutElastic)     // Easing function
            .Start();
    };

    win.Run();
}
```

### **Animate a Color with Yoyo Playback**

```cpp
// Animate a widget's background color from Blue to Green and back
StaticRect myWidget;
Animation(myWidget)
    .AnimateColor(myWidget, Blue, Green, 1000, Easing::InOutSine, Yoyo);
```

## API Overview

### The `Animation` Class
-   `Animation(Ctrl& owner)`: Creates an animation bound to a widget.
-   `Start()`: Begins the animation.
-   `Stop()`: Stops the animation and removes it from the scheduler.
-   `Pause()` / `Resume()`: Pauses or resumes the animation timer.

### Fluent Methods
-   `.Time(int ms)`: Sets the duration.
-   `.Ease(Easing::Fn)`: Sets the easing function.
-   `.Count(int n)`: Sets the number of times to repeat (`-1` for infinite).
-   `.Yoyo()`: Enables reversing playback direction on each loop.
-   `.Rect(Rect r)`, `.Pos(Point p)`, `.Size(Size s)`: Built-in helpers for common `Ctrl` transformations.

### Global Functions
-   `KillAll()`: Immediately stops and destroys all active animations in the application.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
