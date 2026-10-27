#include "ParticleController.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "PipelineStateCache.h"
#include "RHIResources.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessInputs.h"
#include "SceneTexturesConfig.h"
#include "SceneRendering.h"
#include "CommonRenderResources.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "StaticMeshResources.h"
#include "EngineUtils.h"

namespace
{
    constexpr uint32 ThreadGroupSize = 256;
    constexpr int32 MaxParticleCount = 1000000;

    struct FParticle
    {
        alignas(PLATFORM_MAC ? 16 : alignof(FVector3f)) FVector3f position;
        alignas(PLATFORM_MAC ? 16 : alignof(FVector3f)) FVector3f velocity;
    };
}

struct FParticleFrameParameters
{
    uint32 particleCount;
    float deltaTime;
    float minSpeed;
    float maxSpeed;
    FMatrix44f d20WorldToLocal;
    FMatrix44f prevD20WorldToLocal;
    FMatrix44f d20LocalToWorld;
    float particleSize;
    float velocityStretch;
    FLinearColor nearParticleColor;
    FLinearColor farParticleColor;
    FVector3f glowColor;
    float glowIntensity;
    float glowSharpness;
};

struct FParticleSimulationResources
{
    TRefCountPtr<FRDGPooledBuffer> particleBuffer;
    TRefCountPtr<FRDGPooledBuffer> d20PlaneBuffer;
    uint32 frameIndex = 0;
    uint32 allocatedParticleCount = 0;
    uint32 d20PlaneCount = 0;
    FParticleFrameParameters parameters;
};

class FParticleSimulationCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FParticleSimulationCS);
    SHADER_USE_PARAMETER_STRUCT(FParticleSimulationCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<Particle>, particles)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, d20Planes)
        SHADER_PARAMETER(uint32, particleCount)
        SHADER_PARAMETER(uint32, initializeParticles)
        SHADER_PARAMETER(uint32, frameIndex)
        SHADER_PARAMETER(float, deltaTime)
        SHADER_PARAMETER(float, minSpeed)
        SHADER_PARAMETER(float, maxSpeed)
        SHADER_PARAMETER(uint32, d20PlaneCount)
        SHADER_PARAMETER(FMatrix44f, d20WorldToLocal)
        SHADER_PARAMETER(FMatrix44f, prevD20WorldToLocal)
        SHADER_PARAMETER(FMatrix44f, d20LocalToWorld)
    END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FParticleSimulationCS, "/Particles/ParticleCompute.usf", "MainCS", SF_Compute);

class FParticleRenderVS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FParticleRenderVS);
    SHADER_USE_PARAMETER_STRUCT(FParticleRenderVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<Particle>, particles)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, d20Planes)
        SHADER_PARAMETER(float, particleSize)
        SHADER_PARAMETER(float, velocityStretch)
        SHADER_PARAMETER(FLinearColor, nearParticleColor)
        SHADER_PARAMETER(FLinearColor, farParticleColor)
        SHADER_PARAMETER(FMatrix44f, worldToView)
        SHADER_PARAMETER(uint32, d20PlaneCount)
        SHADER_PARAMETER(FMatrix44f, d20WorldToLocal)
    END_SHADER_PARAMETER_STRUCT()
};

class FParticleRenderPS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FParticleRenderPS);
    SHADER_USE_PARAMETER_STRUCT(FParticleRenderPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER(FVector3f, glowColor)
        SHADER_PARAMETER(float, glowIntensity)
        SHADER_PARAMETER(float, glowSharpness)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};

BEGIN_SHADER_PARAMETER_STRUCT(FParticleRenderPassParameters, )
    SHADER_PARAMETER_STRUCT_INCLUDE(FParticleRenderVS::FParameters, VS)
    SHADER_PARAMETER_STRUCT_INCLUDE(FParticleRenderPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER(FParticleRenderVS, "/Particles/ParticleRender.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FParticleRenderPS, "/Particles/ParticleRender.usf", "MainPS", SF_Pixel);

class FParticleRenderViewExtension final : public FWorldSceneViewExtension
{
public:
    // Connect the particle renderer to the controller's world and shared GPU resources.
    FParticleRenderViewExtension(
        const FAutoRegister& AutoRegister,
        AParticleController* InSystem,
        TSharedPtr<FParticleSimulationResources, ESPMode::ThreadSafe> InResources
    )
        : FWorldSceneViewExtension(AutoRegister, InSystem->GetWorld())
        , System(InSystem)
        , Resources(MoveTemp(InResources))
    {
    }

    // Apply bloom and film grain settings to the current view.
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& View) override
    {
        if (const AParticleController* Actor = System.Get())
        {
            FPostProcessSettings& Settings = View.FinalPostProcessSettings;
            Settings.BloomMethod = BM_SOG;
            Settings.BloomIntensity = FMath::Clamp(Actor->bloomAmount, 0.0f, 10.0f);
            Settings.BloomGaussianIntensity = 1.0f;
            Settings.BloomThreshold = 1.0f;
            Settings.FilmGrainIntensity = FMath::Clamp(Actor->filmGrainAmount, 0.0f, 1.0f);
        }
    }

    // Draw particle billboards before post-processing.
    virtual void PrePostProcessPass_RenderThread(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessingInputs& Inputs
    ) override
    {
        if (!Resources->particleBuffer.IsValid() || !Resources->d20PlaneBuffer.IsValid() || !Inputs.SceneTextures)
        {
            return;
        }

        const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
        TShaderMapRef<FParticleRenderVS> VertexShader(ShaderMap);
        TShaderMapRef<FParticleRenderPS> PixelShader(ShaderMap);

        FRDGTextureRef SceneColor = (*Inputs.SceneTextures)->SceneColorTexture;
        FRDGTextureRef SceneDepth = (*Inputs.SceneTextures)->SceneDepthTexture;

        const FIntRect ViewRect = static_cast<const FViewInfo&>(View).ViewRect;

        FRDGBufferRef ParticleBuffer = GraphBuilder.RegisterExternalBuffer(Resources->particleBuffer, TEXT("ParticleBuffer"));
        FRDGBufferRef D20PlaneBuffer = GraphBuilder.RegisterExternalBuffer(Resources->d20PlaneBuffer, TEXT("D20PlaneBuffer"));
        const FParticleFrameParameters& parameters = Resources->parameters;
        FParticleRenderPassParameters* PassParameters = GraphBuilder.AllocParameters<FParticleRenderPassParameters>();

        PassParameters->VS.View = View.ViewUniformBuffer;
        PassParameters->VS.particles = GraphBuilder.CreateSRV(ParticleBuffer);
        PassParameters->VS.d20Planes = GraphBuilder.CreateSRV(D20PlaneBuffer);
        PassParameters->VS.particleSize = parameters.particleSize;
        PassParameters->VS.velocityStretch = parameters.velocityStretch;
        PassParameters->VS.nearParticleColor = parameters.nearParticleColor;
        PassParameters->VS.farParticleColor = parameters.farParticleColor;
        PassParameters->VS.worldToView = FMatrix44f(View.ViewMatrices.GetWorldToView());
        PassParameters->VS.d20PlaneCount = Resources->d20PlaneCount;
        PassParameters->VS.d20WorldToLocal = parameters.d20WorldToLocal;
        PassParameters->PS.View = View.ViewUniformBuffer;
        PassParameters->PS.glowColor = parameters.glowColor;
        PassParameters->PS.glowIntensity = parameters.glowIntensity;
        PassParameters->PS.glowSharpness = parameters.glowSharpness;
        PassParameters->PS.RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
        PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
            SceneDepth,
            ERenderTargetLoadAction::ELoad,
            ERenderTargetLoadAction::ENoAction,
            FExclusiveDepthStencil::DepthRead_StencilNop
        );

        const uint32 DrawParticleCount = Resources->allocatedParticleCount;

        GraphBuilder.AddPass(
            RDG_EVENT_NAME("Particles"),
            PassParameters,
            ERDGPassFlags::Raster,
            [VertexShader, PixelShader, PassParameters, ViewRect, DrawParticleCount](FRDGAsyncTask, FRHICommandList& RHICmdList)
            {
                FGraphicsPipelineStateInitializer GraphicsPSOInit;
                RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
                RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

                GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
                GraphicsPSOInit.BlendState = TStaticBlendState<
                    CW_RGBA,
                    BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha,
                    BO_Add, BF_One, BF_InverseSourceAlpha
                >::GetRHI();
                GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
                GraphicsPSOInit.PrimitiveType = PT_TriangleList;
                GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
                GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
                GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

                SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
                SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
                SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

                RHICmdList.DrawPrimitive(0, DrawParticleCount * 2, 1);
            }
        );
    }

private:
    TWeakObjectPtr<AParticleController> System;
    TSharedPtr<FParticleSimulationResources, ESPMode::ThreadSafe> Resources;
};

// Enable per-frame particle updates.
AParticleController::AParticleController()
{
    PrimaryActorTick.bCanEverTick = true;
}

// Initialize the camera and particle simulation.
void AParticleController::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
    {
        PlayerController->SetViewTarget(*TActorIterator<ACameraActor>(GetWorld()));
    }

    d20MeshComponent = d20Mesh->GetStaticMeshComponent();
    prevD20WorldToLocal = FMatrix44f(d20MeshComponent->GetComponentTransform().ToInverseMatrixWithScale());

    particleResources = MakeShared<FParticleSimulationResources, ESPMode::ThreadSafe>();
    ENQUEUE_RENDER_COMMAND(InitializeD20Planes)(
        [Resources = particleResources, planes = BuildD20Planes()](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            Resources->d20PlaneCount = planes.Num();
            FRDGBufferRef D20PlaneBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("D20PlaneBuffer"), planes);
            GraphBuilder.QueueBufferExtraction(D20PlaneBuffer, &Resources->d20PlaneBuffer);
            GraphBuilder.Execute();
        }
    );
    particleRenderer = FSceneViewExtensions::NewExtension<FParticleRenderViewExtension>(this, particleResources);
}

// Release the particle renderer and shared GPU resource references.
void AParticleController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    particleRenderer.Reset();
    particleResources.Reset();
    Super::EndPlay(EndPlayReason);
}

// Rotate the D20 object and run the particle simulation.
void AParticleController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    particleCount = FMath::Clamp(particleCount, 1, MaxParticleCount);

    const FVector deltaRotation = d20RotationSpeed * FMath::DegreesToRadians(DeltaTime);
    d20Mesh->AddActorWorldRotation(
        FQuat(FVector::UpVector, deltaRotation.Y) *
        FQuat(FVector::RightVector, deltaRotation.X) *
        FQuat(FVector::ForwardVector, deltaRotation.Z)
    );

    RunParticleSimulation(DeltaTime);
}

// Update shader parameters, rebuild the particle buffer when needed, and dispatch the simulation.
void AParticleController::RunParticleSimulation(float DeltaTime)
{
    FParticleFrameParameters parameters;
    parameters.particleCount = particleCount;
    parameters.deltaTime = DeltaTime;
    parameters.minSpeed = minSpeed;
    parameters.maxSpeed = maxSpeed;

    const FTransform d20Transform = d20MeshComponent->GetComponentTransform();
    parameters.d20WorldToLocal = FMatrix44f(d20Transform.ToInverseMatrixWithScale());
    parameters.prevD20WorldToLocal = prevD20WorldToLocal;
    parameters.d20LocalToWorld = FMatrix44f(d20Transform.ToMatrixWithScale());
    prevD20WorldToLocal = parameters.d20WorldToLocal;

    parameters.particleSize = FMath::Max(particleSize, 0.1f);
    parameters.velocityStretch = FMath::Clamp(velocityStretch, 0.0f, 0.5f);
    parameters.nearParticleColor = nearParticleColor;
    parameters.farParticleColor = farParticleColor;
    parameters.glowColor = FVector3f(glowColor.R, glowColor.G, glowColor.B);
    parameters.glowIntensity = enableGlow ? FMath::Clamp(glowIntensity, 0.0f, 10.0f) : 0.0f;
    parameters.glowSharpness = FMath::Clamp(glowSharpness, 0.25f, 8.0f);

    ENQUEUE_RENDER_COMMAND(ParticleSimulationDispatch)(
        [Resources = particleResources, parameters](FRHICommandListImmediate& RHICmdList)
        {
            Resources->parameters = parameters;
            FRDGBuilder GraphBuilder(RHICmdList);
            const bool rebuildParticles = !Resources->particleBuffer.IsValid() || Resources->allocatedParticleCount != parameters.particleCount;
            FRDGBufferRef ParticleBuffer = rebuildParticles
                ? GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FParticle), parameters.particleCount), TEXT("ParticleBuffer"))
                : GraphBuilder.RegisterExternalBuffer(Resources->particleBuffer, TEXT("ParticleBuffer"));
            FRDGBufferRef D20PlaneBuffer = GraphBuilder.RegisterExternalBuffer(Resources->d20PlaneBuffer, TEXT("D20PlaneBuffer"));

            FParticleSimulationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FParticleSimulationCS::FParameters>();
            Parameters->particles = GraphBuilder.CreateUAV(ParticleBuffer);
            Parameters->d20Planes = GraphBuilder.CreateSRV(D20PlaneBuffer);
            Parameters->particleCount = parameters.particleCount;
            Parameters->initializeParticles = rebuildParticles;
            Parameters->frameIndex = Resources->frameIndex++;
            Parameters->deltaTime = parameters.deltaTime;
            Parameters->minSpeed = parameters.minSpeed;
            Parameters->maxSpeed = parameters.maxSpeed;
            Parameters->d20PlaneCount = Resources->d20PlaneCount;
            Parameters->d20WorldToLocal = parameters.d20WorldToLocal;
            Parameters->prevD20WorldToLocal = parameters.prevD20WorldToLocal;
            Parameters->d20LocalToWorld = parameters.d20LocalToWorld;

            TShaderMapRef<FParticleSimulationCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("ParticleSimulation"),
                ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
                ComputeShader,
                Parameters,
                FIntVector(FMath::DivideAndRoundUp(parameters.particleCount, ThreadGroupSize), 1, 1)
            );

            GraphBuilder.QueueBufferExtraction(ParticleBuffer, &Resources->particleBuffer);
            GraphBuilder.Execute();
            Resources->allocatedParticleCount = parameters.particleCount;
        }
    );
}

// Build normalized outward planes in the D20 object's local space.
TArray<FVector4f> AParticleController::BuildD20Planes() const
{
    TArray<FVector4f> planes;
    const UStaticMesh* Mesh = d20MeshComponent->GetStaticMesh();
    const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
    const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
    const FPositionVertexBuffer& vertices = LOD.VertexBuffers.PositionVertexBuffer;
    const FIndexArrayView triangles = LOD.IndexBuffer.GetArrayView();
    for (uint32 i = 0; i + 2 < triangles.Num(); i += 3)
    {
        const FVector a(vertices.VertexPosition(triangles[i]));
        const FVector b(vertices.VertexPosition(triangles[i + 1]));
        const FVector c(vertices.VertexPosition(triangles[i + 2]));
        FVector normal = FVector::CrossProduct(b - a, c - a).GetSafeNormal();
        if (FVector::DotProduct(normal, a) < 0.0f)
        {
            normal = -normal;
        }

        planes.Emplace(normal.X, normal.Y, normal.Z, -FVector::DotProduct(normal, a));
    }

    return planes;
}
