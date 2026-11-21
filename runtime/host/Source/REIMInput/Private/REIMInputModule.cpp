#include "Modules/ModuleManager.h"

/* Input capture and host settings live here (M2-D1/D4): AReimPlayerController
 * forwards keys/mouse to the Bridge, and UReimHostSettingsSubsystem owns
 * instance.json host settings. No gameplay state is stored in this module. */
IMPLEMENT_MODULE(FDefaultModuleImpl, REIMInput);
