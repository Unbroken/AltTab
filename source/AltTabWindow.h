#pragma once

#include <string>
#include <vector>
#include <wtypes.h>
#include <set>

#define CLASS_NAME   L"__AltTab_WndCls__"
#define WINDOW_NAME  L"AltTab Window"

/*!
 * @brief Structure to hold AltTab window data
 */
struct AltTabWindowData {
    HWND          hWnd;                  // Window handle
    HWND          hOwner;                // Owner window handle
    HICON         hIcon;                 // Window icon
    std::wstring  Title;                 // Window title
    std::wstring  ProcessName;           // Process name of the window
    std::wstring  FullPath;              // Full path of the process
    std::wstring  Description;           // File description of the process
    std::wstring  CompanyName;           // Company name of the process
    DWORD         PID;                   // Process ID
    bool          IsConflictProcess;     // Indicates if the process is running from different paths.
    std::wstring  Version;               // File version of the process
    bool          IsBeingClosed;         // Indicates if the window is being closed

    std::set<std::pair<size_t, size_t>> TitleHighlights;
    std::set<std::pair<size_t, size_t>> ProcessNameHighlights;

    // Compare operator
    bool operator==(const AltTabWindowData& other) const {
        const bool isSame = hWnd == other.hWnd && hOwner == other.hOwner && PID == other.PID && Title == other.Title
                            && ProcessName == other.ProcessName;
        if (!isSame) {
            return false;
        }

        // Compare the TitleHighlights sets
        if (TitleHighlights.size() != other.TitleHighlights.size()) {
            return false;
        }
        for (const auto& highlight : TitleHighlights) {
            if (other.TitleHighlights.find(highlight) == other.TitleHighlights.end()) {
                return false;
            }
        }

        // Compare the ProcessNameHighlights sets
        if (ProcessNameHighlights.size() != other.ProcessNameHighlights.size()) {
            return false;
        }
        for (const auto& highlight : ProcessNameHighlights) {
            if (other.ProcessNameHighlights.find(highlight) == other.ProcessNameHighlights.end()) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(const AltTabWindowData& other) const {
        return !(*this == other);
    }
};

/*!
 * \brief Register AltTab window class
 * 
 * \return Return true if the class is registered successfully otherwise false.
 */
bool RegisterAltTabWindow();

/*!
 * \brief Create AltTab main window
 */
HWND CreateAltTabWindow();

/**
 * \brief Show AltTab window
 * 
 * \param hAltTabWnd AltTab window handle
 * \param direction  Direction which tells to select next or previous item
 * \return 
 */
HWND ShowAltTabWindow(HWND& hAltTabWnd, int direction);

/**
 * \brief Clear the existing items and re-create the AltTab window with new windows list.
 */
void RefreshAltTabWindow();

void ATWListViewSelectItem(int rowNumber);
void ATWListViewSelectNextItem();
void ATWListViewSelectPrevItem();

void ATWListViewDeleteItem(int rowNumber);

int  ATWListViewGetSelectedItem();

void ATWListViewPageDown();

void ShowContextMenuAtItemCenter();

void ShowContextMenu(HWND hWnd, POINT pt);

void SetAltTabActiveWindow();

/**
 * \brief Translate the virtual key code to a character value.
 * 
 * \param[in]  uCode    The virtual key code or scan code value of the key.
 * \param[out] vkCode   Either a virtual-key code or a character value.
 * 
 * \return Return true if the given uCode is a printable character otherwise false.
 */
bool ATMapVirtualKey(UINT uCode, wchar_t& ch);

std::vector<AltTabWindowData> GetAltTabWindows();
