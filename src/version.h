// WimSmith - version.h
//  The version and the license of the program, in one place: used by
//  app.rc (the version resource of WimSmith.exe: file properties ->
//  Details) and by the code (window title, first line of the log, the "?" tab).
//  Plain #defines only: rc.exe reads this file too.
#pragma once

#define WS_VER_MAJOR    1
#define WS_VER_MINOR    0
#define WS_VER_PATCH    0
#define WS_VER_BUILD    0

#define WS_VERSION_STR      "1.0"
#define WS_FILEVERSION_STR  "1.0.0.0"
#define WS_COPYRIGHT_STR    "Copyright (c) 2026 zerojinny. MIT License."
#define WS_AUTHOR_STR       "zerojinny"
#define WS_EMAIL_STR        "wimsmith2026@gmail.com"
#define WS_URL_STR          "https://github.com/Zerojinny/WimSmith"

#define WS_WIDEN2(s)    L##s
#define WS_WIDEN(s)     WS_WIDEN2(s)
#define WS_VERSION_W    WS_WIDEN(WS_VERSION_STR)
#define WS_COPYRIGHT_W  WS_WIDEN(WS_COPYRIGHT_STR)
#define WS_AUTHOR_W     WS_WIDEN(WS_AUTHOR_STR)
#define WS_EMAIL_W      WS_WIDEN(WS_EMAIL_STR)
#define WS_URL_W        WS_WIDEN(WS_URL_STR)
