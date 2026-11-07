// ============================================================
//  7z_wimlib - tab_splitjoin.h
//  The split / join tab: a WIM file as split parts (.swm) of a chosen
//  size (e.g. for FAT32), and the parts joined into one WIM file again.
//  Both run in the helper without administrator rights.
// ============================================================
#pragma once
#include "app.h"

namespace sevenwim
{
    struct SplitJoinTab
    {
        SplitJoinTab(App& app, HWND page);
        void Refresh();

        App& app;
        HWND page;

    private:
        HWND split, join;                     // the two group boxes
        HWND txtSrc, btnSrc, txtSwm, btnSwm, numPart, lblMb, btnSplit;
        HWND txtFirst, btnFirst, lblParts, txtWim, btnWim, btnJoin;
        ui::Grid splitGrid, joinGrid;

        void Layout(int w, int h);
        void ChooseSource();
        void ChooseSwm();
        void ChooseParts();
        void ShowParts();
        void ChooseJoinTarget();
        void SetBusy(bool busy);
        void StartSplit();
        void StartJoin();
    };
}
