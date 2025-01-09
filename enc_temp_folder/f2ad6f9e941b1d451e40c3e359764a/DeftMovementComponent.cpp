// Fill out your copyright notice in the Description page of Project Settings.


#include "DeftMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Character/PlayerCharacter.h"
#include "CollisionQueryParams.h"

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

	m_CollisionQueryParams.AddIgnoredActor(GetOwner());
	const UCapsuleComponent* capsuleComponent = Cast<ACharacter>(GetOwner())->GetCapsuleComponent();
	m_CapsuleCollisionShapeCache = FCollisionShape::MakeCapsule(capsuleComponent->GetUnscaledCapsuleRadius(), capsuleComponent->GetUnscaledCapsuleHalfHeight());

	m_SphereCollisionShape = FCollisionShape::MakeSphere(10.f);
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
	if (CharacterOwner && CharacterOwner->CanJump())
	{
		// Don't jump if we can't move up/down.
		if (!bConstrainToPlane || !FMath::IsNearlyEqual(FMath::Abs(GetGravitySpaceZ(PlaneConstraintNormal)), 1.f))
		{
			//// Don't allow double jump with positive velocity
			//if (m_bInPlatformJump && !m_bJumpApexReached)
			//	return false;

			//TODO: we don't care if we're in mid air or not, just make a function to get a different parabola if need be			
			FVector initialVelocity = CalculateJumpInitialVelocity(TimeToJumpMaxHeight, JumpMaxHeight);
			float gravityScale = m_MaxPreJumpGravityScale;
			//if (IsAttemptingDoubleJump())
			//{
			//	// only add extra velocity if we're headed downwards
			//	if (Velocity.Z < 0)
			//	{
			//		initialVelocity.Z += -Velocity.Z; // I want a double jump to always cover (for ex) 2m. The velocity we calculate is assuming we're starting at 0 velocity, but if we're already falling we might have a large negative velocity
			//	}
			//	gravityScale = m_MinPreJumpGravityScale;
			//}
			Velocity.Z = initialVelocity.Z;
			GravityScale = gravityScale;

			// allows the physx engine to take over and apply gravity over time and automatic collision checks
			SetMovementMode(MOVE_Falling);
			m_bInPlatformJump = true;
			m_bJumpApexReached = false;

			// TODO: these are really debug...
			// For now I want to combine double jumps so we see the highest apex ever, not each individual parabola
			if (!IsAttemptingDoubleJump())
				m_PlatformJumpInitialPosition = CharacterOwner->GetActorLocation();
			m_PlatformJumpApex = 0.f;

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
	// only do if we're changing to a different move state. If we go from Falling -> Falling (which could technically happen with multiple in air jumps) we don't want to reset anything
	if (MovementMode ^ PreviousMovementMode)
	{
		switch (PreviousMovementMode)
		{
			case MOVE_Falling:
				ResetFromFalling();
		}
	}
}


bool UDeftMovementComponent::CanAttemptJump() const
{
	// TODO: other types of aerial moves should reset the jump ability like a mid air kick or dash you should be able to perform a jump after perhaps
 
	// Allows coyote time for a short distance after true falling occurs 
	if (m_FallDistanceJumpThreshold > 0)
	{
		float distanceFallen = FMath::Abs(m_FallOrigin.Z - CharacterOwner->GetActorLocation().Z);
		return	Super::CanAttemptJump() &&
			(!IsAttemptingDoubleJump() || distanceFallen <= m_FallDistanceJumpThreshold);
	}

	return	Super::CanAttemptJump();
}

void UDeftMovementComponent::OnJumpPressed()
{
	// we don't want variable jump for double jumps
	//if (IsAttemptingDoubleJump())
	//	return;

	m_JumpKeyHoldTime = 0.f;
	m_bIncrementJumpInputHoldTime = true;
}

void UDeftMovementComponent::OnJumpReleased()
{
	// we don't want variable jump for double jumps
	//if (IsAttemptingDoubleJump())
	//	return;

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
 		GravityScale = gravityScaledByInput;
	}
}


void UDeftMovementComponent::PhysFalling(float aDeltaTime, int32 aIterations)
{
	Super::PhysFalling(aDeltaTime, aIterations);

	if (MovementMode == MOVE_Falling && Velocity.Z < 0 && !m_bIsFallOriginSet)
	{
		m_bIsFallOriginSet = true;
		m_FallOrigin = CharacterOwner->GetActorLocation();
	}

	FindLedge();
}


void UDeftMovementComponent::ResetFromFalling()
{
	if (m_bInPlatformJump)
	{
		m_bInPlatformJump = false;
		m_bJumpApexReached = false; //tODO: this needs to be reset to false when we double jump!! thats why we're never resetting gravity to the post jump gravity
		GravityScale = m_DefaultGravityScaleCache;
	}

	m_bIsFallOriginSet = false;
	m_FallOrigin = FVector::ZeroVector;
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
	UE_LOG(LogTemp, Warning, TEXT("VelocityZ %.2f"), Velocity.Z);
	if (Velocity.Z < 0.f)
	{
		OnJumpApexReached();
	}
}


void UDeftMovementComponent::FindLedge()
{
	FVector wallLocation;
	if (!CheckForWall(wallLocation))
		return;

	FVector heightDistance;
	if (!CheckForLedge(wallLocation, heightDistance))
		return;

	FVector ledgeSurfaceLocation, ledgeSurfaceNormal;
	if (!CheckLedgeSurface(heightDistance, ledgeSurfaceLocation, ledgeSurfaceNormal))
		return;

	// Regardless if there's space I want to know where the edge is
	FVector ledgeEdgeLocation;
	GetLedgeEdge(ledgeSurfaceLocation, ledgeSurfaceNormal, wallLocation, ledgeEdgeLocation);

	if (!CheckSpaceForCapsule(ledgeSurfaceLocation))
		return;
}

bool UDeftMovementComponent::CheckForWall(FVector& outWallLocation)
{
	// inside actor capsule at half height
	const FVector wallRayStart = CharacterOwner->GetActorLocation();
	// extending in forward direction outwards
	const FVector wallRayEnd = wallRayStart + CharacterOwner->GetActorForwardVector() * WallReach;

	// default to max reach in case we don't hit anything
	outWallLocation = wallRayEnd;


	FHitResult wallHit;
	const bool bHitWall = GetWorld()->LineTraceSingleByProfile(wallHit, wallRayStart, wallRayEnd, CharacterOwner->GetCapsuleComponent()->GetCollisionProfileName(), m_CollisionQueryParams);
	if (bHitWall)
	{
		// if we hit something that means there is a wall in front of us
		outWallLocation = wallHit.Location;
		DrawDebugLine(GetWorld(), wallRayStart,  wallRayEnd, FColor::Green);
		DrawDebugSphere(GetWorld(), wallHit.Location, 5.f, 12, FColor::Blue);
		UE_VLOG_SEGMENT(this, LogDeftMovement, Log, wallRayStart, wallRayEnd, FColor::Green, TEXT("Wall Reach"));
		UE_VLOG_LOCATION(this, LogDeftMovement, Log, outWallLocation, 5.f, FColor::Green, TEXT("Wall hit location"));
		return true;
	}

	DrawDebugLine(GetWorld(), wallRayStart, wallRayEnd, FColor::Red);
	UE_VLOG_SEGMENT(this, LogDeftMovement, Log, wallRayStart, wallRayEnd, FColor::Red, TEXT("Wall Reach"));
	return false;
}

bool UDeftMovementComponent::CheckForLedge(const FVector& aWallLocation, FVector& outHeightDistance)
{
	const FVector heightRayStart = CharacterOwner->GetActorLocation() + (CharacterOwner->GetActorUpVector() * LedgeHeightOrigin);
	const FVector heightRayEnd = heightRayStart + CharacterOwner->GetActorForwardVector() * LedgeHeightForwardReach;

	// we want this to be the max distance to make sure there is a ledge beneath that's at least wide enough for the character to stand
	outHeightDistance = heightRayEnd;

	FHitResult wallHit;
	const bool bHitAnything = GetWorld()->LineTraceSingleByProfile(wallHit, heightRayStart, heightRayEnd, CharacterOwner->GetCapsuleComponent()->GetCollisionProfileName(), m_CollisionQueryParams);
	if (!bHitAnything)
	{
		// no hit means open space above the player which indicates a ledge
		UE_VLOG_SEGMENT(this, LogDeftMovement, Log, heightRayStart, heightRayStart + heightRayEnd, FColor::Green, TEXT("Space Reach"));
		return true;
	}

	// if we hit something there is no open space above the player so there is no ledge
	UE_VLOG_SEGMENT(this, LogDeftMovement, Log, heightRayStart, heightRayStart + heightRayEnd, FColor::Red, TEXT("Space Reach"));
	UE_VLOG_LOCATION(this, LogDeftMovement, Log, wallHit.Location, 5.f, FColor::Red, TEXT("Space hit location"));
	return false;
}

bool UDeftMovementComponent::CheckLedgeSurface(const FVector& aFloorCheckHeightOrigin, FVector& outFloorLocation, FVector& outFloorNormal)
{
	const FVector floorRayStart = aFloorCheckHeightOrigin;
	const FVector floorRayEnd = floorRayStart - CharacterOwner->GetActorUpVector() * LedgeHeightOrigin * 2; // check for a floor twice as far just to see if we hit something

	// default to max reach distance in case we don't hit anything
	outFloorLocation = floorRayEnd;
	outFloorNormal = FVector::ZeroVector;

	FHitResult floorHit;
	const bool bHitFloor = GetWorld()->LineTraceSingleByProfile(floorHit, floorRayStart, floorRayEnd, CharacterOwner->GetCapsuleComponent()->GetCollisionProfileName(), m_CollisionQueryParams);
	if (bHitFloor)
	{
		// hitting the floor means there is a ledge at least wide enough for us to stand on
		outFloorLocation = floorHit.Location;
		outFloorNormal = floorHit.Normal;
		UE_VLOG_SEGMENT(this, LogDeftMovement, Log, floorRayStart, floorRayStart + floorRayEnd, FColor::Green, TEXT("Floor Reach"));
		UE_VLOG_LOCATION(this, LogDeftMovement, Log, outFloorLocation, 5.f, FColor::Green, TEXT("Floor hit location"));
		return true;
	}

	UE_VLOG_SEGMENT(this, LogDeftMovement, Log, floorRayStart, floorRayStart + floorRayEnd, FColor::Red, TEXT("Floor Reach"));
	return false;

	// TODO: check surface normal maybe
}


bool UDeftMovementComponent::CheckSpaceForCapsule(const FVector& aFloorLocation)
{
	const FVector capsuleStart = aFloorLocation + CharacterOwner->GetActorUpVector() * 2.f; // start the capsule a few units off the ground just so the bottom of the capsule doesn't hit it
	const FVector capsuleEnd = capsuleStart + CharacterOwner->GetActorUpVector() * 2.f;		// cannot sweep at the exact location so it has to move a tiny amount

	FHitResult hitAnything;
	const bool bHitAnything = GetWorld()->SweepSingleByProfile(hitAnything, capsuleStart, capsuleEnd, CharacterOwner->GetActorRotation().Quaternion(), CharacterOwner->GetCapsuleComponent()->GetCollisionProfileName(), m_CapsuleCollisionShapeCache, m_CollisionQueryParams);
	if (!bHitAnything)
	{
		// not hitting anything means there's enough space for the character's capsule with a little wiggle room
		return true;
	}

	// hitting something indicates there's not enough space to stand so no ledge up
	// TODO: could still indicate a ledge hang maybe
	return false;
}


void UDeftMovementComponent::GetLedgeEdge(const FVector& aFloorLocation, const FVector& aFloorNormal, FVector& aWallLocation, FVector& outLedgeEdge)
{
	// starting from the floor location
	// find a vector perpendicular to the floor's normal which should give us the surface
	const FVector dirFloorToPlayer = CharacterOwner->GetActorLocation() - aFloorLocation;
	const FVector floorUp = aFloorNormal;
	const FVector floorRight = floorUp.Cross(dirFloorToPlayer);
	const FVector floorForward = floorRight.Cross(floorUp);

	// TODO: there is an issue where the ledge edge is overshooting the wall location somehow
	UE_VLOG_SEGMENT(this, LogDeftMovement, Log, aFloorLocation, aWallLocation, FColor::Yellow, TEXT("2 Floor To Wall Location"));

	const FVector dirToWall = aWallLocation - aFloorLocation;
	const FVector projWallDirOntoFloorSurface = (dirToWall.Dot(floorForward) / floorForward.Dot(floorForward)) * floorForward;
	const float distToEdge = projWallDirOntoFloorSurface.Length();
	outLedgeEdge = aFloorLocation + floorForward.GetSafeNormal() * distToEdge;

	// draw floor axis
	DrawDebugLine(GetWorld(), aFloorLocation, aFloorLocation + floorUp * 100.f, FColor::Cyan);
	DrawDebugLine(GetWorld(), aFloorLocation, aFloorLocation + floorRight * 100.f, FColor::Green);
	DrawDebugLine(GetWorld(), aFloorLocation, aFloorLocation + floorForward * 100.f, FColor::Red);

	UE_VLOG_LOCATION(this, LogDeftMovement, Log, aWallLocation, 2.5f, FColor::Yellow, TEXT("Wall Loc 2"));
	//UE_VLOG_SEGMENT(this, LogDeftMovement, Log, aFloorLocation, aFloorLocation + projWallDirOntoFloorSurface, FColor::Cyan, TEXT("Proj Wall Dir Onto Floor"));
	//UE_VLOG_SEGMENT(this, LogDeftMovement, Log, aFloorLocation, aFloorLocation + dirToWall.GetSafeNormal() * dirToWall.Length(), FColor::Cyan, TEXT("Floor To Wall Location"));

	//UE_VLOG_SEGMENT(this, LogDeftMovement, Log, aFloorLocation, aFloorLocation + floorUp * 100.f, FColor::Cyan, TEXT("Floor Up"));
	//UE_VLOG_SEGMENT(this, LogDeftMovement, Log, aFloorLocation, aFloorLocation + floorRight * 100.f, FColor::Green, TEXT("Floor Right"));
	UE_VLOG_SEGMENT(this, LogDeftMovement, Log, aFloorLocation, aFloorLocation + floorForward * 100.f, FColor::Red, TEXT("Floor Forward"));
	UE_VLOG_LOCATION(this, LogDeftMovement, Log, outLedgeEdge, 5.f, FColor::Blue, TEXT("Ledge Edge"));
}

void UDeftMovementComponent::OnJumpApexReached()
{
	UE_LOG(LogTemp, Warning, TEXT("apex reached"));
	if (CVarEnablePostJumpGravity.GetValueOnGameThread())
	{
		m_bJumpApexReached = true;
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


void UDeftMovementComponent::DebugPhysFalling()
{
	if (MovementMode != MOVE_Falling)
		return;

	const FVector actorLocation = CharacterOwner->GetActorLocation() + CharacterOwner->GetActorUpVector() * LedgeVerticalReachMax;
	FColor geometryDebugColor = m_LedgeDebug.m_GeometryHit ? FColor::Red : FColor::White;

	DrawDebugLine(GetWorld(), actorLocation, m_LedgeDebug.m_GeometryHitLocation, FColor::Yellow);

	DrawDebugSphere(GetWorld(), m_LedgeDebug.m_GeometryHitLocation, m_SphereCollisionShape.GetSphereRadius(), 12, geometryDebugColor);
}

void UDeftMovementComponent::DebugPlatformJump()
{
	if (!GEngine)
		return;

	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tJumpHoldTime: %.2f"), m_JumpKeyHoldTime));
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tPost Jump Gravity Scale: %.2f"), m_PostJumpGravityScale));
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tMin Gravity Scale: %.2f"), m_MinPreJumpGravityScale));
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tMax Gravity Scale: %.2f"), m_MaxPreJumpGravityScale));
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White, FString::Printf(TEXT("\tGravity Scale: %.2f"), GravityScale));

	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::White, FString::Printf(TEXT("\tJump Apex: %.2f"), m_PlatformJumpApex));
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::White, FString::Printf(TEXT("\tJump Initial Pos: %s"), *m_PlatformJumpInitialPosition.ToString()));
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::White, FString::Printf(TEXT("\tInitial Velocity: %.2f"), m_PlatformJumpDebug.m_InitialVelocity));

	const bool bUsePostJumpGravity = CVarEnablePostJumpGravity.GetValueOnGameThread();
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, bUsePostJumpGravity ? FColor::Green : FColor::Red, FString::Printf(TEXT("Post Jump Gravity: %s"), bUsePostJumpGravity ? TEXT("Enabled") : TEXT("Disabled")));
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, m_bInPlatformJump ? FColor::Green : FColor::White, FString::Printf(TEXT("Jump State %s\nFrom %s"), m_bInPlatformJump ? TEXT("In Jump") : TEXT("Not Jumping"), IsAttemptingDoubleJump() ? TEXT("Mid Air") : TEXT("Solid Ground")));
	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::Cyan, TEXT("\n-Gravity Scaled Jump-"));

	DebugPhysFalling();
}
#endif