#include "framework.h"
#include <chrono>

/*!
 * @brief Check for updates
 * @param quiteMode Whether to run in quiet mode or not. If true, it will not show any message boxes or dialogs.
 */
void CheckForUpdates(const bool quiteMode = false);

std::chrono::system_clock::time_point ReadLastCheckForUpdatesTS();

void WriteCheckForUpdatesTS(const std::chrono::system_clock::time_point& timestamp);
