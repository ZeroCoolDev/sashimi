// Fill out your copyright notice in the Description page of Project Settings.


#include "DeftMovementComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

static TAutoConsoleVariable<bool> CVarDebugLocomotion(
	TEXT("d.DebugMovement"),
	false,
	TEXT("shows debug info for movement")
);


void UDeftMovementComponent::TickComponent(float aDeltaTime, enum ELevelTick aTickType, FActorComponentTickFunction* aThisTickFunction)
{
	Super::TickComponent(aDeltaTime, aTickType, aThisTickFunction);

#if !UE_BUILD_SHIPPING
	DrawDebug();
#endif
}


bool UDeftMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	if (CharacterOwner && CharacterOwner->CanJump()) // TODO: will have to override CanJump()
	{
		// Don't jump if we can't move up/down.
		if (!bConstrainToPlane || !FMath::IsNearlyEqual(FMath::Abs(GetGravitySpaceZ(PlaneConstraintNormal)), 1.f))
		{
			// as time advances the velocity.Z should increase to an apex, then decrease
			// position = 1/2gt^2 + v0t + p0

			/*
			*	Calculating the height of the jump:
					h = apex of the jump which design defines in advance
					t = duration to the apex of the jump which design defines

					what we don't know is the gravity needed to make this happen and the initial velocity

					initial velocity v0
					v0 = 2h / t(h)
					g = -2h / t(h)^2

				Calculating the maximum horizontal movement

			*/
		}
	}
}


void UDeftMovementComponent::StartJump()
{
	if (CharacterOwner && CharacterOwner->CanJump()) // TODO: will have to override CanJump()
	{
		// Don't jump if we can't move up/down.
		if (!bConstrainToPlane || !FMath::IsNearlyEqual(FMath::Abs(GetGravitySpaceZ(PlaneConstraintNormal)), 1.f))
		{
			CharacterOwner->Jump();
			SetMovementMode(MOVE_Custom, CustomMovementMode::CMOVE_Jump);
		}
	}
}

/*
	if ( CharacterOwner && CharacterOwner->CanJump() )
	{
		// Don't jump if we can't move up/down.
		if (!bConstrainToPlane || !FMath::IsNearlyEqual(FMath::Abs(GetGravitySpaceZ(PlaneConstraintNormal)), 1.f))
		{
			// If first frame of DoJump, we want to always inject the initial jump velocity.
			// For subsequent frames, during the time Jump is held, it depends...
			// bDontFallXXXX == true means we want to ensure the character's Z velocity is never less than JumpZVelocity in this period
			// bDontFallXXXX == false means we just want to leave Z velocity alone and "let the chips fall where they may" (e.g. fall properly in physics)

			// NOTE:
			// Checking JumpCurrentCountPreJump instead of JumpCurrentCount because Character::CheckJumpInput might have
			// incremented JumpCurrentCount just before entering this function... in order to compensate for the case when
			// on the first frame of the jump, we're already in falling stage. So we want the original value before any
			// modification here.
			//
			const bool bFirstJump = (CharacterOwner->JumpCurrentCountPreJump == 0);

			if (bFirstJump || bDontFallBelowJumpZVelocityDuringJump)
			{
				if (HasCustomGravity())
				{
					SetGravitySpaceZ(Velocity, FMath::Max<FVector::FReal>(GetGravitySpaceZ(Velocity), JumpZVelocity));
				}
				else
				{
					Velocity.Z = FMath::Max<FVector::FReal>(Velocity.Z, JumpZVelocity);
				}
			}

			SetMovementMode(MOVE_Falling);
			return true;
		}
	}

	return false;
*/

void UDeftMovementComponent::DrawDebug()
{
	if (CVarDebugLocomotion.GetValueOnGameThread())
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

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 0.01f, FColor::Yellow, FString::Printf(TEXT("Velocity: %s"), *Velocity.ToString()));
		}
	}
}
