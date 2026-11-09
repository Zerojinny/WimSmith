---
title: "PhoenixPE plugin: one language for WimSmith and its menu, menu texts from WimSmith's own language files, no menu for 1.0"
author: Zerojinny
date: 2026-11-09
branch: dev-1.1
id: l-b965af48
scope: repo
status: active
src: agent
touches:
  - phoenixpe/WimSmith.script
---

## Decided
- The plugin resolves one language and uses it for both WimSmith.ini (`language` is always written) and the right-click menu. Before, "Windows default" wrote no language, so the program followed the PE while the menu was English unless Korean was picked explicitly: the two could differ.
- The default choice is "PE language": the language of the PE source (`%SourceLang%`, set by PhoenixPE's ConfigSource, e.g. ko-KR). `ko-*` gives ko, anything else en. Explicit Korean / English stay available.
- The menu texts are read with `IniRead` from WimSmith's `lang\<code>.ini` (section `ui`, the keys `RegisterMenus` in mainwin.cpp uses: bk.tab_apply, st.menu_append, ex.tab, sj.tab, in.tab, br.tab). Why: they match the program exactly, and the script stays pure ASCII, so PEBakery cannot garble Korean in a script without BOM. English defaults when the file or key is missing.
- WimSmith 1.0 cannot open files (no `<file>` / `--open <tab> <file>` argument). The plugin detects it by the missing `st.menu_append` in `lang\en.ini` and then skips the file associations and the menu, with a WARN in the build log.

## Tried and ruled out
- Korean literals for the menu in the script: worked only when Korean was chosen explicitly, didn't follow "Windows default", and risked encoding problems.

## Watch out
- The GitHub source takes the latest release. While that is 1.0, only "Add an image (backup)" (st.menu_append, new in 1.1) came out in English and no item could open a file. This is now detected and skipped. The menu needs a 1.1+ release with WimSmith-x64.zip, or the Local folder source. The local build was confirmed by the user to show the menu correctly in Korean.
- Copying the script into PhoenixPE's MyApps resets the user's interface choices to the script's values.

In the code: phoenixpe/WimSmith.script ([Process]: LangCode, OpensFiles, IniRead block; [Interface] cmb_Language), copy at F:\PE\PhoenixPE-2026.07.25-x64\Projects\MyApps\Applications\WimSmith.script.
