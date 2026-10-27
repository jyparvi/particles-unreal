#include "Particles.h"

#include "ParticleController.h"
#include "CanvasItem.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "Internationalization/Text.h"
#include "Misc/App.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FParticlesModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        AddShaderSourceDirectoryMapping(
            TEXT("/Particles"),
            FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"))
        );
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(
    FParticlesModule,
    Particles,
    "Particles"
);

AParticlesGameMode::AParticlesGameMode()
{
    DefaultPawnClass = nullptr;
    SpectatorClass = nullptr;
    bStartPlayersAsSpectators = true;
    HUDClass = AParticlesHUD::StaticClass();
    PlayerControllerClass = AParticlesPlayerController::StaticClass();
}

AParticlesPlayerController::AParticlesPlayerController()
{
    bAutoManageActiveCameraTarget = false;
}

void AParticlesHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!ParticleSystem.IsValid())
    {
        if (TActorIterator<AParticleController> It(GetWorld()); It)
        {
            ParticleSystem = *It;
        }
    }

    const AParticleController* System = ParticleSystem.Get();
    if (!System || !Canvas || !GEngine)
    {
        return;
    }

    const FString Text = FString::Printf(
        TEXT("Particles: %s\nFPS: %.0f"),
        *FText::AsNumber(System->particleCount).ToString(),
        1.0f / FMath::Max(static_cast<float>(FApp::GetDeltaTime()), 0.0001f)
    );
    FCanvasTextItem TextItem(FVector2D(16.0f, 16.0f), FText::FromString(Text), GEngine->GetMediumFont(), FLinearColor::White);
    TextItem.Scale = FVector2D(3.0f, 3.0f);
    Canvas->DrawItem(TextItem);
}
