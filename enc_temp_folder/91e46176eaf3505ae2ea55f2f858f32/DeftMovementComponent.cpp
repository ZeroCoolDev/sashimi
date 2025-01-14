// Fill out your copyright notice in the Description page of Project Settings.


#include "DeftMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Character/PlayerCharacter.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "DeftLocks.h"
#include "GameFramework/ForceFeedbackEffect.h"

// DEBUG VISUALIZATION
static TAutoConsoleVariable<bool> CVarDebugLocomotion(TEXT("d.DebugMovement"), false, TEXT("shows debug info for movement"));
static TAutoConsoleVariable<bool> CVarDebugJump(TEXT("d.DebugJump"), false, TEXT("shows debug info for jumping"));

// FEATURE TOGGLES
static TAutoConsoleVariable<bool> CVarUseUEJump(TEXT("d.UseUEJump"), false, TEXT("if enabled use the default UE5 jump physics"));
static TAutoConsoleVariable<bool> CVarEnablePostJumpGravity(TEXT("d.EnablePostJumpGravity"), true, TEXT("if enabled use a different post jump gravity"));
static TAutoConsoleVariable<bool> CVarTestVariableJump(TEXT("d.TestVariableJump"), false, TEXT("spamspam"));
static TAutoConsoleVariable<bool> CVarDeftLocksUnlockAll(TEXT("d.DeftLocks.UnlockAll"), false, TEXT("reset all locks"));

DEFINE_LOG_CATEGORY(LogDeftMovement);
DEFINE_LOG_CATEGORY(LogDeftLedge);
DEFINE_LOG_CATEGORY(LogDeftLedgeLaunchTrajectory);
DEFINE_LOG_CATEGORY(LogDeftLedgeLaunchPath);

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
	m_CapsuleCollisionShapeCache = FCollisionShape::MakeCapsule(capsuleComponent->GetScaledCapsuleRadius(), capsuleComponent->GetScaledCapsuleHalfHeight());

	m_SphereCollisionShape = FCollisionShape::MakeSphere(10.f);
}

void UDeftMovementComponent::TickComponent(float aDeltaTime, enum ELevelTick aTickType, FActorComponentTickFunction* aThisTickFunction)
{
	Super::TickComponent(aDeltaTime, aTickType, aThisTickFunction);

#if DEBUG_VIEW
	DrawDebug();

	if (CVarDeftLocksUnlockAll.GetValueOnGameThread())
	{
		DeftLocks::UnlockAll();
		IConsoleManager& consoleManager = IConsoleManager::Get();
		if (IConsoleVariable* cvar = consoleManager.FindConsoleVariable(TEXT("d.DeftLocks.UnlockAll")))
		{
			UE_LOG(LogTemp, Warning, TEXT("reset d.DeftLocks.UnlockAll"));
			cvar->Set(false, EConsoleVariableFlags::ECVF_SetByConsole);
			
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("couldn't find d.DeftLocks.UnlockAll"));
		}
	}
#endif
}

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
			//TODO: we don't care if we're in mid air or not, just make a function to get a different parabola if need be			
			FVector initialVelocity = CalculateJumpInitialVelocity(TimeToJumpMaxHeight, JumpMaxHeight);
			float gravityScale = m_MaxPreJumpGravityScale;

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
				ResetJump();
				break;
			case MOVE_Walking:
				m_bHasAirDashed = false;
				break;
		}
	}
}


void UDeftMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	UpdateInternalMoveMode(DeltaSeconds);
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
	m_JumpKeyHoldTime = 0.f;
	m_bIncrementJumpInputHoldTime = true;
	m_bIsJumpButtonDown = true;
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
		float gravityScaledByInput = (val * (m_MaxPreJumpGravityScale - m_MinPreJumpGravityScale)) + m_MinPreJumpGravityScale;
 		GravityScale = gravityScaledByInput;
	}
	
	m_bIsJumpButtonDown = false;
}


void UDeftMovementComponent::OnAirDash()
{
	if (IsFalling() && !m_bHasAirDashed)
	{
		ResetJump();

		m_bHasAirDashed = true;
		m_InternalMoveMode = IMOVE_AirDash;

		const float dashSpeed = AirDashDistance / AirDashTime;
		const FVector forwardVelocity = CharacterOwner->GetActorForwardVector() * dashSpeed;

		const FVector verticalVelocity = CalculateJumpInitialVelocity(AirDashTime, AirDashVerticalHeight);
		const float dashGravityScale = CalculateJumpGravityScale(AirDashTime, AirDashVerticalHeight);

		GravityScale = m_DefaultGravityZCache * dashGravityScale;
		const FVector finalDashVelocity = FVector(forwardVelocity.X, forwardVelocity.Y, verticalVelocity.Z);
		Launch(finalDashVelocity);
	}
}

void UDeftMovementComponent::PhysFalling(float aDeltaTime, int32 aIterations)
{
	Super::PhysFalling(aDeltaTime, aIterations);

	// store the last fall origin
	if (MovementMode == MOVE_Falling && Velocity.Z < 0 && !m_bIsFallOriginSet)
	{
		m_bIsFallOriginSet = true;
		m_FallOrigin = CharacterOwner->GetActorLocation();
	}

	// Only perform a ledge up if we're not already ledging up
	if (!m_bIsLedgingUp && Velocity.Z < 0)
	{
		// if velocity is negative (or we're post apex) and there is a ledge grab, grab it
		//		currently holding a ledge and jump or (maybe) forward is pressed hop up
		// if velocity is positive
		//		holding jump key: hop up
		const FVector actorLocation = CharacterOwner->GetActorLocation();
		const FVector fwd = CharacterOwner->GetActorForwardVector();

		if (FindLedge() && m_bIsJumpButtonDown && Velocity.Z < 0)
		{
			// ledge up
			//	- height = however high we need to make sure the capsule is above the ledge surface
			//	- time = the constant time we want it to take
			// 1. lock all move input
			// 2. calculate the jump velocity and gravity needed to achieve height in time
			// 3. jump
			// 4. on jump apex achieved unlock forward and backwards input and ADD some small constant forward velocity

			// 1. Lock all move input
			DeftLocks::IncrementMoveInputForwardBackLockRef();
			DeftLocks::IncrementMoveInputRightLeftockRef();

			// 2. calculate min height needed for capsule to be above surface
			const UCapsuleComponent* capsuleComponent = CharacterOwner->GetCapsuleComponent();
			const float capsuleHalfHeight = capsuleComponent->GetScaledCapsuleHalfHeight();
			const FVector targetLocation = m_ledgeEdgeCache + FVector(0.f, 0.f, capsuleHalfHeight + LedgeUpAdditionalHeightOffset);
			const float distanceToLedgeUpHeight = targetLocation.Z - CharacterOwner->GetActorLocation().Z;

			UE_VLOG_SPHERE(this, LogDeftLedgeLaunchTrajectory, Log, targetLocation, 5.f, FColor::Green, TEXT("ledgeUpHeight"));
			UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("char to target distance: %.2f"), distanceToLedgeUpHeight);

			// 3. calculate jump velocity and gravity needed to reach ledge height in time
			const FVector ledgeUpVelocity = CalculateJumpInitialVelocity(TimeToReachLedgeUpHeight, distanceToLedgeUpHeight);
			const float ledgeUpGravityScale = CalculateJumpGravityScale(TimeToReachLedgeUpHeight, distanceToLedgeUpHeight);

			// 4. Clear out any previous state or velocity
			ResetJump();
			Velocity = FVector::ZeroVector;

			// 5. Jump
			// this should still respect the number of jumps we've done, so if we double jumped to get to the ledge we won't be able to perform another after
			// but if we only jumped once into a ledge up we should be able to jump again after
			Velocity.Z = ledgeUpVelocity.Z;	// Velocity XY will be slowly updated over time rather than suddenly
			GravityScale = ledgeUpGravityScale;
			m_bIsLedgingUp = true;
			SetInternalMoveMode(EInternalMoveMode::IMOVE_LedgeUp);

			if (LedgeUpFeedback)
			{
				if (APlayerController* playerController = Cast<APlayerController>(CharacterOwner->Controller))
				{
					FForceFeedbackParameters feedbackParams;
					playerController->ClientPlayForceFeedback(LedgeUpFeedback, feedbackParams);
				}
			}

			//PerformLedgeUp();
		}

		UE_VLOG_SEGMENT(this, LogDeftLedge, Log, actorLocation, actorLocation + fwd * 1000.f, FColor::Magenta, TEXT("Actor Forward"));

		// TODO: implement wall run up which will also trigger an automatic hop up at the top of ledges
	}
}

void UDeftMovementComponent::UpdateInternalMoveMode(float aDeltaTime)
{
	if (m_bInPlatformJump)
	{
		if (!m_bJumpApexReached)// TODO: might be unnecessary
		{
			// Keep track of the highest point the jump reaches
			float distanceJumped = FMath::Abs(CharacterOwner->GetActorLocation().Z - m_PlatformJumpInitialPosition.Z);
			m_PlatformJumpApex = FMath::Max(m_PlatformJumpApex, distanceJumped);
		}

		// Keep track of how long the jump input has been held
		if (m_bIncrementJumpInputHoldTime)
		{
			m_JumpKeyHoldTime += aDeltaTime;
		}
	}

	// indicates we've started falling because our velocity has switched directions or gone from pos to 0
	if (Velocity.Z < 0.f)
	{
		OnJumpApexReached();
	}

	if (m_InternalMoveMode == EInternalMoveMode::IMOVE_LedgeUp)
	{
		// apply forward velocity over time
		// apply minimum forward velocity so even without input player still lands on ledge
		FVector forwardVelocity = CharacterOwner->GetActorForwardVector() * LedgeUpForwardMinBoost /*TODO: this is really the speed*/ * aDeltaTime;
		Velocity.X += forwardVelocity.X;
		Velocity.Y += forwardVelocity.Y;
	}
}


bool UDeftMovementComponent::FindLedge()
{
	FVector wallLocation;
	if (!CheckForWall(wallLocation))
		return false;

	FVector heightDistance;
	if (!CheckForLedge(wallLocation, heightDistance))
		return false;

	FVector ledgeSurfaceLocation, ledgeSurfaceNormal;
	if (!CheckLedgeSurface(heightDistance, ledgeSurfaceLocation, ledgeSurfaceNormal))
		return false;

	// Regardless if there's space I want to know where the edge is
	FVector ledgeEdgeLocation;
	GetLedgeEdge(ledgeSurfaceLocation, ledgeSurfaceNormal, wallLocation, ledgeEdgeLocation);
	m_ledgeEdgeCache = ledgeEdgeLocation;

	if (!CheckSpaceForCapsule(ledgeSurfaceLocation))
		return false;

	FVector hopUpLocation;
	GetHopUpLocation(ledgeEdgeLocation, hopUpLocation);
	m_ledgeHopUpLocationCache = hopUpLocation;

	return true;
}


void UDeftMovementComponent::PerformLedgeUp()
{
	// TODO: maybe use a timer to set this to false
	m_bIsLedgingUp = true;
	GravityScale *= 2.f;

	// get velocity to reach high enough that the entire capsule is above the ledge
	const UCapsuleComponent* capsuleComponent = CharacterOwner->GetCapsuleComponent();
	const float capsuleHalfHeight = capsuleComponent->GetScaledCapsuleHalfHeight();
	const FVector capsuleBase = CharacterOwner->GetActorLocation() - CharacterOwner->GetActorUpVector() * capsuleHalfHeight;

	UE_VLOG_SPHERE(this, LogDeftLedgeLaunchTrajectory, Log, capsuleBase, 2.5f, FColor::Green, TEXT("Capsule Base"));
	UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("capsuleBase: %s"), *capsuleBase.ToString());
	UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("hopUpLocation: %s"), *m_ledgeHopUpLocationCache.ToString());

	// using the capsule base as the players position because the hop up location also uses the base.
	// otherwise the "center" of the capsule might clear the height but the bottom would collide and that'd mess everything up.

	// horizontal motion
	// Xf = X0 + V0 * cos(theta) * t

	// vertical motion
	// Yf = Y0 + V0 * sin(theta) * t - (1/2gt^2)
	
	// min height check
	// H = ( V0^2 * sin^2(theta) ) / 2g

	// time to reach target horizontally
	// t = Xf - X0 / V0 * cos(theta)

	FVector startLocation = capsuleBase;

	// Calculate the horizontal and vertical distance from hop up location
	FVector directionToTarget = (m_ledgeHopUpLocationCache - startLocation).GetSafeNormal();
	UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("directionToTarget: %s"), *directionToTarget.ToString());
	// d = Xf - X0
	float horizontalDistance = FVector(m_ledgeHopUpLocationCache.X - startLocation.X, m_ledgeHopUpLocationCache.Y - startLocation.Y, 0.f).Length();
	// Yf - Y0, distance from players position to the target height
	float verticalDistance = m_ledgeHopUpLocationCache.Z - startLocation.Z;

	UE_VLOG_SEGMENT(this, LogDeftLedgeLaunchTrajectory, Log, startLocation, startLocation + directionToTarget * horizontalDistance, FColor::Red, TEXT("horizontalDist"));
	UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("horizontal distance: %.2f"), horizontalDistance);

	UE_VLOG_SEGMENT(this, LogDeftLedgeLaunchTrajectory, Log, startLocation, startLocation + directionToTarget * verticalDistance, FColor::Cyan, TEXT("verticalDis"));
	UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("vertical distance: %.2f"), verticalDistance);

	// gravity for whatever our current jump velocity is
	// TODO might want to make this constant
	// Gravity needs to be positive for the calculation to work
	const float gravity = FMath::Abs(m_DefaultGravityZCache * GravityScale);
	UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("gravity: %.2f"), gravity);

	// Multiple angles will result in a valid solution, but choosing the shortest flight time makes sure we don't overshoot the target
	struct FBestResult 
	{
		float shortestTime = FLT_MAX;
		FVector bestLaunchVelocity = FVector::ZeroVector;
		float bestAngle = 0.f;
	} bestResult;

	// numerical analysis: loop through angles to find a valid trajectory that clear the min height
	for (float testAngle = 10.f; testAngle <= 80.f; testAngle += 5.f)
	{
		// angle to rads
		const float theta = FMath::DegreesToRadians(testAngle);

		UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("\n\ttheta (deg): %.2f"), testAngle);

		// Calculate initial velocity
		// V0^2 = (g * d^2) / ( 2 * cos^2(theta) * (d * tan(theta) - (Yf - Y0)) )
		const float numerator = gravity * horizontalDistance * horizontalDistance;
		const float denom = 2 * FMath::Cos(theta) * FMath::Cos(theta) * ( horizontalDistance * FMath::Tan(theta) - verticalDistance);

		if (FMath::IsNearlyEqual(denom, 0.f))
		{
			UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Error, TEXT("divide by zero detected when calculating velocity for angle %.2f"), testAngle);
			continue;
		}
		const float velocitySquared = numerator / denom;

		UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("numerator: %.2f"), numerator);
		UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("denom: %.2f"), denom);
		UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("velocitySquared: %.2f"), velocitySquared);

		if (velocitySquared > 0.f)
		{
			// TODO: sqrt is slow, dont' do this
			const float initialVelocity = FMath::Sqrt(velocitySquared);
			UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("initialVelocity: %.2f"), initialVelocity);

			// calculate maximum height of the projectile for this theta and velocity
			const float maxHeight = (initialVelocity * initialVelocity * FMath::Sin(theta) * FMath::Sin(theta) ) / (2 * gravity);
			UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("maxHeight: %.2f"), maxHeight);

			// ensure the projectile clears the ledge
			if (maxHeight >= verticalDistance)
			{
				// calculate velocity launch vector towards target from current position given initial velocity and theta
				const FVector forwardVelocity = directionToTarget * (initialVelocity * FMath::Cos(theta));
				UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("horizontal Velocity: %s"), *forwardVelocity.ToString());

				const float upwardVelocity = initialVelocity * FMath::Sin(theta);
				UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("upwardVelocity: %.2f"), upwardVelocity);
				
				const FVector launchVelocity = forwardVelocity + FVector(0.f, 0.f, upwardVelocity);
				UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("launchVelocity: %s"), *launchVelocity.ToString());

				// calculate time of flight
				// t = ( V0 * sin(theta) + sqrt(V0 * sin^2(theta) + 2g(Yf - Y0)) ) / g
				const float v0SinTheta = initialVelocity * FMath::Sin(theta);
				const float flightTime = (v0SinTheta + FMath::Sqrt( (v0SinTheta * v0SinTheta) + (2 * gravity * verticalDistance) )) / gravity;
				UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("flightTime: %.2f"), flightTime);

				bool bNewBestFound = false;
				if (flightTime < bestResult.shortestTime)									// prefer shorter flight times
					bNewBestFound = true;
				else if (FMath::IsNearlyEqual(flightTime, bestResult.shortestTime, 0.01f))	// same flight time, prefer larger angle to stay closer to target position
				{
					if (testAngle > bestResult.bestAngle)
						bNewBestFound = true;
				}

				if (bNewBestFound)
				{
					bestResult.bestLaunchVelocity = launchVelocity;
					bestResult.bestAngle = testAngle;
					bestResult.shortestTime = flightTime;
					UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("Found velocity [%.2f] at [%.2f] degrees with a shorter flighttime [%.2f]"), initialVelocity, bestResult.bestAngle, flightTime);
				}

				// Still vizlog all of them
				DebugLedgeLaunch(startLocation, launchVelocity, flightTime, UGameplayStatics::GetWorldDeltaSeconds(GetWorld()), FColor::Yellow);
			}
			else
			{
				UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("maxHeight (%.2f) < verticalDistance (%.2f), next angle"), maxHeight, verticalDistance);
			}
		}
		else
		{
			UE_VLOG(this, LogDeftLedgeLaunchTrajectory, Log, TEXT("velocitySquared was <= 0.f, next angle"));
		}
	}
	
	// launch vector found to hop up the ledge!
	if (!bestResult.bestLaunchVelocity.IsZero())
	{
		// make sure any previous velocity doesn't carry over
		Velocity = FVector::ZeroVector;
		// launches the player 
		Launch(bestResult.bestLaunchVelocity);
		DebugLedgeLaunch(startLocation, bestResult.bestLaunchVelocity, bestResult.shortestTime, UGameplayStatics::GetWorldDeltaSeconds(GetWorld()), FColor::Green);
	}
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
		UE_VLOG_SEGMENT(this, LogDeftLedge, Log, wallRayStart, wallRayEnd, FColor::Green, TEXT("Wall Reach"));
		UE_VLOG_LOCATION(this, LogDeftLedge, Log, outWallLocation, 5.f, FColor::Green, TEXT("Wall hit location"));
		return true;
	}

	DrawDebugLine(GetWorld(), wallRayStart, wallRayEnd, FColor::Red);
	UE_VLOG_SEGMENT(this, LogDeftLedge, Log, wallRayStart, wallRayEnd, FColor::Red, TEXT("Wall Reach"));
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
		UE_VLOG_SEGMENT(this, LogDeftLedge, Log, heightRayStart, heightRayEnd, FColor::Green, TEXT("Space Reach"));
		return true;
	}

	// if we hit something there is no open space above the player so there is no ledge
	UE_VLOG_SEGMENT(this, LogDeftLedge, Log, heightRayStart, heightRayEnd, FColor::Red, TEXT("Space Reach"));
	UE_VLOG_LOCATION(this, LogDeftLedge, Log, wallHit.Location, 5.f, FColor::Red, TEXT("Space hit location"));
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
		UE_VLOG_SEGMENT(this, LogDeftLedge, Log, floorRayStart, floorRayEnd, FColor::Green, TEXT("Floor Reach"));
		UE_VLOG_LOCATION(this, LogDeftLedge, Log, outFloorLocation, 5.f, FColor::Green, TEXT("Floor hit location"));
		return true;
	}

	UE_VLOG_SEGMENT(this, LogDeftLedge, Log, floorRayStart, floorRayEnd, FColor::Red, TEXT("Floor Reach"));
	return false;

	// TODO: check surface normal maybe
}


bool UDeftMovementComponent::CheckSpaceForCapsule(const FVector& aFloorLocation)
{
	const UCapsuleComponent* capsuleComponent = CharacterOwner->GetCapsuleComponent();
	const float capsuleHalfHeight = capsuleComponent->GetScaledCapsuleHalfHeight();

	// shape sweep needs to physically sweep _some_ distance, it can't be exactly the same.
	const FVector capsuleBase = aFloorLocation + CharacterOwner->GetActorUpVector().GetSafeNormal() * 1.5f;				// start sweep: floor location raised by a tiny amount so we don't collide with the floor
	const FVector capsuleBaseSlightlyHigher = capsuleBase + CharacterOwner->GetActorUpVector().GetSafeNormal() * 1.5f;	// end sweep: slightly above the start location again just because UE requires it to be different

	// Vizlog specifies the BASE location of the capsule
	//UE_VLOG_CAPSULE(this, LogDeftLedge, Log, capsuleBase, capsuleComponent->GetScaledCapsuleHalfHeight(), capsuleComponent->GetScaledCapsuleRadius(), CharacterOwner->GetActorRotation().Quaternion(), FColor::White, TEXT("Space Sweep Start"));
	//UE_VLOG_CAPSULE(this, LogDeftLedge, Log, capsuleBaseSlightlyHigher, capsuleComponent->GetScaledCapsuleHalfHeight(), capsuleComponent->GetScaledCapsuleRadius(), CharacterOwner->GetActorRotation().Quaternion(), FColor::Yellow, TEXT("Space Sweep End"));

	FHitResult hitAnything;
	// sweep puts the CENTER of the capsule at the start and end locations so we have to raise them by half height to have the BASE be at the start and end height
	const bool bHitAnything = GetWorld()->SweepSingleByProfile(hitAnything, capsuleBase + capsuleHalfHeight, capsuleBaseSlightlyHigher + capsuleHalfHeight, CharacterOwner->GetActorRotation().Quaternion(), CharacterOwner->GetCapsuleComponent()->GetCollisionProfileName(), m_CapsuleCollisionShapeCache, m_CollisionQueryParams);
	if (!bHitAnything)
	{
		// not hitting anything means there's enough space for the character's capsule with a little wiggle room
		UE_VLOG(this, LogDeftLedge, Log, TEXT("CheckSpaceForCapsule: Player will fit"));
		return true;
	}

	// hitting something indicates there's not enough space to stand so no ledge up
	// TODO: could still indicate a ledge hang maybe
	UE_VLOG(this, LogDeftLedge, Log, TEXT("CheckSpaceForCapsule: Colliding with %s"), *hitAnything.GetActor()->GetActorNameOrLabel());
	UE_VLOG_LOCATION(this, LogDeftLedge, Log, hitAnything.Location, 5.f, FColor::Red, TEXT("Space Check Collision"));
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

	const FVector dirToWall = aWallLocation - aFloorLocation;
	const FVector projWallDirOntoFloorSurface = (dirToWall.Dot(floorForward) / floorForward.Dot(floorForward)) * floorForward;
	const float distToEdge = projWallDirOntoFloorSurface.Length();
	outLedgeEdge = aFloorLocation + floorForward.GetSafeNormal() * distToEdge;

	// draw floor axis
	DrawDebugLine(GetWorld(), aFloorLocation, aFloorLocation + floorUp * 100.f, FColor::Cyan);
	DrawDebugLine(GetWorld(), aFloorLocation, aFloorLocation + floorRight * 100.f, FColor::Green);
	DrawDebugLine(GetWorld(), aFloorLocation, aFloorLocation + floorForward * 100.f, FColor::Red);

	//UE_VLOG_SEGMENT(this, LogDeftLedge, Log, aFloorLocation, aFloorLocation + projWallDirOntoFloorSurface, FColor::Cyan, TEXT("Proj Wall Dir Onto Floor"));
	//UE_VLOG_SEGMENT(this, LogDeftLedge, Log, aFloorLocation, aFloorLocation + dirToWall.GetSafeNormal() * dirToWall.Length(), FColor::Cyan, TEXT("Floor To Wall Location"));

	//UE_VLOG_SEGMENT(this, LogDeftLedge, Log, aFloorLocation, aFloorLocation + floorUp * 100.f, FColor::Cyan, TEXT("Floor Up"));
	//UE_VLOG_SEGMENT(this, LogDeftLedge, Log, aFloorLocation, aFloorLocation + floorRight * 100.f, FColor::Green, TEXT("Floor Right"));
	UE_VLOG_SEGMENT(this, LogDeftLedge, Log, aFloorLocation, aFloorLocation + floorForward * 100.f, FColor::Red, TEXT("Floor Forward"));
	UE_VLOG_LOCATION(this, LogDeftLedge, Log, outLedgeEdge, 5.f, FColor::Blue, TEXT("Ledge Edge"));
}


void UDeftMovementComponent::GetHopUpLocation(const FVector& aLedgeEdge, FVector& outHopUpLocation)
{
	const UCapsuleComponent* capsuleComponent = CharacterOwner->GetCapsuleComponent();
	const FVector actorForward = CharacterOwner->GetActorForwardVector();

	const float halfCapsuleRadius = capsuleComponent->GetScaledCapsuleRadius() / 2.f;
	// location starting from the ledge edge, in the direction the player is facing, pushed in by half the capsule radius which gives the shortest distance we can stand on the ledge
	outHopUpLocation = aLedgeEdge + actorForward * halfCapsuleRadius;

	UE_VLOG_SEGMENT(this, LogDeftLedge, Log, aLedgeEdge, aLedgeEdge + actorForward * 50.f, FColor::Yellow, TEXT("Dir Inward From Ledge"));
	UE_VLOG_CAPSULE(this, LogDeftLedge, Log, outHopUpLocation, capsuleComponent->GetScaledCapsuleHalfHeight(), capsuleComponent->GetScaledCapsuleRadius(), CharacterOwner->GetActorRotation().Quaternion(), FColor::Green, TEXT("Hop Up Location"));
	UE_VLOG_SPHERE(this, LogDeftLedge, Log, outHopUpLocation, 5.f, FColor::Red, TEXT("Hop Up base"));
}

void UDeftMovementComponent::OnJumpApexReached()
{
	UE_LOG(LogTemp, Warning, TEXT("apex reached"));
	m_bJumpApexReached = true;
	m_bIncrementJumpInputHoldTime = false;
	m_JumpKeyHoldTime = 0.f;
	GravityScale = m_PostJumpGravityScale;

	if (m_InternalMoveMode == EInternalMoveMode::IMOVE_LedgeUp && DeftLocks::IsMoveInputForwardBackLocked())
	{
		DeftLocks::DecrementMoveInputForwardBackLockRef();
	}

#if DEBUG_VIEW
	m_PlatformJumpDebug.m_GravityValues.Add(m_DefaultGravityZCache * GravityScale);
#endif
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


void UDeftMovementComponent::ResetJump()
{
	m_InternalMoveMode = EInternalMoveMode::IMOVE_None;

	// reset ledge up v2
	if (m_bIsLedgingUp)
	{
		DeftLocks::DecrementMoveInputRightLeftLockRef();
		DeftLocks::DecrementMoveInputForwardBackLockRef();
	}
	m_bIsLedgingUp = false; // TODO: might want its own reset

	// reset jump
	m_bInPlatformJump = false;
	m_bJumpApexReached = false;
	GravityScale = m_DefaultGravityScaleCache;

	// reset falling
	m_bIsFallOriginSet = false;
	m_FallOrigin = FVector::ZeroVector;

	// reset ledge up
	m_ledgeHopUpLocationCache = FVector::ZeroVector;
	m_ledgeEdgeCache = FVector::ZeroVector;
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

	DeftLocks::DrawLockDebug();
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

	GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::White, FString::Printf(TEXT("\tVelocity %s"), *Velocity.ToString()));

	UE_VLOG_CAPSULE(this, LogDeftMovement, Log, CharacterOwner->GetActorLocation() - CharacterOwner->GetActorUpVector() * capsulHalfHeight, capsulHalfHeight, capsulComponent->GetScaledCapsuleRadius(), CharacterOwner->GetActorRotation().Quaternion(), FColor::White, TEXT(""));
}


void UDeftMovementComponent::DebugPhysFalling()
{
	if (MovementMode != MOVE_Falling)
		return;
}

void UDeftMovementComponent::DebugLedgeLaunch(const FVector& aStartLocation, const FVector& aLaunchVelocity, float aFlightTime, float aTimestep, FColor aDrawColor)
{
	const float flightTime = FMath::Min(aFlightTime, 5.f);
	if (flightTime >= 5.f)
	{
		UE_VLOG(this, LogDeftLedgeLaunchPath, Log, TEXT("flight time WAY too big [%.2f] check calculation"), aFlightTime);
	}

	FVector currentLocation = aStartLocation;
	FVector currentVelocity = aLaunchVelocity;
	const float gravity = FMath::Abs(m_DefaultGravityZCache * GravityScale);

	int i = 1;
	bool bApexReached = false;
	//Simulate the projectile's path over time
	for (float time = 0.f; time < flightTime; time += aTimestep)
	{
		// Calculate the next position using projectile motion equations
		FVector nextLocation = currentLocation + currentVelocity * aTimestep;

		// Apply gravity to the vertical component
		currentVelocity.Z -= gravity * aTimestep;

		// Just a good way to log the name at the apex of the arc versus everyone at the same
		if (!bApexReached && currentVelocity.Z < 0.f)
		{
			bApexReached = true;
			UE_VLOG_SPHERE(this, LogDeftLedgeLaunchPath, Log, nextLocation, 2.5f, aDrawColor, TEXT("FlightTime: %.2f"), flightTime);
		}

		UE_VLOG_SEGMENT(this, LogDeftLedgeLaunchPath, Log, currentLocation, nextLocation, aDrawColor, TEXT(""));
		++i;

		currentLocation = nextLocation;
	}
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