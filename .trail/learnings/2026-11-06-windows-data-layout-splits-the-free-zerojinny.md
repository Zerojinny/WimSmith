---
title: "Windows + data layout splits the free space by a Windows percentage, and MSR is optional"
author: Zerojinny
date: 2026-11-06
cost: { minutes: 1, tokens: 728 }
id: l-09fda033
scope: repo
status: active
src: agent
---

## Decided
- Layouts: Windows only, Windows + data, data only. With Windows + data, Windows gets a percentage (5-95) of the space left after the system / MSR / recovery partitions, rounded down to whole MB; the data partition takes the rest with "create partition primary" (no size), so rounding never leaves a gap.
- Order on the disk: system (EFI or system reserved), MSR, Windows, recovery, data. Recovery stays right after Windows as Microsoft recommends; with a data partition the recovery gets an explicit size instead of the "shrink" trick used when Windows takes the rest.
- MSR is optional (checkbox, default on, GPT + Windows only). Without it the plan shows a note: Windows boots, but Setup and some disk tools expect an MSR on GPT.
- MBR + Windows + recovery + data uses exactly the four primary partitions MBR allows.
- Only the Windows partition gets a chosen letter (reported to the GUI); the data partition gets diskpart's next free letter.

In the code: `src/DiskJob.cs` (`DiskPlan.WindowsBytes`, `DiskPlan.DataBytes`, `DiskPlan.Script`, `PlanProblem`), `src/BackupPanel.Disk.cs` (`UpdateDiskUi`, `CurrentPlan`).
