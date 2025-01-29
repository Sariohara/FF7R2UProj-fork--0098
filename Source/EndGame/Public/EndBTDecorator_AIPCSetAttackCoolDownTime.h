#pragma once
#include "CoreMinimal.h"
#include "EndBTDecorator_InvokeBase.h"
#include "EndBTDecorator_AIPCSetAttackCoolDownTime.generated.h"

UCLASS(Blueprintable)
class UEndBTDecorator_AIPCSetAttackCoolDownTime : public UEndBTDecorator_InvokeBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bForce;
    
    UEndBTDecorator_AIPCSetAttackCoolDownTime();

};

