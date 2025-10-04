// AltTabWindow.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "version.h"
#include "AltTabWindow.h"

#include "Logger.h"

#include <CommCtrl.h>
#include <Psapi.h>
#include <Windows.h>
#include <dwmapi.h>
#include <filesystem>
#include <shlobj.h>
#include <string>
#include <vector>
#include <winnt.h>
#include "Resource.h"
#include "AltTabSettings.h"
#include "Utils.h"
#include "AltTab.h"
#include "GlobalData.h"
#include <unordered_map>
#include "CheckForUpdates.h"
#include <thread>
#include <windowsx.h>


HWND           g_hStaticText            = nullptr;
HWND           g_hListView              = nullptr;
int            g_nLVHotItem             = -1;
HFONT          g_hSSFont                = nullptr;
HFONT          g_hLVFont                = nullptr;
int            g_SelectedIndex          = 0;
int            g_MouseHoverIndex        = -1;
HANDLE         g_hAltTabThread          = nullptr;
std::wstring   g_SearchString;
RECT           g_rcBtnClose;
bool           g_IsMouseOverCloseButton = false;

const int      COL_ICON_WIDTH           = 36;
const int      COL_PROCNAME_WIDTH       = 180;

// Forward declarations of functions included in this code module:
INT_PTR CALLBACK ATAboutDlgProc        (HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK AltTabWindowProc      (HWND, UINT, WPARAM, LPARAM);
bool             IsAltTabWindow        (HWND hWnd);
HWND             GetOwnerWindowHwnd    (HWND hWnd);
static void      AddListViewItem       (HWND hListView, int index, const AltTabWindowData& windowData);
static void      ContextMenuItemHandler(HWND hWnd, HMENU hSubMenu, UINT menuItemId);
static BOOL      TerminateProcessEx    (DWORD pid);
static void      ATCloseWindow         (const int index);
static bool      IsExcludedProcess     (const std::wstring& processName);

// Window procedure functions for individual messages
static void    ATW_OnActivate        (HWND hwnd, UINT state, HWND hwndActDeact, BOOL fMinimized);
static void    ATW_OnClose           (HWND hwnd);
static void    ATW_OnCommand         (HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
static void    ATW_OnContextMenu     (HWND hwnd, HWND hwndContext, UINT xPos, UINT yPos);
static BOOL    ATW_OnCreate          (HWND hWnd, LPCREATESTRUCT lpCreateStruct);
static LRESULT ATW_OnCtlColorStatic  (HWND hWnd, HDC hDC, HWND hCtl, UINT type);
static void    ATW_OnDestroy         (HWND hwnd);
static void    ATW_OnDrawItem        (HWND hwnd, const DRAWITEMSTRUCT* lpDrawItem);
static void    ATW_OnKeyDown         (HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags);
static void    ATW_OnKillFocus       (HWND hwnd, HWND hwndNewFocus);
static void    ATW_OnLButtonDown     (HWND hwnd, BOOL fDoubleClick, int x, int y, UINT keyFlags);
static BOOL    ATW_OnNotify          (HWND hwnd, int idFrom, NMHDR* pnmhdr);
static void    ATW_OnSysCommand      (HWND hwnd, UINT cmd, int x, int y);
static void    ATW_OnTimer           (HWND hwnd, UINT id);

std::vector<AltTabWindowData> g_AltTabWindows;

namespace AT {
    // Get the file version
    void GetPEInfo(
        const std::wstring& filePath,
        std::wstring& description,
        std::wstring& version,
        std::wstring& companyName)
    {
        DWORD dummy;
        DWORD verSize = GetFileVersionInfoSizeW(filePath.c_str(), &dummy);
        if (verSize > 0) {
            std::vector<BYTE> verData(verSize);
            if (GetFileVersionInfoW(filePath.c_str(), 0, verSize, verData.data())) {
                VS_FIXEDFILEINFO* fileInfo = nullptr;
                UINT len = 0;
                if (VerQueryValue(verData.data(), L"\\", (LPVOID*)&fileInfo, &len)) {
                    if (fileInfo) {
                        DWORD major = HIWORD(fileInfo->dwFileVersionMS);
                        DWORD minor = LOWORD(fileInfo->dwFileVersionMS);
                        DWORD build = HIWORD(fileInfo->dwFileVersionLS);
                        DWORD revision = LOWORD(fileInfo->dwFileVersionLS);
                        // Format: major.minor.build.revision
                        version = std::to_wstring(major) + L"." + std::to_wstring(minor) + L"." + std::to_wstring(build)
                                  + L"." + std::to_wstring(revision);
                    }
                }

                // First get the translation table (language + codepage)
                struct LANGANDCODEPAGE {
                    WORD wLanguage;
                    WORD wCodePage;
                }* lpTranslate;

                UINT cbTranslate = 0;
                if (VerQueryValueW(
                        verData.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate)) {
                    // Build the query string for "FileDescription"
                    wchar_t subBlock[64];
                    swprintf_s(
                        subBlock,
                        L"\\StringFileInfo\\%04x%04x\\FileDescription",
                        lpTranslate[0].wLanguage,
                        lpTranslate[0].wCodePage);

                    LPWSTR lpBuffer = nullptr;
                    UINT size = 0;
                    if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&lpBuffer, &size) && size > 0) {
                        description = lpBuffer;
                    }

                    // Build the query string for "CompanyName"
                    swprintf_s(
                        subBlock,
                        L"\\StringFileInfo\\%04x%04x\\CompanyName",
                        lpTranslate[0].wLanguage,
                        lpTranslate[0].wCodePage);

                    if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&lpBuffer, &size) && size > 0) {
                        companyName = lpBuffer;
                    }
                }
            }
        }
    }

    BOOL ATListViewDrawItem(HWND hListView, LPDRAWITEMSTRUCT lpDrawItemStruct) {
        /* */ HDC& hdc     = lpDrawItemStruct->hDC;
        const RECT rcItem  = lpDrawItemStruct->rcItem;
        const int rowIndex = lpDrawItemStruct->itemID;

        if (rowIndex < 0)
            return FALSE;

        // Get item data
        LVITEM lvItem = { 0 };
        lvItem.iItem  = rowIndex;
        lvItem.mask   = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        ListView_GetItem(hListView, &lvItem);

        const AltTabWindowData* pWindowData = (AltTabWindowData*)lvItem.lParam;

        // Get number of columns
        const HWND hHeader = ListView_GetHeader(hListView);
        const int columns = Header_GetItemCount(hHeader);

        // Get configured colors
        COLORREF clrBk     = ListView_GetBkColor    (hListView);
        COLORREF clrText   = ListView_GetTextColor  (hListView);

        // Fill background
        if (lpDrawItemStruct->itemState & ODS_SELECTED) {
            clrBk   = GetSysColor(COLOR_HIGHLIGHT);
            clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        }

        //AT_LOG_INFO("LVDrawItem: Index: %d, IsBeingClosed: %d, Title: %ls", rowIndex, pData->IsBeingClosed, pData->Title.c_str());

        // Check if the window is being closed
        // Show the item in red color to indicate the window is being closed
        if (pWindowData->IsBeingClosed) {
            clrBk = RGB(255, 69, 54);   // Red Orange
            clrText = RGB(0, 0, 0);   // Black
        }

        // Draw gradient background
        TRIVERTEX vertex[2];
        vertex[0].x     = rcItem.left   + 1;
        vertex[0].y     = rcItem.top    + 1;
        vertex[1].x     = rcItem.right  - 1;
        vertex[1].y     = rcItem.bottom - 1;
        vertex[0].Red   = vertex[1].Red   = GetRValue(clrBk) << 8;
        vertex[0].Green = vertex[1].Green = GetGValue(clrBk) << 8;
        vertex[0].Blue  = vertex[1].Blue  = GetBValue(clrBk) << 8;
        vertex[0].Alpha = vertex[1].Alpha = 0;

        GRADIENT_RECT gRect = { 0, 1 };
        GradientFill(hdc, vertex, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

        // Draw a border around the item
        if (lpDrawItemStruct->itemState & ODS_SELECTED) {
            HBRUSH hbr = CreateSolidBrush(RGB(201, 201, 201));
            FrameRect(hdc, &rcItem, hbr);
            DeleteObject(hbr);
        }

        // Hot track: draw a outline rectangle for hot-tracked item instead of filling the background
        if (lpDrawItemStruct->itemState & ODS_HOTLIGHT || g_nLVHotItem == rowIndex) {
            HBRUSH hbr = CreateSolidBrush(RGB(100, 149, 237)); // Cornflower Blue
            FrameRect(hdc, &rcItem, hbr);
            DeleteObject(hbr);
        }

        SetTextColor(hdc, clrText);

        for (int col = 0; col < columns; ++col) {
            RECT rcSub;
            ListView_GetSubItemRect(hListView, rowIndex, col, LVIR_BOUNDS, &rcSub);
            SetBkMode(hdc, TRANSPARENT);

            // Draw icon (first column usually)
            if (col == 0 && lvItem.iImage >= 0) {
                // ImageList_Draw(hImgList, lvItem.iImage, hdc, rcSub.left + 2, rcSub.top + 0, ILD_TRANSPARENT);
                //  Calculate rect for icon
                int rowHeight = rcSub.bottom - rcSub.top;
                int iconSize = 32;

                int x = rcSub.left + 2;
                int y = rcSub.top + (rowHeight - iconSize) / 2; // vertically centered

                ImageList_DrawEx(g_hLVImageList, rowIndex, hdc, x, y, iconSize, iconSize, CLR_NONE, CLR_NONE, ILD_NORMAL);
            } else if (col == 1) {
                // Leave some margin at left
                rcSub.left += 3;

                // Draw title and subtext (if conflict process)
                if (pWindowData->IsConflictProcess) {
                    // Just append the version to the title for conflict processes
                    const std::wstring title = pWindowData->Title + L" - [v" + pWindowData->Version + L"]";
                    DrawText(hdc, title.c_str(), -1, &rcSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                } else {
                    DrawText(hdc, pWindowData->Title.c_str(), -1, &rcSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                }
            } else {
                // Leave some margin at left
                rcSub.left += 3;

                DrawText(hdc, pWindowData->ProcessName.c_str(), -1, &rcSub, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
        }

        // Draw close button if hot-tracked
        if (lpDrawItemStruct->itemState & ODS_HOTLIGHT || g_nLVHotItem == rowIndex) {
            // Draw close button at the right side of the item
            g_rcBtnClose.left   = rcItem.right - g_nIconSize - 1;
            g_rcBtnClose.top    = rcItem.top + 1;
            g_rcBtnClose.right  = g_rcBtnClose.left + g_nIconSize;
            g_rcBtnClose.bottom = g_rcBtnClose.bottom + g_nIconSize;

            const int imgIndex = g_IsMouseOverCloseButton ? g_nImgCloseActiveInd : g_nImgCloseInactiveInd;
            ImageList_DrawEx(
                g_hImageList,
                imgIndex,
                hdc,
                g_rcBtnClose.left,
                g_rcBtnClose.top,
                g_nIconSize,
                g_nIconSize,
                CLR_NONE,
                CLR_NONE,
                ILD_TRANSPARENT);
        }

        return TRUE;
    }
}

HICON GetWindowIcon(HWND hWnd) {
    // Try to get the large icon
    HICON hIcon;
    // Use SendMessageTimeout to get the icon with a timeout
    LRESULT responding = SendMessageTimeout(hWnd,
       WM_GETICON,
       ICON_BIG,
       0,
       SMTO_ABORTIFHUNG,
       10,
       reinterpret_cast<PDWORD_PTR>(&hIcon));

    if (responding) {
        if (hIcon)
            return hIcon;

        // If the large icon is not available, try to get the small icon
        hIcon = (HICON)SendMessageW(hWnd, WM_GETICON, ICON_SMALL2, 0);
        if (hIcon)
            return hIcon;

        hIcon = (HICON)SendMessageW(hWnd, WM_GETICON, ICON_SMALL, 0);
        if (hIcon)
            return hIcon;
    }

    // Get the class long value that contains the icon handle
    hIcon = (HICON)GetClassLongPtr(hWnd, GCLP_HICON);
    if (!hIcon) {
        // If the class does not have an icon, try to get the small icon
        hIcon = (HICON)GetClassLongPtr(hWnd, GCLP_HICONSM);
    }

    if (!hIcon) {
        hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        return hIcon;
    }

    return hIcon;
}

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    if (IsAltTabWindow(hWnd)) {
        HWND   hOwner = GetOwnerWindowHwnd(hWnd);
        DWORD  processId;
        GetWindowThreadProcessId(hWnd, &processId);

        if (processId != 0) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

            if (hProcess != nullptr) {
                wchar_t szProcessPath[MAX_PATH];
                if (GetModuleFileNameEx(hProcess, nullptr, szProcessPath, MAX_PATH)) {
                    std::filesystem::path filePath = szProcessPath;
             
                    // Always get the title of owner window, otherwise popup window title will be displayed.
                    const int bufferSize = 256;
                    wchar_t windowTitle[bufferSize];
                    GetWindowTextW(hOwner, windowTitle, bufferSize);

                    // We get the actual window title for the process but windows adds "(Not Responding)"
                    // to the processes which are hung. So, appending "(Not Responding)" to the hung process manually.
                    std::wstring title = windowTitle;
                    if (IsHungAppWindow(hWnd)) {
                        title += L" (Not Responding)";
                    }

                    AltTabWindowData item;

                    item.hWnd              = hWnd;
                    item.hOwner            = hOwner;
                    item.hIcon             = GetWindowIcon(hOwner);
                    item.Title             = title;
                    item.ProcessName       = filePath.filename().wstring();
                    item.FullPath          = filePath.wstring();
                    item.PID               = processId;
                    item.IsConflictProcess = false;
                    item.IsBeingClosed     = false;

                    AT::GetPEInfo(item.FullPath, item.Description, item.Version, item.CompanyName);

                    auto* vItems   = (std::vector<AltTabWindowData>*)lParam;
                    bool  insert   = true;
                    bool  excluded = IsExcludedProcess(ToLower(item.ProcessName));

                    // If Alt+Tab is pressed, show all windows
                    if (g_IsAltTab || g_IsAltCtrlTab) {
                        insert = excluded ? false : true;
                    }
                    // If Alt+Backtick is pressed, show the process of similar process groups
                    else if (g_IsAltBacktick) {
                        if (g_AltBacktickWndInfo.hWnd == nullptr) {            
                            if (!(GetWindowLong(item.hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
                               g_AltBacktickWndInfo = item;
                               AT_LOG_INFO("g_AltBacktickWndInfo: %s", WStrToUTF8(g_AltBacktickWndInfo.ProcessName).c_str());
                            } 
                        }
                        if (g_AltBacktickWndInfo.hWnd != nullptr) {
                            insert = IsSimilarProcess(g_AltBacktickWndInfo.ProcessName, item.ProcessName);
                        }
                    }

                    // If the window is to be inserted, now apply the search string
                    if (insert && !g_SearchString.empty()) {
                        insert = InStr(item.ProcessName, g_SearchString) || InStr(item.Title, g_SearchString);

                        double matchRatio = insert ? 100 : 0;
                        // Search in process name
                        if (!insert && g_Settings.FuzzyMatchPercent != 100) {
                            matchRatio = GetPartialRatioW(g_SearchString, item.ProcessName);
                            if (matchRatio >= g_Settings.FuzzyMatchPercent) {
                                insert = true;
                            }
                        }
                        // Search in window title
                        if (!insert && g_Settings.FuzzyMatchPercent != 100) {
                            matchRatio = GetPartialRatioW(g_SearchString, item.Title);
                            if (matchRatio >= g_Settings.FuzzyMatchPercent) {
                                insert = true;
                            }
                        }
                        //AT_LOG_INFO("matchRatio = %5.1f, title = [%s]", matchRatio, WStrToUTF8(item.Title).c_str());
                    }

                    if (insert) {
                        //AT_LOG_INFO("Inserting hWnd: %0#9x, title: %s", item.hWnd, WStrToUTF8(item.Title).c_str());
                        vItems->push_back(std::move(item));
                    }
                }

                CloseHandle(hProcess);
            }
        }
    }

    return TRUE;
}

DWORD WINAPI AltTabThread(LPVOID pvParam) {
    AT_LOG_TRACE;
    int direction = *((int*)pvParam);
    CreateAltTabWindow();
    ShowAltTabWindow(g_hAltTabWnd, direction);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

HWND ShowAltTabWindow(HWND& hAltTabWnd, int direction) {
#if 0
    if (g_hAltTabThread && WaitForSingleObject(g_hAltTabThread, 0) != WAIT_OBJECT_0) {
        // Move to next / previous item based on the direction
        int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        int N = (int)g_AltTabWindows.size();
        int nextInd = (selectedInd + N + direction) % N;

        ATWListViewSelectItem(nextInd);

        HWND hWnd = GetForegroundWindow();
        if (hAltTabWnd != hWnd) {
            AT_LOG_ERROR("hAltTabWnd is NOT a foreground window!");
            ActivateWindow(hAltTabWnd);
        }
        return hAltTabWnd;
    }
    if (g_hAltTabThread) {
        CloseHandle(g_hAltTabThread);
        g_hAltTabThread = nullptr;
    }

	g_hAltTabThread = CreateThread(nullptr, 0, AltTabThread, (LPVOID)(UINT_PTR)direction, CREATE_SUSPENDED, nullptr);
    if (!g_hAltTabThread)
        return nullptr;

    ResumeThread(g_hAltTabThread);

    return hAltTabWnd;
#else
    if (hAltTabWnd == nullptr) {
        // We need this to set the AltTab window active when it is brought to the foreground
        g_hFGWnd = GetForegroundWindow();
        if (!IsHungAppWindowEx(g_hFGWnd)) {
            g_idThreadAttachTo = g_hFGWnd ? GetWindowThreadProcessId(g_hFGWnd, nullptr) : 0;
            if (g_idThreadAttachTo) {
                AttachThreadInput(GetCurrentThreadId(), g_idThreadAttachTo, TRUE);
            }
        }

        hAltTabWnd = CreateAltTabWindow();

        //SetWindowPos(hAltTabWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(hAltTabWnd);
    }

    // Move to next / previous item based on the direction
    int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (selectedInd == -1) return hAltTabWnd;

    int N           = (int)g_AltTabWindows.size();
    int nextInd     = (selectedInd + N + direction) % N;

    ATWListViewSelectItem(nextInd);

    TRACKMOUSEEVENT tme;
    tme.cbSize    = sizeof(tme);
    tme.dwFlags   = TME_LEAVE;
    tme.hwndTrack = hAltTabWnd;
    TrackMouseEvent(&tme);

    return hAltTabWnd;
#endif // 0
}

void RefreshAltTabWindow() {
    AT_LOG_TRACE;

    // Clear the list
    ListView_DeleteAllItems(g_hListView);
    g_AltTabWindows.clear();

    // Enumerate windows
    EnumWindows(EnumWindowsProc, (LPARAM)(&g_AltTabWindows));

    // Identify the processes which are running from different paths
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> processMap;
    for (const auto& item : g_AltTabWindows) {
        const std::wstring key = item.ProcessName + item.Title;
        processMap[key].insert(item.FullPath);
    }
    for (auto& item : g_AltTabWindows) {
        const std::wstring key = item.ProcessName + item.Title;
        if (processMap[key].size() > 1) {
            item.IsConflictProcess = true;
        }
    }

    const int imageWidth = GetSystemMetrics(SM_CXICON);
    const int imageHeight = GetSystemMetrics(SM_CYICON);

    // Create ImageList and add icons, assign a dummy ImageList to set the row height
    // The row height is determined by the height of the icons in the ImageList assigned as LVSIL_SMALL
    // But the icons in the ImageList are drawn using the ImageList g_hLVImageList.
    HIMAGELIST hImageListDummy = ImageList_Create(imageWidth, imageHeight + 1, ILC_COLOR32 | ILC_MASK, 0, 1);
    HIMAGELIST hImageList = ImageList_Create(imageWidth, imageHeight, ILC_COLOR32 | ILC_MASK, 0, 1);

    for (const auto& item : g_AltTabWindows) {
        ImageList_AddIcon(hImageList, item.hIcon);
    }

    // Set the ImageList for the ListView
    // Assign as the small image list (LVSIL_SMALL affects row height)
    ListView_SetImageList(g_hListView, hImageListDummy, LVSIL_SMALL);
    g_hLVImageList = hImageList;

    // Add windows to ListView
    for (int i = 0; i < g_AltTabWindows.size(); ++i) {
        AddListViewItem(g_hListView, i, g_AltTabWindows[i]);
    }

    // Select the previously selected item
    ATWListViewSelectItem(g_SelectedIndex);
}

void ATWListViewSelectItem(int rowNumber) {
    if (g_AltTabWindows.empty()) {
        g_SelectedIndex = -1;
        return;
    }

    // Move to next / previous item based on the direction
    int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    LVITEM lvItem;
    lvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    lvItem.state = 0;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, selectedInd, (LPARAM)&lvItem);

    rowNumber = max(0, min(rowNumber, (int)g_AltTabWindows.size() - 1));

    lvItem.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(g_hListView, LVM_SETITEMSTATE  , rowNumber, (LPARAM)&lvItem);
    SendMessageW(g_hListView, LVM_ENSUREVISIBLE , rowNumber, (LPARAM)&lvItem);

    g_SelectedIndex = rowNumber;
}

void ATWListViewSelectPrevItem() {
    // Move to next / previous item based on the direction
    int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (selectedInd == -1) return;
    int N           = (int)g_AltTabWindows.size();
    int prevInd     = (selectedInd + N - 1) % N;

    LVITEM lvItem;
    lvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    lvItem.state = 0;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, selectedInd, (LPARAM)&lvItem);

    prevInd = max(0, min(prevInd, (int)g_AltTabWindows.size() - 1));

    lvItem.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(g_hListView, LVM_SETITEMSTATE  , prevInd, (LPARAM)&lvItem);
    SendMessageW(g_hListView, LVM_ENSUREVISIBLE , prevInd, (LPARAM)&lvItem);

    g_SelectedIndex = prevInd;

}
void ATWListViewSelectNextItem() {
    // Move to next / previous item based on the direction
    int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (selectedInd == -1) return;
    int N           = (int)g_AltTabWindows.size();
    int nextInd     = (selectedInd + N + 1) % N;

    LVITEM lvItem;
    lvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    lvItem.state = 0;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, selectedInd, (LPARAM)&lvItem);

    nextInd = max(0, min(nextInd, (int)g_AltTabWindows.size() - 1));

    lvItem.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(g_hListView, LVM_SETITEMSTATE  , nextInd, (LPARAM)&lvItem);
    SendMessageW(g_hListView, LVM_ENSUREVISIBLE , nextInd, (LPARAM)&lvItem);

    g_SelectedIndex = nextInd;
}

void ATWListViewDeleteItem(int rowNumber) {
    SendMessageW(g_hListView, LVM_DELETEITEM, rowNumber, 0);
    ATWListViewSelectItem(rowNumber);
}

int ATWListViewGetSelectedItem() {
    // Move to next / previous item based on the direction
    int selectedRow = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    return selectedRow;
}

void ATWListViewPageDown() {
    AT_LOG_TRACE;
    // Scroll down one page (assuming item height is the default)
    SendMessageW(g_hListView, LVM_SCROLL, 0, 1);

    // Optionally, you can add a delay if needed
    Sleep(100); // Sleep for 100 milliseconds

    // Scroll up one page
    SendMessageW(g_hListView, LVM_SCROLL, 0, -1);

    //SendMessageW(g_hListView, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);

    //int selectedRow = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    //LVITEM lvItem;
    //lvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    //lvItem.state = LVIS_FOCUSED | LVIS_SELECTED;
    //SendMessageW(g_hListView, LVN_KEYDOWN, (WPARAM)VK_NEXT, (LPARAM)&lvItem);
}

bool RegisterAltTabWindow() {
    AT_LOG_TRACE;

    // Register the window class
    WNDCLASS wc      = {};
    wc.lpfnWndProc   = AltTabWindowProc;
    wc.lpszClassName = CLASS_NAME;
    wc.hInstance     = g_hInstance;

    if (!RegisterClass(&wc)) {
        AT_LOG_ERROR("Failed to register AltTab Window class!");
        LogLastErrorInfo();
        return false;
    }
    return true;
}

HWND CreateAltTabWindow() {
    AT_LOG_TRACE;

    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    DWORD style   = WS_POPUP | WS_BORDER;

    // Create the window
    HWND hWnd = CreateWindowExW(
        exStyle,            // Optional window styles
        CLASS_NAME,         // Window class
        WINDOW_NAME,        // Window title
        style,              // Styles
        0,                  // X
        0,                  // Y
        0,                  // Width
        0,                  // Height
        g_hMainWnd,         // Parent window
        nullptr,            // Menu
        g_hInstance,        // Instance handle
        nullptr             // Additional application data
    );

    if (hWnd == nullptr) {
        AT_LOG_ERROR("Failed to create AltTab Window!");
        return nullptr;
    }

    // Show the window and bring to the top
    SetForegroundWindow(hWnd);
    SetWindowPos       (hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ShowWindow         (hWnd, SW_SHOWNORMAL);
    UpdateWindow       (hWnd);
    BringWindowToTop   (hWnd);

    return hWnd;
}

void AddListViewItem(HWND hListView, int index, const AltTabWindowData& windowData) {
    LVITEM lvItem    = {0};
    lvItem.mask      = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
    lvItem.iItem     = index;
    lvItem.iSubItem  = 0;
    lvItem.iImage    = index;
    lvItem.lParam    = (LPARAM)(&windowData);

    ListView_InsertItem(hListView, &lvItem);
    ListView_SetItem(hListView, &lvItem);
    ImageList_AddIcon(ListView_GetImageList(hListView, LVSIL_NORMAL), windowData.hIcon);
    ListView_SetItemText(hListView, index, 1, const_cast<wchar_t*>(windowData.Title.c_str()));
    ListView_SetItemText(hListView, index, 2, const_cast<wchar_t*>(windowData.ProcessName.c_str()));
}

static void CustomizeListView(HWND hListView, int dpi) {
    // Set extended style for the List View control
    DWORD dwExStyle = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
    ListView_SetExtendedListViewStyle(hListView, dwExStyle);

    const int colTitleWidth = g_Settings.WindowWidth - COL_ICON_WIDTH;

    // Add columns to the List View
    LVCOLUMN lvCol   = {0};
    lvCol.mask       = LVCF_TEXT | LVCF_WIDTH;
    lvCol.pszText    = (LPWSTR)L"#";
    lvCol.cx         = ScaleValueForDPI(COL_ICON_WIDTH, dpi);
    ListView_InsertColumn(hListView, 0, &lvCol);

    if (g_Settings.ShowColProcessName) {
        lvCol.pszText = (LPWSTR)L"Window Title";
        lvCol.cx      = colTitleWidth - COL_PROCNAME_WIDTH;
        ListView_InsertColumn(hListView, 1, &lvCol);

        lvCol.pszText = (LPWSTR)L"Process Name";
        lvCol.cx      = COL_PROCNAME_WIDTH - 2;
        ListView_InsertColumn(hListView, 2, &lvCol);
    } else {
        lvCol.pszText = (LPWSTR)L"Window Title";
        lvCol.cx      = colTitleWidth - 2;
        ListView_InsertColumn(hListView, 1, &lvCol);
    }
}

#define FONT_POINT(hdc, p) (-MulDiv(p, GetDeviceCaps(hdc, LOGPIXELSY), 72))

HFONT CreateFontEx(HDC hdc, const std::wstring& fontName, int fontSize, const std::wstring& fontStyle) {
    std::unordered_map<std::wstring, int> fontStyleMap = {
        { L"normal"     , FW_NORMAL },
        { L"italic"     , FW_NORMAL },
        { L"bold"       , FW_BOLD   },
        { L"bold italic", FW_BOLD   },
    };

    int  fStyle  = fontStyleMap[fontStyle];
    BOOL bItalic = fontStyle.find(L"italic") != -1;

    // Create a font for the static text control
    return CreateFontW(
        FONT_POINT(hdc, fontSize), // Font height
        0,                         // Width of each character in the font
        0,                         // Angle of escapement
        0,                         // Orientation angle
        fStyle,                    // Font weight
        bItalic,                   // Italic
        FALSE,                     // Underline
        FALSE,                     // Strikeout
        DEFAULT_CHARSET,           // Character set identifier
        OUT_DEFAULT_PRECIS,        // Output precision
        CLIP_DEFAULT_PRECIS,       // Clipping precision
        DEFAULT_QUALITY,           // Output quality
        DEFAULT_PITCH | FF_SWISS,  // Pitch and family
        fontName.c_str()           // Font face name
    );
}

static void SetListViewCustomColors(HWND hListView, COLORREF backgroundColor, COLORREF textColor) {
    // Set the background color
    SendMessageW(hListView, LVM_SETBKCOLOR,     0, (LPARAM)backgroundColor);
    SendMessageW(hListView, LVM_SETTEXTBKCOLOR, 0, (LPARAM)backgroundColor);

    // Set the text color
    SendMessageW(hListView, LVM_SETTEXTCOLOR,   0, (LPARAM)textColor);
}

LRESULT CALLBACK ListViewSubclassProc(
    HWND       hListView,
    UINT       uMsg,
    WPARAM     wParam,
    LPARAM     lParam,
    UINT_PTR   /*uIdSubclass*/,
    DWORD_PTR  /*dwRefData*/)
{
    //AT_LOG_INFO(std::format("uMsg: {:4}, wParam: {}, lParam: {}", uMsg, wParam, lParam).c_str());
    auto vkCode = wParam;
    const bool isShiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;

    switch (uMsg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        // ----------------------------------------------------------------------------
        // VK_ESCAPE
        // ----------------------------------------------------------------------------
        if (wParam == VK_ESCAPE) {
            AT_LOG_INFO("VK_ESCAPE");
            DestroyAltTabWindow();
        }
        // ----------------------------------------------------------------------------
        // VK_DOWN
        // ----------------------------------------------------------------------------
        else if (wParam == VK_DOWN) {
            AT_LOG_INFO("Down Pressed!");
            ATWListViewSelectNextItem();
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // VK_DOWN
        // ----------------------------------------------------------------------------
        else if (wParam == VK_UP) {
            AT_LOG_INFO("Up Pressed!");
            ATWListViewSelectPrevItem();
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // VK_HOME / VK_PRIOR
        // ----------------------------------------------------------------------------
        else if (vkCode == VK_HOME || vkCode == VK_PRIOR) {
            AT_LOG_INFO("Home/PageUp Pressed!");
            if (!g_AltTabWindows.empty()) {
                ATWListViewSelectItem(0);
            }
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // VK_END / VK_NEXT
        // ----------------------------------------------------------------------------
        else if (vkCode == VK_END || vkCode == VK_NEXT) {
            AT_LOG_INFO("End/PageDown Pressed!");
            if (!g_AltTabWindows.empty()) {
                ATWListViewSelectItem((int)g_AltTabWindows.size() - 1);
            }
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // VK_DELETE
        // ----------------------------------------------------------------------------
        else if (vkCode == VK_DELETE) {
            if (isShiftPressed) {
                AT_LOG_INFO("Shift+Delete Pressed!");
                int ind = ATWListViewGetSelectedItem();
                if (ind == -1)
                    return TRUE;
                g_AltTabWindows[ind].IsBeingClosed = true;
                TerminateProcessEx(g_AltTabWindows[ind].PID);
            } else {
                AT_LOG_INFO("Delete Pressed!");

                // Send the SC_CLOSE command to the window
                const int ind = ATWListViewGetSelectedItem();
                if (ind == -1)
                    return TRUE;
                AT_LOG_INFO("Ind: %d, Title: %s", ind, WStrToUTF8(g_AltTabWindows[ind].Title).c_str());
                ATCloseWindow(ind);
            }
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // Backtick
        // ----------------------------------------------------------------------------
        else if (vkCode == VK_OEM_3) { // 0xC0 - '`~' for US
            AT_LOG_INFO("Backtick Pressed!, g_IsAltBacktick = %d", g_IsAltBacktick);
            const int direction = isShiftPressed ? -1 : 1;

            // Move to next / previous same item based on the direction
            const int selectedInd = ATWListViewGetSelectedItem();
            if (selectedInd == -1)
                return TRUE;

            const int N = (int)g_AltTabWindows.size();
            const auto& processName = g_AltTabWindows[selectedInd].ProcessName; // Selected process name
            const int pgInd = GetProcessGroupIndex(processName);                // Index in ProcessGroupList
            int nextInd = (selectedInd + N + direction) % N;                    // Next index to select

            // If the AltTab window is invoked with Alt + Backtick, then we should
            // move to the next item in the list without checking the process name.
            if (g_IsAltBacktick) {
                ATWListViewSelectItem(nextInd);
                return TRUE;
            }

            // If the control comes to here, AltTab window is invoked with Alt + Tab
            // Check if the next item is similar to the selected item
            for (int i = 1; i < N; ++i) {
                nextInd = (selectedInd + N + i * direction) % N;
                if (IsSimilarProcess(pgInd, g_AltTabWindows[nextInd].ProcessName)) {
                    break;
                }
                if (EqualsIgnoreCase(processName, g_AltTabWindows[nextInd].ProcessName)) {
                    break;
                }
                nextInd = -1;
            }

            if (nextInd != -1)
                ATWListViewSelectItem(nextInd);
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // VK_F1
        // ----------------------------------------------------------------------------
        else if (vkCode == VK_F1) {
            DestroyAltTabWindow();
            if (isShiftPressed) {
                DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), nullptr, ATAboutDlgProc);
            } else {
                ShowHelpWindow();
            }
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // Shift + VK_F1
        // ----------------------------------------------------------------------------
        else if (isShiftPressed && vkCode == VK_F1) {
            DestroyAltTabWindow();
            DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), nullptr, ATAboutDlgProc);
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // VK_F2
        // ----------------------------------------------------------------------------
        else if (vkCode == VK_F2) {
            DestroyAltTabWindow();
            // Do NOT assign owner for this, if owner is assigned and the settings
            // dialog is not active, then it won't be displayed in alt tab windows list.
            if (g_hSettingsWnd == nullptr) {
                DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), nullptr, ATSettingsDlgProc);
            } else {
                SetForegroundWindow(g_hSettingsWnd);
            }
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // VK_ENTER
        // ----------------------------------------------------------------------------
        else if (vkCode == VK_RETURN) {
            AT_LOG_INFO("Enter Pressed!");
            DestroyAltTabWindow(true);
            return TRUE;
        }
        // ----------------------------------------------------------------------------
        // WM_CHAR won't be sent when ALT is pressed, this is the alternative to handle when a key is pressed.
        // And collect the chars and form a search string.
        // Ignore Backtick (`), Tab, Backspace, and non-printable characters while forming search string.
        // ----------------------------------------------------------------------------
        else {
            wchar_t ch = '\0';
            bool isChar = ATMapVirtualKey((UINT)wParam, ch);
            bool update = false;
            if (!(wParam == '`' || wParam == VK_TAB || wParam == VK_BACK || ch == '\0') && isChar) {
                g_SearchString += ch;
                update = true;
            } else if (wParam == VK_BACK && !g_SearchString.empty()) {
                g_SearchString.pop_back();
                update = true;
            }
            AT_LOG_INFO("Char: %#4x, SearchString: [%s]", ch, WStrToUTF8(g_SearchString).c_str());
            update&& SendMessageW(g_hStaticText, WM_SETTEXT, 0, (LPARAM)(L"Search String: " + g_SearchString).c_str());
        }
        // AT_LOG_INFO("Not handled: wParam: %0#4x, iswprint: %d", wParam, iswprint((wint_t)wParam));
    } break;

    case WM_NOTIFY: {
        AT_LOG_INFO("WM_NOTIFY");
        //LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lParam);
        //if (nmhdr->code == LVN_ITEMCHANGED) {
        //    LPNMLISTVIEW pnmListView = reinterpret_cast<LPNMLISTVIEW>(nmhdr);

        //    if ((pnmListView->uChanged & LVIF_STATE) && (pnmListView->uNewState & LVIS_SELECTED)) {
        //        // The mouse has moved over the item
        //        // You can show a message or perform any action here
        //        MessageBox(hListView, L"Mouse moved over item!", L"ListView Notification", MB_OK | MB_ICONINFORMATION);
        //    }
        //}
    } break;

    // ----------------------------------------------------------------------------
    // Owner draw item
    // ----------------------------------------------------------------------------
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT lpDrawItemStruct = (LPDRAWITEMSTRUCT)lParam;
        return AT::ATListViewDrawItem(hListView, lpDrawItemStruct);
    }
    }

    return DefSubclassProc(hListView, uMsg, wParam, lParam);
}

void WindowResizeAndPosition(HWND hWnd, int wndWidth, int wndHeight) {
    // Get the dimensions of the screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Calculate the position to center the window
    int xPos = (screenWidth - wndWidth) / 2;
    int yPos = (screenHeight - wndHeight) / 2;

    // Set the window position
    SetWindowPos(hWnd, HWND_TOP, xPos, yPos, wndWidth, wndHeight, SWP_NOSIZE | SWP_SHOWWINDOW);
}

int GetColProcessNameWidth() {
    if (g_Settings.ShowColProcessName) {
        return COL_PROCNAME_WIDTH;
    }
    return 0;
}

INT_PTR CALLBACK AltTabWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    //AT_LOG_TRACE;
    //AT_LOG_INFO(std::format("uMsg: {:4}, wParam: {}, lParam: {}", uMsg, wParam, lParam).c_str());

    switch (uMsg) {
        HANDLE_MSG(hWnd, WM_ACTIVATE      , ATW_OnActivate      );
        HANDLE_MSG(hWnd, WM_CLOSE         , ATW_OnClose         );
        HANDLE_MSG(hWnd, WM_COMMAND       , ATW_OnCommand       );
        HANDLE_MSG(hWnd, WM_CONTEXTMENU   , ATW_OnContextMenu   );
        HANDLE_MSG(hWnd, WM_CREATE        , ATW_OnCreate        );
        HANDLE_MSG(hWnd, WM_CTLCOLORSTATIC, ATW_OnCtlColorStatic);
        HANDLE_MSG(hWnd, WM_DESTROY       , ATW_OnDestroy       );
        HANDLE_MSG(hWnd, WM_DRAWITEM      , ATW_OnDrawItem      );
        HANDLE_MSG(hWnd, WM_KEYDOWN       , ATW_OnKeyDown       );
        HANDLE_MSG(hWnd, WM_KILLFOCUS     , ATW_OnKillFocus     );
        HANDLE_MSG(hWnd, WM_LBUTTONDOWN   , ATW_OnLButtonDown   );
        HANDLE_MSG(hWnd, WM_NOTIFY        , ATW_OnNotify        );
        HANDLE_MSG(hWnd, WM_SYSCOMMAND    , ATW_OnSysCommand    );
        HANDLE_MSG(hWnd, WM_TIMER         , ATW_OnTimer         );

        default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

bool IsInvisibleWin10BackgroundAppWindow(HWND hWnd) {
    BOOL isCloaked;
    DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &isCloaked, sizeof(BOOL));
    return isCloaked;
}

/**
 * Check if the given window handle's window is a AltTab window.
 * 
 * \param hWnd Window handle
 * \return True if the given hWnd is a AltTab window otherwise false.
 */
bool IsAltTabWindow(HWND hWnd) {
    if (!IsWindowVisible(hWnd))
        return false;

    HWND hOwner = GetOwnerWindowHwnd(hWnd);

    if (GetLastActivePopup(hOwner) != hWnd)
        return false;

    // Even the owner window is hidden we are getting the window styles, so make
    // sure that the owner window is visible before checking the window styles
    DWORD ownerES = GetWindowLong(hOwner, GWL_EXSTYLE);
    if (ownerES && IsWindowVisible(hOwner) && !((ownerES & WS_EX_TOOLWINDOW) && !(ownerES & WS_EX_APPWINDOW))
        && !IsInvisibleWin10BackgroundAppWindow(hOwner)) {
        return true;
    }

    DWORD windowES = GetWindowLong(hWnd, GWL_EXSTYLE);
    if (windowES && !((windowES & WS_EX_TOOLWINDOW) && !(windowES & WS_EX_APPWINDOW))
        && !IsInvisibleWin10BackgroundAppWindow(hWnd)) {
        return true;
    }

    if (windowES == 0 && ownerES == 0) {
        return true;
    }

    return false;
}

/**
 * Get owner window handle for the given hWnd.
 * 
 * \param hWnd Window handle
 * \return The owner window handle for the given hWnd.
 */
HWND GetOwnerWindowHwnd(HWND hWnd) {
    HWND hOwner = hWnd;
    do {
        hOwner = GetWindow(hOwner, GW_OWNER);
    } while (GetWindow(hOwner, GW_OWNER));
    hOwner = hOwner ? hOwner : hWnd;
    return hOwner;
}

/*!
 * \brief Show AltTab window's context menu at the center of the selected item.
 */
void ShowContextMenuAtItemCenter()
{
    // Get the position of the selected item
    // Get the bounding rectangle of the selected item
    RECT itemRect;
    ListView_GetItemRect(g_hListView, g_SelectedIndex, &itemRect, LVIR_BOUNDS);

    // Calculate the center of the entire row
    POINT center;
    center.x = (itemRect.left + itemRect.right) / 2;
    center.y = (itemRect.top + itemRect.bottom) / 2;

    // Convert to screen coordinates if needed
    ClientToScreen(g_hListView, &center);

    ShowContextMenu(g_hAltTabWnd, center);
}

// ----------------------------------------------------------------------------
// Show AltTab context menu
// ----------------------------------------------------------------------------
void ShowContextMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_CONTEXTMENU));
    if (hMenu) {
        HMENU hSubMenu = GetSubMenu(hMenu, 0);
        if (hSubMenu) {
            // respect menu drop alignment
            UINT uFlags = TPM_RIGHTBUTTON;
            if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0) {
                uFlags |= TPM_RIGHTALIGN;
            } else {
                uFlags |= TPM_LEFTALIGN;
            }

            // Use TPM_RETURNCMD flag let TrackPopupMenuEx function return the
            // menu item identifier of the user's selection in the return value.
            uFlags |= TPM_RETURNCMD;
            UINT menuItemId = TrackPopupMenuEx(hSubMenu, uFlags, pt.x, pt.y, hWnd, nullptr);

            ContextMenuItemHandler(hWnd, hSubMenu, menuItemId);
        }
        DestroyMenu(hMenu);
    }
}

void SetAltTabActiveWindow() {
    SetForegroundWindow(g_hAltTabWnd);
    SetActiveWindow(g_hAltTabWnd);
    SetFocus(g_hListView);
}

void CopyToClipboard(const std::wstring& text) {
    if (OpenClipboard(g_hAltTabWnd)) {
        EmptyClipboard();
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (hGlobal) {
            wchar_t* pGlobal = (wchar_t*)GlobalLock(hGlobal);
            if (pGlobal) {
                wcscpy_s(pGlobal, text.size() + 1, text.c_str());
                GlobalUnlock(hGlobal);
                SetClipboardData(CF_UNICODETEXT, hGlobal);
            }
        }
        CloseClipboard();
    } else {
        AT_LOG_ERROR("OpenClipboard failed");
    }
}

void BrowseToFile(const std::wstring& filepath) {
    if (!filepath.empty()) {
        LPITEMIDLIST pidl = ILCreateFromPathW(filepath.c_str());
        if (pidl) {
            SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            ILFree(pidl);
        }
    }
}

// ----------------------------------------------------------------------------
// AltTab window context menu handler
// ----------------------------------------------------------------------------
void ContextMenuItemHandler(HWND hWnd, HMENU /*hSubMenu*/, UINT menuItemId) {
    switch (menuItemId) {
    case ID_CONTEXTMENU_CLOSE_WINDOW: {
        // Send the SC_CLOSE command to the window
        const int ind = ATWListViewGetSelectedItem();
        if (ind != -1) {
            ATCloseWindow(ind);
        }
    }
    break;

    case ID_CONTEXTMENU_KILL_PROCESS: {
        const int ind = ATWListViewGetSelectedItem();
        if (ind != -1) {
            TerminateProcessEx(g_AltTabWindows[ind].PID);
        }
    }
    break;

    case ID_CONTEXTMENU_CLOSEALLWINDOWS: {
        AT_LOG_INFO("ID_CONTEXTMENU_CLOSEALLWINDOWS");

        // Here, this MessageBox is also displayed in AltTab windows list. Did
        // not find a way to avoid this. So, turning off the timer.
        KillTimer(hWnd, TIMER_WINDOW_COUNT);

        bool closeAllWindows = true;
        if (g_Settings.PromptTerminateAll) {
            int result = MessageBoxW(
                hWnd,
                L"Are you sure you want to close all windows?",
                AT_PRODUCT_NAMEW L": Close All Windows",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            closeAllWindows = (result == IDYES);
        }

        if (closeAllWindows) {
            for (int i = 0; i < g_AltTabWindows.size(); ++i) {
                ATCloseWindow(i);
            }
        } else {
            SetAltTabActiveWindow();
        }

        SetTimer(hWnd, TIMER_WINDOW_COUNT, TIMER_WINDOW_COUNT_ELAPSE, nullptr);
    }
    break;

    case ID_CONTEXTMENU_KILLALLPROCESSES: {
        AT_LOG_INFO("ID_CONTEXTMENU_KILLALLPROCESSES");

        // Here, this MessageBox is also displayed in AltTab windows list. Did
        // not find a way to avoid this. So, turning off the timer.
        KillTimer(hWnd, TIMER_WINDOW_COUNT);

        bool terminateAllWindows = true;
        if (g_Settings.PromptTerminateAll) {
            int result = MessageBoxW(
                hWnd,
                L"Are you sure you want to terminate all windows?",
                AT_PRODUCT_NAMEW L": Close All Windows",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            terminateAllWindows = (result == IDYES);
        }
        if (terminateAllWindows) {
            for (auto& g_AltTabWindow : g_AltTabWindows) {
                TerminateProcessEx(g_AltTabWindow.PID);
            }
        } else {
            SetAltTabActiveWindow();
        }

        SetTimer(hWnd, TIMER_WINDOW_COUNT, TIMER_WINDOW_COUNT_ELAPSE, nullptr);
    }
    break;

    case ID_CONTEXTMENU_OPEN_PATH: {
        AT_LOG_INFO("ID_CONTEXTMENU_OPEN_PATH");
        const auto filepath = g_AltTabWindows[ATWListViewGetSelectedItem()].FullPath;
        DestroyAltTabWindow();
        BrowseToFile(filepath);
    } break;
    
    case ID_CONTEXTMENU_COPY_PATH: {
        AT_LOG_INFO("ID_CONTEXTMENU_COPY_PATH");
        const auto filepath = g_AltTabWindows[ATWListViewGetSelectedItem()].FullPath;
        DestroyAltTabWindow();
        CopyToClipboard(filepath);
    } break;

    case ID_CONTEXTMENU_COPY_TITLE: {
        AT_LOG_INFO("ID_CONTEXTMENU_COPY_TITLE");
        const auto title = g_AltTabWindows[ATWListViewGetSelectedItem()].Title;
        DestroyAltTabWindow();
        CopyToClipboard(title);
    } break;

    case ID_CONTEXTMENU_ABOUTALTTAB:
    case ID_TRAYCONTEXTMENU_ABOUTALTTAB: {
        AT_LOG_INFO("ID_CONTEXTMENU_ABOUTALTTAB");
        DestroyAltTabWindow();
        DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), g_hMainWnd, ATAboutDlgProc);
    }
    break;

    case ID_CONTEXTMENU_SETTINGS: {
        AT_LOG_INFO("ID_CONTEXTMENU_SETTINGS");
        DestroyAltTabWindow();
        DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), g_hMainWnd, ATSettingsDlgProc);
    }
    break;

    case ID_TRAYCONTEXTMENU_README:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_README");
        ShowReadMeWindow();
        break;

    case ID_TRAYCONTEXTMENU_HELP:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_HELP");
        ShowHelpWindow();
        break;

    case ID_TRAYCONTEXTMENU_RELEASENOTES:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RELEASENOTES");
        ShowReleaseNotesWindow();
        break;

    case ID_TRAYCONTEXTMENU_CHECKFORUPDATES: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_CHECKFORUPDATES");
        DestroyAltTabWindow();
        HideCustomToolTip();
        ShowCustomToolTip(L"Checking for updates..., please wait.");

         // Had to run CheckForUpdates in a thread to display the tooltip... :-(
        std::thread thr(CheckForUpdates, false);
        thr.detach(); // Let the thread run independently, otherwise tooltip won't be displayed.
    } break;

    case ID_TRAYCONTEXTMENU_RELOADALTTABSETTINGS: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RELOADALTTABSETTINGS");
        ATLoadSettings();
        ShowCustomToolTip(L"Settings reloaded successfully.", 3000);
    } break;

    case ID_TRAYCONTEXTMENU_RESTART:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RESTART");
        DestroyAltTabWindow();
        RestartApplication();
        break;

    case ID_CONTEXTMENU_EXIT:
    case ID_TRAYCONTEXTMENU_EXIT:
        AT_LOG_INFO("ID_CONTEXTMENU_EXIT");
        DestroyAltTabWindow();
        PostQuitMessage(0);
        //int result = MessageBoxW(
        //    hWnd,
        //    L"Are you sure you want to exit?",
        //    AT_PRODUCT_NAMEW,
        //    MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
        //if (result == IDOK) {
        //    PostQuitMessage(0);
        //}
        break;
    }
}

/*!
 * Terminate the given process id
 * 
 * \param pid  ProcessID
 * \return True if terminated successfully otherwise false.
 */
BOOL TerminateProcessEx(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        return TRUE;
    }
    return FALSE;
}

void ATCloseWindow(const int index) {
    if (index >= 0 && index < g_AltTabWindows.size()) {
        AltTabWindowData& windowData = g_AltTabWindows[index];
        windowData.IsBeingClosed = true;
        AT_LOG_INFO("ATCloseWindow: index: %d, hWnd: %#x, hOwner: %#x, Title: %ls", index, windowData.hWnd, windowData.hOwner, windowData.Title.c_str());

        // Send REDRAW message immediately
        ListView_RedrawItems(g_hListView, index, index);
        UpdateWindow(g_hListView);

        // Allow some time for the redraw to complete so that the use can see the change
        Sleep(50);

        // If there is a modal dialog box is open for the window, both WM_CLOSE and SC_CLOSE will
        // close the dialog box on sending the WM_CLOSE on the hWnd instead of closing the actual window.
        // The reason behind this is actual window handle (hOwner) is not the foreground window, instead 
        // the dialog box is the foreground window.
        // And sending WM_CLOSE to hOwner will not close the actual window if there is no dialog box.
        // So, first send SC_CLOSE to hWnd, wait for some time and then send SC_CLOSE to hOwner if hOwner
        // is different than hWnd.
        SendMessageW(windowData.hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        Sleep(50);

        // If the hWnd is not same as hOwner then also send the SC_CLOSE to hOwner
        if (windowData.hOwner != windowData.hWnd && IsWindow(windowData.hOwner)) {
            SendMessageW(windowData.hOwner, WM_SYSCOMMAND, SC_CLOSE, 0);
        }

        // TODO: Check if still the window is not closed then try to send WM_CLOSE
    }
}

bool ATMapVirtualKey(UINT uCode, wchar_t& vkCode) {
    wchar_t ch = static_cast<wchar_t>(uCode);
    //AT_LOG_INFO("uCode: %c, ch: %c", uCode, ch);

    if (!iswprint((wint_t)uCode)) {
        vkCode = '\0';
        return false;
    }

    bool isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Check for alphabetic and digit keys
    if ((uCode >= 'A' && uCode <= 'Z') || (uCode >= '0' && uCode <= '9')) {
        ch = (wchar_t)MapVirtualKey(uCode, MAPVK_VK_TO_CHAR);

        // Adjust the case based on the Shift key state for alphabets
        if (!isShiftPressed && ch >= L'A' && ch <= L'Z') {
            ch = ch - L'A' + L'a';
        }

        if (isShiftPressed && ch >= L'0' && ch <= L'9') {
            ch = L")!@#$%^&*("[ch - L'0'];
        }

        vkCode = ch;
        return true;
    }

    // Handle other special characters
    switch (uCode) {
        case VK_SPACE:        vkCode = L' ';                          return true;
        case VK_OEM_MINUS:    vkCode = isShiftPressed ? L'_' : L'-';  return true;
        case VK_OEM_PLUS:     vkCode = isShiftPressed ? L'=' : L'+';  return true;
        case VK_OEM_1:        vkCode = isShiftPressed ? L':' : L';';  return true;
        case VK_OEM_2:        vkCode = isShiftPressed ? L'?' : L'/';  return true;
        case VK_OEM_3:        vkCode = isShiftPressed ? L'~' : L'`';  return true;
        case VK_OEM_4:        vkCode = isShiftPressed ? L'{' : L'[';  return true;
        case VK_OEM_5:        vkCode = isShiftPressed ? L'|' : L'\\'; return true;
        case VK_OEM_6:        vkCode = isShiftPressed ? L'}' : L']';  return true;
        case VK_OEM_7:        vkCode = isShiftPressed ? L'"' : L'\''; return true;
        case VK_OEM_COMMA:    vkCode = isShiftPressed ? L'<' : L',';  return true;
        case VK_OEM_PERIOD:   vkCode = isShiftPressed ? L'>' : L'.';  return true;
        case VK_OEM_102:      vkCode = isShiftPressed ? L'>' : L'<';  return true;
    }
    return false;
}

std::vector<AltTabWindowData> GetAltTabWindows() {
    std::vector<AltTabWindowData> altTabWindows;
    EnumWindows(EnumWindowsProc, (LPARAM)(&altTabWindows));
    return altTabWindows;
}

/*!
 * Check if the process name is in the exclusion list
 * 
 * \param processName Process name, should be in lower case
 * \return True if the processName is in the exclusion list otherwise false
 */
bool IsExcludedProcess(const std::wstring& processName) {
    bool excluded = false;
    if (g_Settings.ProcessExclusionsEnabled) {
        excluded = std::find(
           g_Settings.ProcessExclusionList.begin(),
           g_Settings.ProcessExclusionList.end(),
           processName) != g_Settings.ProcessExclusionList.end();
    }
    return excluded;
}

// ----------------------------------------------------------------------------
// AltTab window procedure handlers
// ----------------------------------------------------------------------------

BOOL ATW_OnCreate(HWND hWnd, LPCREATESTRUCT /*lpCreateStruct*/) {
    AT_LOG_TRACE;

    g_hAltTabWnd = hWnd;

    // Get screen width and height
    const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Compute the window size (e.g., 80% of the screen width and height)
    const int windowWidth = static_cast<int>(screenWidth * g_Settings.WidthPercentage * 0.01);
    const int windowHeight = static_cast<int>(screenHeight * g_Settings.HeightPercentage * 0.01);

    // Compute the window position (centered on the screen)
    const int windowX = (screenWidth - windowWidth) / 2;
    const int windowY = (screenHeight - windowHeight) / 2;
    DWORD style = WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDRAWFIXED;
    if (!g_Settings.ShowColHeader) {
        style |= LVS_NOCOLUMNHEADER;
    }

    // Create Static control for the search string
    int staticTextHeight = 24;

    // Calculate the required height for the static control based on font size
    HDC hdc = GetDC(hWnd);

    g_hSSFont = CreateFontEx(hdc, g_Settings.SSFontName, g_Settings.SSFontSize, g_Settings.SSFontStyle);
    g_hLVFont = CreateFontEx(hdc, g_Settings.LVFontName, g_Settings.LVFontSize, g_Settings.LVFontStyle);

    SelectObject(hdc, g_hSSFont);

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    staticTextHeight = (int)(tm.tmHeight + tm.tmExternalLeading + 1);

    // Get the DPI of the window/screen
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);

    ReleaseDC(hWnd, hdc);

    // While creating static search string control, use 0 for height and
    // use -1 for the rest of the calculations while adjusting window size.
    if (!g_Settings.ShowSearchString) {
        staticTextHeight = 0;
    }

    int X = 0;
    int Y = 0;
    int width = windowWidth;
    int height = staticTextHeight;

    // AT_LOG_INFO("StaticTextControl: X: %d, Y: %d, width: %d, height: %d", X, Y, width, height);

    // Create a static text control
    HWND hStaticText = CreateWindowW(
        L"Static",                         // Static text control class
        L"Search String: empty",           // Text content
        WS_CHILD | WS_VISIBLE | SS_CENTER, // Styles
        X,                                 // X
        Y,                                 // Y
        width,                             // Width
        height,                            // Height
        hWnd,                              // Parent window
        (HMENU) nullptr,                   // Menu or control ID (set to NULL for static text)
        g_hInstance,                       // Instance handle
        nullptr                            // No window creation data
    );

    SendMessageW(hStaticText, WM_SETFONT, (WPARAM)g_hSSFont, 0);
    g_hStaticText = hStaticText;

    // Here adding 1 pixel to the Y position to avoid the static text control overlap with the ListView control
    if (g_Settings.ShowSearchString) {
        Y += staticTextHeight + 1;
    } else {
        staticTextHeight = -1;
    }

    // Here, reducing the window width by 1 (-1) to fit the scrollbar properly in the window.
    // height is reduced by 3 (-3)
    //  1 pixel for the static text control and listView control overlap
    //  2 pixels for the upper and bottom border in listview control
    width = windowWidth - 1;
    height = windowHeight - staticTextHeight - 3;

    // AT_LOG_INFO("ListViewControl: X: %d, Y: %d, width: %d, height: %d", X, Y, width, height);

    // Create ListView control
    HWND hListView = CreateWindowExW(
        0,                   // Optional window styles
        WC_LISTVIEW,         // Predefined class
        L"",                 // No window title
        style,               // Styles
        X,                   // X
        Y,                   // Y
        width,               // Width
        height,              // Height
        hWnd,                // Parent window
        (HMENU)IDC_LISTVIEW, // Control identifier
        g_hInstance,         // Instance handle
        nullptr              // No window creation data
    );

    SendMessageW(hListView, WM_SETFONT, (WPARAM)g_hLVFont, MAKELPARAM(TRUE, 0));
    g_hListView = hListView;

    // Subclass the ListView control
    SetWindowSubclass(hListView, ListViewSubclassProc, 1, 0);

    int wndWidth = (int)(screenWidth * g_Settings.WidthPercentage * 0.01);
    int wndHeight = (int)(screenHeight * g_Settings.HeightPercentage * 0.01);

    g_Settings.WindowWidth = wndWidth;
    g_Settings.WindowHeight = wndHeight;

    // Add header / columns
    CustomizeListView(hListView, dpi);

    // Set ListView background and font colors
    SetListViewCustomColors(hListView, g_Settings.LVBackgroundColor, g_Settings.LVFontColor);

    // Set window transparency
    SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hWnd, RGB(255, 255, 255), (BYTE)g_Settings.Transparency, LWA_ALPHA);

    // std::vector<AltTabWindowData> altTabWindows;
    EnumWindows(EnumWindowsProc, (LPARAM)(&g_AltTabWindows));
    AT_LOG_INFO("g_AltTabWindows.size() : %d", g_AltTabWindows.size());

    // Identify the processes which are running from different paths
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> processMap;
    for (const auto& item : g_AltTabWindows) {
        const std::wstring key = item.ProcessName + item.Title;
        processMap[key].insert(item.FullPath);
    }
    for (auto& item : g_AltTabWindows) {
        const std::wstring key = item.ProcessName + item.Title;
        if (processMap[key].size() > 1) {
            item.IsConflictProcess = true;
        }
    }

    // Create ImageList and add icons
    const int imageWidth = GetSystemMetrics(SM_CXICON);
    const int imageHeight = GetSystemMetrics(SM_CYICON);

    // Create ImageList and add icons, assign a dummy ImageList to set the row height
    // The row height is determined by the height of the icons in the ImageList assigned as LVSIL_SMALL
    // But the icons in the ImageList are drawn using the ImageList g_hLVImageList.
    HIMAGELIST hImageListDummy = ImageList_Create(imageWidth, imageHeight + 1, ILC_COLOR32 | ILC_MASK, 0, 1);
    HIMAGELIST hImageList = ImageList_Create(imageWidth, imageHeight, ILC_COLOR32 | ILC_MASK, 0, 1);

    for (const auto& item : g_AltTabWindows) {
        ImageList_AddIcon(hImageList, item.hIcon);
    }

    // Set the ImageList for the ListView
    // Assign as the small image list (LVSIL_SMALL affects row height)
    ListView_SetImageList(hListView, hImageListDummy, LVSIL_SMALL);
    g_hLVImageList = hImageList;

    // Add windows to ListView
    for (int i = 0; i < g_AltTabWindows.size(); ++i) {
        AddListViewItem(hListView, i, g_AltTabWindows[i]);
    }

    // Compute the required height and resize the ListView
    // Get the header control associated with the ListView
    HWND hHeader = ListView_GetHeader(g_hListView);
    int headerHeight = 0;
    if (hHeader) {
        RECT rcHeader;
        GetClientRect(hHeader, &rcHeader);
        headerHeight = rcHeader.bottom - rcHeader.top;
    }
    RECT rcListView;
    GetClientRect(g_hListView, &rcListView);
    int itemHeight =
        ListView_GetItemRect(g_hListView, 0, &rcListView, LVIR_BOUNDS) ? rcListView.bottom - rcListView.top : 0;
    int itemCount = ListView_GetItemCount(g_hListView);
    int requiredHeight = itemHeight * itemCount + headerHeight + staticTextHeight + 3;

    if (requiredHeight <= g_Settings.WindowHeight) {
        SetWindowPos(hWnd, HWND_TOPMOST, windowX, windowY, windowWidth, requiredHeight, SWP_NOZORDER);
        WindowResizeAndPosition(hWnd, wndWidth, requiredHeight);
    } else {
        int scrollBarWidth = GetSystemMetrics(SM_CXVSCROLL);
        int processNameWidth = GetColProcessNameWidth();
        int colTitleWidth = g_Settings.WindowWidth - (COL_ICON_WIDTH + processNameWidth) - scrollBarWidth - 1;
        int lvHeight = (g_Settings.WindowHeight - itemHeight + 1) / itemHeight * itemHeight;
        requiredHeight = lvHeight + headerHeight + staticTextHeight + 3;

        ListView_SetColumnWidth(hListView, 1, colTitleWidth);

        // Here, reducing the window width by 1 (-1) to fit the scrollbar properly in the window.
        SetWindowPos(hListView, nullptr, 0, 0, windowWidth - 1, lvHeight, SWP_NOMOVE | SWP_NOZORDER);
        SetWindowPos(hWnd, HWND_TOPMOST, windowX, windowY, windowWidth, requiredHeight, SWP_NOZORDER);
        WindowResizeAndPosition(hWnd, wndWidth, requiredHeight);
    }

    SetForegroundWindow(hWnd);
    SetFocus(hListView);

    // Select the first row
    LVITEM lvItem;
    lvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    lvItem.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(hListView, LVM_SETITEMSTATE, 0, (LPARAM)&lvItem);

    // Create a timer to refresh the ListView when there is a change in windows
    SetTimer(hWnd, TIMER_WINDOW_COUNT, TIMER_WINDOW_COUNT_ELAPSE, nullptr);

    return TRUE;
}

LRESULT ATW_OnCtlColorStatic(HWND hWnd, HDC hDC, HWND hCtl, UINT /*type*/) {
    AT_LOG_TRACE;
    if (hCtl == g_hStaticText) {
        // Set the text color and background color of the static text control
        SetTextColor(hDC, g_Settings.SSFontColor);
        SetBkColor(hDC, g_Settings.SSBackgroundColor);

        return (INT_PTR)CreateSolidBrush(g_Settings.SSBackgroundColor);
    }
    return DefWindowProc(hWnd, WM_CTLCOLORSTATIC, (WPARAM)hDC, (LPARAM)hCtl);
}

void ATW_OnCommand(HWND /*hwnd*/, int id, HWND /*hwndCtl*/, UINT /*codeNotify*/) {
    AT_LOG_TRACE;
    if (id == IDOK || id == IDCANCEL) {
        PostQuitMessage(0);
    }
}

void ATW_OnContextMenu(HWND hwnd, HWND /*hwndContext*/, UINT xPos, UINT yPos) {
    AT_LOG_TRACE;

    POINT pt = { (LONG)xPos, (LONG)yPos };
    ShowContextMenu(hwnd, pt);
}

void ATW_OnSysCommand(HWND hwnd, UINT cmd, int x, int y) {
    AT_LOG_TRACE;

    if (cmd == SC_KEYMENU && (x == -1 && y == -1) /*VK_APPS*/) {
        // Alt+Space pressed, handle it here
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        TrackPopupMenu(GetSystemMenu(hwnd, FALSE), 0, cursorPos.x, cursorPos.y, 0, hwnd, nullptr);
    }
}

void ATW_OnClose(HWND /*hwnd*/) {
    AT_LOG_TRACE;

    // Release the fonts
    if (g_hLVFont != nullptr) {
        DeleteObject(g_hLVFont);
    }
    if (g_hSSFont != nullptr) {
        DeleteObject(g_hSSFont);
    }

    PostQuitMessage(0);
}

void ATW_OnKeyDown(HWND /*hwnd*/, UINT vk, BOOL /*fDown*/, int /*cRepeat*/, UINT /*flags*/) {
    AT_LOG_TRACE;

    if (vk == VK_ESCAPE) {
        AT_LOG_INFO("WM_KEYDOWN: VK_ESCAPE");
        // Close the window when Escape key is pressed
        DestroyAltTabWindow();
    }
}

void ATW_OnKillFocus(HWND /*hwnd*/, HWND /*hwndNewFocus*/) {
    AT_LOG_TRACE;
}

void ATW_OnLButtonDown(HWND /*hwnd*/, BOOL /*fDoubleClick*/, int /*x*/, int /*y*/, UINT /*keyFlags*/) {
    AT_LOG_TRACE;
}

void ATW_OnTimer(HWND /*hwnd*/, UINT /*id*/) {
    //AT_LOG_TRACE;
    std::vector<AltTabWindowData> altTabWindows;
    EnumWindows(EnumWindowsProc, (LPARAM)(&altTabWindows));
    // AT_LOG_INFO("altTabWindows.size(): %d, g_AltTabWindows.size(): %d", altTabWindows.size(),
    // g_AltTabWindows.size());
    bool doRefresh = altTabWindows.size() != g_AltTabWindows.size();
    if (!doRefresh) {
        // Deep compare the two vectors and update only if there is a change
        for (int i = 0; i < altTabWindows.size(); ++i) {
            const auto& a = altTabWindows[i];
            const auto& b = g_AltTabWindows[i];
            if (a.hWnd != b.hWnd || a.hOwner != b.hOwner || a.PID != b.PID || a.Title != b.Title
                || a.ProcessName != b.ProcessName) {
                doRefresh = true;
                break;
            }
        }
    }
    if (doRefresh) {
        RefreshAltTabWindow();
    }
}

void ATW_OnActivate(HWND /*hwnd*/, UINT state, HWND /*hwndActDeact*/, BOOL /*fMinimized*/) {
    AT_LOG_TRACE;
    if (state == WA_INACTIVE) {
        AT_LOG_INFO("WM_ACTIVATE: The application is becoming inactive, close the window");
        // The application is becoming inactive, close the window
        DestroyAltTabWindow();
    }
}

void ATW_OnDestroy(HWND hwnd) {
    AT_LOG_TRACE;
    KillTimer(hwnd, TIMER_WINDOW_COUNT);
}

void ATW_OnDrawItem(HWND /*hwnd*/, const DRAWITEMSTRUCT* lpDrawItem) {
    //AT_LOG_TRACE;
    //AT_LOG_INFO("hwndItem: %#x CtlType: %d, itemID: %2d, itemAction: %d, itemState: %#4x",
    //    lpDrawItem->hwndItem,
    //    lpDrawItem->CtlType,
    //    lpDrawItem->itemID,
    //    lpDrawItem->itemAction,
    //    lpDrawItem->itemState);
    // See if the message is from our ListView control then forward to subclass
    if (lpDrawItem->CtlType == ODT_LISTVIEW) {
        // Forward to subclass
        SendMessageW(lpDrawItem->hwndItem, WM_DRAWITEM, 0, (LPARAM)lpDrawItem);
    }
}

BOOL ATW_OnNotify(HWND /*hwnd*/, int /*idFrom*/, NMHDR* pnmhdr) {
    if (pnmhdr->hwndFrom == g_hListView && pnmhdr->code == LVN_HOTTRACK) {
        LPNMLISTVIEW pnmListView = reinterpret_cast<LPNMLISTVIEW>(pnmhdr);

        // After adding LVS_OWNERDRAWFIXED to listview, we are getting -1 in iItem
        // So, try to get the item under the cursor position using hit test.
        POINT pt;
        GetCursorPos(&pt);

        const POINT ptScreenPos = pt;

        ScreenToClient(g_hListView, &pt);
        const POINT ptClientPos = pt;

        if (pnmListView->iItem < 0) {
            LVHITTESTINFO ht = { 0 };
            ht.pt = ptClientPos;
            if (ListView_SubItemHitTest(g_hListView, &ht) != -1) {
                pnmListView->iItem = ht.iItem;
            }
        }
        // Check if the mouse is over on close button area then change the button image to active
        if (g_rcBtnClose.left <= ptClientPos.x && ptClientPos.x <= g_rcBtnClose.right
            && g_rcBtnClose.top <= ptClientPos.y && ptClientPos.y <= g_rcBtnClose.bottom) {
            if (!g_IsMouseOverCloseButton) {
                g_IsMouseOverCloseButton = true;
                // Invalidate the button area to redraw
                InvalidateRect(g_hListView, &g_rcBtnClose, FALSE);
            }
        } else {
            if (g_IsMouseOverCloseButton) {
                g_IsMouseOverCloseButton = false;
                // Invalidate the button area to redraw
                InvalidateRect(g_hListView, &g_rcBtnClose, FALSE);
            }
        }

        if (pnmListView->iItem != g_nLVHotItem) {
            const int oldItem = g_nLVHotItem;
            g_nLVHotItem = pnmListView->iItem;

            // Invalidate only old + new row
            if (oldItem >= 0) {
                RECT rc;
                ListView_GetItemRect(g_hListView, oldItem, &rc, LVIR_BOUNDS);
                InvalidateRect(g_hListView, &rc, FALSE);
            }

            if (g_nLVHotItem >= 0) {
                RECT rc;
                ListView_GetItemRect(g_hListView, g_nLVHotItem, &rc, LVIR_BOUNDS);
                InvalidateRect(g_hListView, &rc, FALSE);
            }
        }

        // Check if the mouse is hovering over an item
        if (g_Settings.ShowProcessInfoTooltip && pnmListView->iItem >= 0
            && (pnmListView->iItem != g_MouseHoverIndex || !g_TooltipVisible)) {
            g_MouseHoverIndex = pnmListView->iItem;

            HideCustomToolTip();

            // The mouse is over an item
            const std::wstring tooltip = std::format(
                L"Title: {}\nPath: {}\nDescription: {} {}\nCompanyName: {}\nPID: {}",
                g_AltTabWindows[g_MouseHoverIndex].Title,
                g_AltTabWindows[g_MouseHoverIndex].FullPath,
                g_AltTabWindows[g_MouseHoverIndex].Description,
                g_AltTabWindows[g_MouseHoverIndex].Version,
                g_AltTabWindows[g_MouseHoverIndex].CompanyName,
                g_AltTabWindows[g_MouseHoverIndex].PID);

            // Show the tooltip just below the item
            RECT itemRect;
            if (ListView_GetItemRect(g_hListView, g_MouseHoverIndex, &itemRect, LVIR_BOUNDS)) {
                pt.x = ptClientPos.x;
                pt.y = itemRect.bottom + 1;
                ClientToScreen(g_hListView, &pt);
                pt.x = ptScreenPos.x + 36;
                ShowCustomToolTipAt(tooltip, pt, -1);
            }
        }
        return TRUE;
    }

    if (pnmhdr->code == NM_CLICK) {
        // Check if the single-click event is from your ListView control
        if (pnmhdr->hwndFrom == g_hListView) {
            // First check if the mouse is over on close button area
            if (g_IsMouseOverCloseButton) {
                // Close the window of the item under mouse cursor
                if (g_nLVHotItem != -1) {
                    ATCloseWindow(g_nLVHotItem);
                }
            } else {
                DestroyAltTabWindow(true);
            }
            return TRUE;
        }
    }

    return FALSE;
}
