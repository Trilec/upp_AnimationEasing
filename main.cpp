// main.cpp — runs console probe first, then opens the GUI animation lab.

#include <CtrlLib/CtrlLib.h>
#include <CtrlLib/Animation.h>

using namespace Upp;

#include "ConsoleAnim.h"
#include "GUIAnim.h"

GUI_APP_MAIN
{
    // ConsoleAnim::RunProbe();          // prints PASS/FAIL summary
    Animation::Finalize();            // <-- ensure the scheduler is clean & idle
    Animation::SetFPS(30);            // optional: make pacing explicit
    GUIAnim::RunLab();                // interactive lab; close window to exit
}
