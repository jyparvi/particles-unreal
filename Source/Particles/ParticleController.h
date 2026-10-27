#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParticleController.generated.h"

UCLASS()
class PARTICLES_API AParticleController : public AActor
{
    GENERATED_BODY()

public:
    AParticleController();
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(EditAnywhere, Category = "Particles", meta = (ClampMin = "1", ClampMax = "1000000"))
    int32 particleCount = 500;

    UPROPERTY(EditAnywhere, Category = "Particles", meta = (ClampMin = "0.1"))
    float particleSize = 5.0f;

    UPROPERTY(EditAnywhere, Category = "Particles", meta = (ClampMin = "0.0"))
    float minSpeed = 100.0f;

    UPROPERTY(EditAnywhere, Category = "Particles", meta = (ClampMin = "0.0"))
    float maxSpeed = 400.0f;

    UPROPERTY(EditAnywhere, Category = "Particles", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float velocityStretch = 0.04f;

    UPROPERTY(EditAnywhere, Category = "Particles", meta = (DisplayName = "D20 Rotation Speed"))
    FVector d20RotationSpeed = FVector(10.0f, 15.0f, 10.0f);

    UPROPERTY(EditAnywhere, Category = "Particles")
    FLinearColor nearParticleColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Particles")
    FLinearColor farParticleColor = FLinearColor(1.0f, 1.0f, 0.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Particles|Glow")
    bool enableGlow = true;

    UPROPERTY(EditAnywhere, Category = "Particles|Glow", meta = (HideAlphaChannel))
    FLinearColor glowColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, Category = "Particles|Glow", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float glowIntensity = 4.0f;

    UPROPERTY(EditAnywhere, Category = "Particles|Glow", meta = (ClampMin = "0.25", ClampMax = "8.0"))
    float glowSharpness = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Particles|Post Processing", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float bloomAmount = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Particles|Post Processing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float filmGrainAmount = 0.5f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY()
    TObjectPtr<class AStaticMeshActor> d20Mesh;

    UPROPERTY(Transient)
    TObjectPtr<class UStaticMeshComponent> d20MeshComponent;

    FMatrix44f prevD20WorldToLocal;

    TArray<FVector4f> BuildD20Planes() const;
    void RunParticleSimulation(float DeltaTime);

    TSharedPtr<struct FParticleSimulationResources, ESPMode::ThreadSafe> particleResources;
    TSharedPtr<class FParticleRenderViewExtension, ESPMode::ThreadSafe> particleRenderer;
};
