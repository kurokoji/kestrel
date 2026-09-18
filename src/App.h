#pragma once

#include <windows.h>

// Owns process-wide startup/shutdown (COM, common controls) and the
// message loop. Deliberately thin - no framework, no event bus.
class App {
public:
    int run(HINSTANCE hInstance, int nCmdShow);
};
