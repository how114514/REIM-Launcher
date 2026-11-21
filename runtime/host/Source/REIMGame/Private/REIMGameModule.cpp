#include "Modules/ModuleManager.h"

/* One game module must be the primary module: in monolithic Game/Shipping
 * builds this defines GInternalProjectName/GIsGameAgnosticExe and the game's
 * module boilerplate (docs/DEVELOPMENT.md, M2-A3). */
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, REIMGame, "REIM");
