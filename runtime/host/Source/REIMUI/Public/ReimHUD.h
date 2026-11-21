#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "ReimHUD.generated.h"

class AReimPlayerController;

/** Minimal R0 HUD (M2-C7/D3): loading progress, Bridge/TPS numbers and the
 *  pause/exit menu. It contains no Minecraft UI and cannot change world
 *  state; menu actions only save host settings or request process exit. */
UCLASS()
class REIMUI_API AReimHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawMenu(AReimPlayerController* PlayerController);
	void DrawFatal();
	void DrawLoading(const struct ReimRenderFrame& Frame);
};
