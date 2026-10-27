#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Particles.generated.h"

UCLASS()
class PARTICLES_API AParticlesHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

private:
    TWeakObjectPtr<class AParticleController> ParticleSystem;
};

UCLASS()
class PARTICLES_API AParticlesPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AParticlesPlayerController();
};

UCLASS()
class PARTICLES_API AParticlesGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AParticlesGameMode();
};
