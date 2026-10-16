#pragma once

// TestServerCommands needs to redirect Client/ClientImpl to an isolated named
// pipe without exposing a production-only test API. Parse WeaselIPC.h once with
// Client's private section visible, then immediately restore the keyword before
// any other headers are processed.
#define private public
#include <WeaselIPC.h>
#undef private
