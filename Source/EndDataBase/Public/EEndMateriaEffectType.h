#pragma once
#include "CoreMinimal.h"
#include "EEndMateriaEffectType.generated.h"

UENUM(BlueprintType)
enum class EEndMateriaEffectType : uint8 {
    None,
    UseAbility,
    empty,
    UseEnemyAbility,
    UseAbilitySummon,
    ItemATBUnlimited,
    MateriaUseAbility,
    PowerUpGuard,
    PowerUpJustGuard,
    UseMugAbility,
    AIPCPartyPinchUseProvocation,
    UseAbilitySpecialCommand,
    AIPCUseWeaponAbility,
    AIPCUseUniqueAbility,
    AddCombinationGaugeOutsider,
    DistributeLimitGauge,
    UseAbilityDarkness,
    UseAbilityZeninage,
    UseAbilityGamble,
    Area = 50,
    AddAttribute,
    AddStatusChangeResist,
    DamageAbsorbHP,
    DamageAbsorbMP,
    AIPCUCPCCommandUseMagic,
    APScale,
    SubSpendMPPercent,
    AIPCUseAbility,
    AddMateriaLevel,
    AddMateriaPower,
    CastMagicShort,
    HPScale = 100,
    MPScale,
    BurstATB,
    MagicScale,
    LuckScale,
    GilScale,
    ExpScale,
    StartATB,
    VariationCommandATB,
    SequenceCommandPartyATB,
    ItemEffectScale,
    StrengthScale,
    VitalityScale,
    SpiritScale,
    DexterityScale,
    SwapHPMaxMPMax,
    SwapStrengthMagic,
    SwapVitalitySpirit,
};

