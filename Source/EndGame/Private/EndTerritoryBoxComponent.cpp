#include "EndTerritoryBoxComponent.h"
#include "NavAreas/NavArea_Obstacle.h"

UEndTerritoryBoxComponent::UEndTerritoryBoxComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->AreaClass = UNavArea_Obstacle::StaticClass();
}


