---
title: "Scripted text replacements must not go through a heredoc: backreferences turned into control characters and broke the manuals"
author: Zerojinny
date: 2026-11-18
branch: dev-1.3
cost: { minutes: 50, tokens: 796686 }
id: l-6f81dc08
scope: repo
status: active
src: agent
touches:
  - CLAUDE.en.md
  - CLAUDE.md
  - MANUAL.en.md
  - MANUAL.md
  - README.en.md
  - README.md
  - phoenixpe/WimSmith.script
  - src/tab_browse.cpp
---

## Watch out
- A regex replacement with backreferences, sent as Python inside a Bash heredoc from this environment, wrote the control characters 0x01 / 0x02 instead of the groups: the text '\\1…\\2' reached Python as '\1…\2' (octal escapes). In the manuals every "X...]" and "X... |" became 0x01 + "…" + 0x02, losing the letter before and the "]" / " |" after, in 14 places per manual, and it was committed, tagged v1.3 and shipped in the v1.3 zip before anyone saw it, because the text looked fine in a quick view (the control characters do not show).
- So: put such scripts in a file (Write) and run that file, use a function as the replacement (lambda m: m.group(1) + …) or a raw string, and after any scripted edit of documents check for control characters: grep -c $'[\x01\x02]' on the changed files (icons are binary and always match).

## Decided
- The repair took the right text from the file before the bad commit: the k-th broken spot is the k-th match of the same pattern there, so nothing had to be retyped.

In the code: MANUAL.md, MANUAL.en.md (broken in fb96be1, repaired on 2026-11-18).
