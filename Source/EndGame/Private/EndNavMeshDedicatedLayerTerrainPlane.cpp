#include "EndNavMeshDedicatedLayerTerrainPlane.h"
#include "NavAreas/NavArea_Default.h"

AEndNavMeshDedicatedLayerTerrainPlane::AEndNavMeshDedicatedLayerTerrainPlane(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bIsEditorOnlyActor = true;
    this->AreaClass = UNavArea_Default::StaticClass();
}


