#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EndPathWayDrawComponent.generated.h"

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENDGAME_API UEndPathWayDrawComponent : public UActorComponent {
    GENERATED_BODY()
public:
    UEndPathWayDrawComponent(const FObjectInitializer& ObjectInitializer);

};

