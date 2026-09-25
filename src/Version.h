#pragma once

// Single source of truth for the app version - consumed by kestrel.rc's
// VERSIONINFO block (FileVersion/ProductVersion, shown in Explorer's file
// Properties > Details) and by MainWindow's About dialog. Bump this and
// tag the matching commit (e.g. `git tag v1.0.0`) together - nothing
// enforces that automatically.
#define KESTREL_VERSION_MAJOR 1
#define KESTREL_VERSION_MINOR 0
#define KESTREL_VERSION_PATCH 0

#define KESTREL_VERSION_STRING "1.0.0"
#define KESTREL_VERSION_STRING_W L"1.0.0"
