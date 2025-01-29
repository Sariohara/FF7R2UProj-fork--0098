#pragma once
#include "CoreMinimal.h"
#include "EndAssetDataSoft.generated.h"

class UObject;

USTRUCT(BlueprintType)
struct ENDGAME_API FEndAssetDataSoft {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName AssetName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<TSoftObjectPtr<UObject>> AssetUserData;
    
    FEndAssetDataSoft();
};

