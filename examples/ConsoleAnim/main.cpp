// main.cpp — runs console probe first, then opens the GUI animation lab.

#include <CtrlLib/CtrlLib.h>
#include <Animation/Animation.h>

using namespace Upp;

#include "ConsoleAnim.h"

CONSOLE_APP_MAIN
{
    ConsoleAnim::RunProbe();          // prints PASS/FAIL summary
}
