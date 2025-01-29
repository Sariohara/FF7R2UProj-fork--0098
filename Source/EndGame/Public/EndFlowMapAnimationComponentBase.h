#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EndAnimComponentInterface.h"
#include "EndFlowMapAnimationComponentBase.generated.h"

class UMeshComponent;

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENDGAME_API UEndFlowMapAnimationComponentBase : public UActorComponent, public IEndAnimComponentInterface {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    TArray<UMeshComponent*> Meshes;
    
public:
    UEndFlowMapAnimationComponentBase(const FObjectInitializer& ObjectInitializer);


    // Fix for true pure virtual functions not being implemented
};

