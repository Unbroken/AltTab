// AltTabTester.cpp : Defines the entry point for the application.
//

#include "PreCompile.h"
#include "GlobalData.h"
#include "AltTabTester.h"
#include "AltTabSettings.h"
#include "AltTabWindow.h"
#include "Logger.h"
#include "Utils.h"

#pragma comment(linker, "/entry:wWinMainCRTStartup /subsystem:console")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "gdiplus.lib")

#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "/manifestdependency:\"type='win32' "                                                                              \
    "name='Microsoft.Windows.Common-Controls' "                                                                        \
    "version='6.0.0.0' "                                                                                               \
    "processorArchitecture='*' "                                                                                       \
    "publicKeyToken='6595b64144ccf1df' "                                                                               \
    "language='*' "                                                                                                    \
    "\"")

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// ----------------------------------------------------------------------------
// Global Variables:
// ----------------------------------------------------------------------------
HINSTANCE       g_hInstance;                                   // Current instance
HHOOK           g_KeyboardHook;                                // Keyboard Hook
HWND            g_hAltTabWnd           = nullptr;              // AltTab window handle
HWND            g_hFGWnd               = nullptr;              // Foreground window handle
HWND            g_hMainWnd             = nullptr;              // AltTab main window handle
HWND            g_hSettingsWnd         = nullptr;              // AltTab settings window handle
UINT_PTR        g_TooltipTimerId;
bool            g_TooltipVisible       = false;                // Is tooltip visible or not
TOOLINFO        g_ToolInfo             = {};                   // Custom tool tip
bool            g_IsAltKeyPressed      = false;                // Is Alt key pressed
DWORD           g_LastAltKeyPressTime  = 0;                    // Last Alt key press time
bool            g_IsAltTab             = false;                // Is Alt+Tab pressed
bool            g_IsAltCtrlTab         = false;                // Is Alt+Ctrl+Tab pressed
bool            g_IsAltBacktick        = false;                // Is Alt+Backtick pressed
DWORD           g_MainThreadID         = GetCurrentThreadId(); // Main thread ID
DWORD           g_idThreadAttachTo     = 0;
HIMAGELIST      g_hImageList           = nullptr;
HIMAGELIST      g_hLVImageList         = nullptr;
int             g_nImgCloseActiveInd   = -1;
int             g_nImgCloseInactiveInd = -1;

IsHungAppWindowFunc g_pfnIsHungAppWindow = nullptr;


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_ALTTAB, szWindowClass, MAX_LOADSTRING);

    // Init globals for AltTab
    g_hInstance = hInstance;

#ifdef _AT_LOGGER
    CreateLogger();
    AT_LOG_INFO("-------------------------------------------------------------------------------");
    AT_LOG_INFO("CreateLogger done.");
#endif // _AT_LOGGER

    // Perform application initialization:
#if 0
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ALTTAB));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
#endif // 0

    // Initialize common things
    RegisterAltTabWindow();

    // Initialize the common things like common controls, GDI+ etc.
    InitGDIPlus();
    InitializeCOM();
    InitImageList();

    // Load GeneralSettings
    
    CreateCustomToolTip();

    // ----------------------------------------------------------------------------
    // Start writing your code from here...
    // ----------------------------------------------------------------------------
    ShowCustomToolTip(L"Initializing AltTab...", 1000);

    // Load settings from AltTabSettings.ini file
    ATLoadSettings();


    // AltTab settings dialog
     //DialogBox(hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), nullptr, ATAboutDlgProc);
    //DialogBox(hInstance, MAKEINTRESOURCE(IDD_SETTINGS), nullptr, ATSettingsDlgProc);

    g_IsAltCtrlTab = true;
    ShowAltTabWindow(g_hAltTabWnd, 1);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ALTTAB));
    MSG msg;
    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}
