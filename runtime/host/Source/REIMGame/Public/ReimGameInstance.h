#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"

#include "ReimGameInstance.generated.h"

/** M2 Host GameInstance. The Bridge and renderer live in subsystems/actors;
 *  this class only anchors the lifecycle. */
UCLASS()
class REIMGAME_API UReimGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;
};
