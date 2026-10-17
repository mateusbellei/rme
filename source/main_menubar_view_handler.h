#ifndef RME_MAIN_MENUBAR_VIEW_HANDLER_H_
#define RME_MAIN_MENUBAR_VIEW_HANDLER_H_

class wxCommandEvent;
class MainMenuBar;

namespace MenuBarViewHandler {
void SyncViewSettingsFromMenu(MainMenuBar* menu);
void CheckViewSettingsOnMenu(MainMenuBar* menu);
}

#endif
