#include "ReimGameInstance.h"

#include "ReimLog.h"

void UReimGameInstance::Init()
{
	Super::Init();
	UE_LOG(LogREIM, Display, TEXT("REIM GameInstance init"));
}

void UReimGameInstance::Shutdown()
{
	UE_LOG(LogREIM, Display, TEXT("REIM GameInstance shutdown"));
	Super::Shutdown();
}
