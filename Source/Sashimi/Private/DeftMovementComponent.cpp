// Fill out your copyright notice in the Description page of Project Settings.


#include "DeftMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"

// DEBUG VISUALIZATION
static TAutoConsoleVariable<bool> CVarDebugLocomotion(TEXT("d.DebugMovement"), false, TEXT("shows debug info for movement"));
static TAutoConsoleVariable<bool> CVarDebugJump(TEXT("d.DebugJump"), false, TEXT("shows debug info for jumping"));

// FEATURE TOGGLES
static TAutoConsoleVariable<bool> CVarUseUEJump(TEXT("d.UseUEJump"), false, TEXT("if enabled use the default UE5 jump physics"));
static TAutoConsoleVariable<bool> CVarEnablePostJumpGravity(TEXT("d.EnablePostJumpGravity"), true, TEXT("if enabled use a different post jump gravity"));
static TAutoConsoleVariable<bool> CVarTestVariableJump(TEXT("d.TestVariableJump"), false, TEXT("spamspam"));

DEFINE_LOG_CATEGORY(LogDeftMovement);

UDeftMovementComponent::UDeftMovementComponent()
{
}


void UDeftMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	UPhysicsSettings* physicsSettings = UPhysicsSettings::Get();
	if (physicsSettings)
	{
		m_DefaultGravityZCache = UPhysicsSettings::Get()->DefaultGravityZ;
	}
	else
		UE_LOG(LogDeftMovement, Error, TEXT("Failed to find valid PhysicsSettings"));

	m_DefaultGravityScaleCache = GravityScale;

	m_MaxPreJumpGravityScale = CalculateJumpGravityScale(TimeToJumpMaxHeight, JumpMaxHeight);
	// We need to scale time by the same factor as height since
	// if it takes 1s to reach 4m, then it would take 0.5s to reach 2m
	// so if the max height is 4m in 1s, and the min height is 2m we shouldn't use gravity that takes us to 2m over 1s, it should take us 0.5s instead
	float timeScale = JumpMaxHeight / JumpMinHeight;
	m_MinPreJumpGravityScale = CalculateJumpGravityScale(TimeToJumpMaxHeight / timeScale, JumpMinHeight);
	m_PostJumpGravityScale = CalculateJumpGravityScale(PostTimeToJumpMaxHeight, JumpMaxHeight);
}

void UDeftMovementComponent::TickComponent(float aDeltaTime, enum ELevelTick aTickType, FActorComponentTickFunction* aThisTickFunction)
{
	Super::TickComponent(aDeltaTime, aTickType, aThisTickFunction);

	if (m_bInPlatformJump && !m_bJumpApexReached) // TODO: right now we're relying on MOVE_Falling to drive the actual physics state, but we still need to track data while in the jump to know when to change gravity
	{
		PhysPlatformJump(aDeltaTime);
	}

#if DEBUG_VIEW
	DrawDebug();
#endif
}

static float testSwitchAtHalf;
bool UDeftMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (CVarUseUEJump.GetValueOnGameThread())
	{
		return Super::DoJump(bReplayingMoves, DeltaTime);
	}

	// TODO: will have to override CanJump()
	// TODO: m_bInAirFromJump is temporary should not exist long term
	if (CharacterOwner && CharacterOwner->CanJump() && !m_bInPlatformJump)
	{
		// Don't jump if we can't move up/down.
		if (!bConstrainToPlane || !FMath::IsNearlyEqual(FMath::Abs(GetGravitySpaceZ(PlaneConstraintNormal)), 1.f))
		{
			// Apply instantaneous velocity and new gravity scale
			FVector initialVelocity = CalculateJumpInitialVelocity(TimeToJumpMaxHeight, JumpMaxHeight);
			Velocity.Z = initialVelocity.Z;
			
			GravityScale = m_MaxPreJumpGravityScale;

			m_PlatformJumpInitialPosition = CharacterOwner->GetActorLocation();
			m_bInPlatformJump = true;

			m_PlatformJumpApex = 0.f;

			// allows the physx engine to take over and apply gravity over time and automatic collision checks
			SetMovementMode(MOVE_Falling);

#if DEBUG_VIEW
			m_PlatformJumpDebug.m_GravityValues.Empty();
			m_PlatformJumpDebug.m_GravityValues.Add(m_DefaultGravityZCache * GravityScale);
			m_PlatformJumpDebug.m_GravityValues.Add(m_DefaultGravityZCache * m_PostJumpGravityScale);
			m_PlatformJumpDebug.m_InitialVelocity = initialVelocity.Z;
#endif
			return true;
		}
	}
	return false;
}


void UDeftMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	// TODO: right now if we went from falling to _anything else_ we consider that the end of any possible jump
	// worst case scenario we weren't in a jump in which case no harm done
	if (PreviousMovementMode == MOVE_Falling)
	{
		if (m_bInPlatformJump)
		{
			m_bInPlatformJump = false;
			m_bJumpApexReached = false;
			GravityScale = m_DefaultGravityScaleCache;
		}
	}
}


void UDeftMovementComponent::OnJumpPressed()
{
	m_JumpKeyHoldTime = 0.f;
	m_bIncrementJumpInputHoldTime = true;
}


void UDeftMovementComponent::OnJumpReleased()
{
	// apply a new gravity scale based on time held
	if (m_bIncrementJumpInputHoldTime)
	{
		// Only switch to gravity if we need to
		m_bIncrementJumpInputHoldTime = false;

		// ex: max time is 2s, we hold for 1s, we get 1/2s = 0.5 == 50% of the max jump
		// ex: max time is 2s, min time is 1s, we hold for 0.2s, we _should_ get 0.2/2s = 0.1 == 10% of the jump
		float val = FMath::Clamp(m_JumpKeyHoldTime / JumpKeyMaxHoldTime, 0.f, 1.f);

		// val == 1 that means we held it max time and shouldn't change gravity at all.
		// val == 0 means we want the min height (more gravity applied)
		// x in [a,b] to [c,d] = (x-a) / (b-a) * (d - c) + c
		// x		= val
		// [a,b]	= [0,1]
		// [c,d]	= [minJumpGrav, maxJumpGrav]

		float gravityScaledByInput = (val * (m_MaxPreJumpGravityScale - m_MinPreJumpGravityScale)) + m_MinPreJumpGravityScale;
		UE_LOG(LogDeftMovement, Warning, TEXT("gravityScaledByInput: %.2f"), gravityScaledByInput);
 		GravityScale = gravityScaledByInput;
	}
}

void UDeftMovementComponent::PhysPlatformJump(float aDeltaTime)
{
	// TODO: can probably precalculate this if we need
	float distanceJumped = FMath::Abs(CharacterOwner->GetActorLocation().Z - m_PlatformJumpInitialPosition.Z);
	m_PlatformJumpApex = FMath::Max(m_PlatformJumpApex, distanceJumped);

	if (m_bIncrementJumpInputHoldTime)
	{
		m_JumpKeyHoldTime += aDeltaTime;
	}

	// indicates we've started falling because our velocity has switched directions or gone from pos to 0
	if (Velocity.Z < 0.f)
	{
		OnJumpApexReached();
	}
}


void UDeftMovementComponent::OnJumpApexReached()
{
	if (CVarEnablePostJumpGravity.GetValueOnGameThread())
	{
		m_bJumpApexReached = true;
		UE_LOG(LogTemp, Warning, TEXT("setting GravityScale to %.2f"), m_PostJumpGravityScale);
		GravityScale = m_PostJumpGravityScale;

		m_bIncrementJumpInputHoldTime = false;
		m_JumpKeyHoldTime = 0.f;

#if DEBUG_VIEW
		m_PlatformJumpDebug.m_GravityValues.Add(m_DefaultGravityZCache * GravityScale);
#endif
	}
}


FVector UDeftMovementComponent::CalculateJumpInitialVelocity(float aTime, float aHeight)
{
	// TODO: return more than just Z
	return FVector(0.f, 0.f, (2 * aHeight) / aTime);
}

float UDeftMovementComponent::CalculateJumpGravityScale(float aTime, float aHeight)
{
	return ((-2 * aHeight) / (aTime * aTime)) / m_DefaultGravityZCache;
}

#if DEBUG_VIEW
void UDeftMovementComponent::DrawDebug()
{
	GEngine->ClearOnScreenDebugMessages();

	if (CVarDebugLocomotion.GetValueOnGameThread())
		DebugMovement();
	if (CVarDebugJump.GetValueOnGameThread())
		DebugPlatformJump();

	GEngine->AddOnScreenDebugMessage(-1, 0.01f, CVarDebugLocomotion.GetValueOnGameThread() ? FColor::Yellow : FColor::White, FString::Printf(TEXT("d.DebugMovement: %d"), (int32)CVarDebugLocomotion.GetValueOnGameThread()));
	GEngine->AddOnScreenDebugMessage(-1, 0.01, CVarDebugJump.GetValueOnGameThread() ? FColor::Yellow : FColor::White, FString::Printf(TEXT("d.DebugJump: %d"), (int32)CVarDebugJump.GetValueOnGameThread()));
	GEngine->AddOnScreenDebugMessage(-1, 0.01, CVarUseUEJump.GetValueOnGameThread() ? FColor::Yellow : FColor::White, FString::Printf(TEXT("d.UseUEJump: %d"), (int32)CVarUseUEJump.GetValueOnGameThread()));
	GEngine->AddOnScreenDebugMessage(-1, 0.01, CVarEnablePostJumpGravity.GetValueOnGameThread() ? FColor::Yellow : FColor::White, FString::Printf(TEXT("d.EnablePostJumpGravity: %d"), (int32)CVarEnablePostJumpGravity.GetValueOnGameThread()));
}

void UDeftMovementComponent::DebugMovement()
{
	ACharacter* owner = Cast<ACharacter>(GetOwner());
	FVector actorLocation = owner->GetActorLocation();
	FVector actorForward = owner->GetActorForwardVector();
	FVector actorRight = owner->GetActorRightVector();
	FVector actorUp = actorForward.Cross(actorRight);
	float drawScalar = 100.f;

	// draw axis
	DrawDebugLine(GetWorld(), actorLocation, actorLocation + actorForward * drawScalar, FColor::Red);
	DrawDebugLine(GetWorld(), actorLocation, actorLocation + actorRight * drawScalar, FColor::Green);
	DrawDebugLine(GetWorld(), actorLocation, actorLocation + actorUp * drawScalar, FColor::Cyan);

	// draw head
	UCapsuleComponent* capsulComponent = owner->GetCapsuleComponent();
	float capsulHalfHeight = capsulComponent->GetScaledCapsuleHalfHeight();
	float headRadius = 25.f;
	DrawDebugSphere(GetWorld(), actorLocation + FVector(0.f, 0.f, capsulHalfHeight - headRadius), headRadius, 12, FColor::White);

	// draw feet
	float capsuleRadius = capsulComponent->GetScaledCapsuleRadius();
	float footRadius = 10.f;
	FVector rightFoot = actorLocation + actorRight * (capsuleRadius - footRadius);
	FVector leftFoot = actorLocation - actorRight * (capsuleRadius - footRadius);

	DrawDebugSphere(GetWorld(), rightFoot + actorUp * -1 * (capsulHalfHeight - footRadius), footRadius, 12, FColor::Yellow);
	DrawDebugSphere(GetWorld(), leftFoot + actorUp * -1 * (capsulHalfHeight - footRadius), footRadius, 12, FColor::Yellow);
}

void UDeftMovementComponent::DebugPlatformJump()
{
	if (!GEngine)
		return;

	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tJumpHoldTime: %.2f"), m_JumpKeyHoldTime));
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tMin Gravity Scale: %.2f"), m_PostJumpGravityScale));
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tMin Gravity Scale: %.2f"), m_MinPreJumpGravityScale));
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tMax Gravity Scale: %.2f"), m_MaxPreJumpGravityScale));
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tGravity Scale: %.2f"), GravityScale));

	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::White, FString::Printf(TEXT("\tJump Apex: %.2f"), m_PlatformJumpApex));
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::White, FString::Printf(TEXT("\tJump Initial Pos: %s"), *m_PlatformJumpInitialPosition.ToString()));
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::White, FString::Printf(TEXT("\tInitial Velocity: %.2f"), m_PlatformJumpDebug.m_InitialVelocity));

	const bool bUsePostJumpGravity = CVarEnablePostJumpGravity.GetValueOnGameThread();
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, bUsePostJumpGravity ? FColor::Green : FColor::Red, FString::Printf(TEXT("Post Jump Gravity: %s"), bUsePostJumpGravity ? TEXT("Enabled") : TEXT("Disabled")));
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, m_bInPlatformJump ? FColor::Green : FColor::White, FString::Printf(TEXT("Jump State %s"), m_bInPlatformJump ? TEXT("In Jump") : TEXT("Not Jumping")));
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::Cyan, TEXT("\n-Gravity Scaled Jump-"));
}
#endif