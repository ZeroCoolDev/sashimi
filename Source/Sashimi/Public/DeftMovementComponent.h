// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sashimi/Sashimi.h"
#include "DeftMovementComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDeftMovement, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDeftLedge, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDeftLedgeLaunchTrajectory, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDeftLedgeLaunchPath, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDeftAirDash, Log, All);

namespace CustomMovement
{
	UENUM(BlueprintType)
	enum ECustomMovementMode
	{
		LedgeHang	UMETA(DisplayName="Ledge Hang"),
		AirDash		UMETA(DisplayName="Air Dash"),
		COUNT		UMETA(Hidden)
	};
};

enum EInternalMoveMode
{
	IMOVE_Jump,
	IMOVE_LedgeUp,
	IMOVE_AirDash,
	IMOVE_None
};

/**
 * 
 */
UCLASS()
class SASHIMI_API UDeftMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
	
	
public:
	UDeftMovementComponent();
	virtual void BeginPlay();
	virtual void TickComponent(float aDeltaTime, enum ELevelTick aTickType, FActorComponentTickFunction* aThisTickFunction) override;

	// Apply instantaneous velocity in Z direction then set to Falling
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	// Used to know when we exit MOVE_Falling so we can reset the jump. Jumps can be reset on demand as well for example when performing a mid-air dash
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	// Allows special updating of internal movement mode after standard movement
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity);
	// We manually track jump input
	virtual bool CanAttemptJump() const override;

	// Jump Input has been pressed
	void OnJumpPressed();
	// Jump Input has been released
	void OnJumpReleased();

	void OnAirDash();

protected:
	virtual void PhysFalling(float aDeltaTime, int32 aIterations) override;

	// Applies any movement updates necessary each frame after the standard CharacterMovementMode is applied
	void UpdateInternalMoveMode(float aDeltaTime);

	bool FindLedge();
	void PerformLedgeUp();

	bool CheckForWall(FVector& outWallLocation);
	bool CheckForLedge(const FVector& aWallLocation, FVector& outHeightDistance);
	bool CheckLedgeSurface(const FVector& aFloorCheckHeightOrigin, FVector& outFloorLocation, FVector& outFloorNormal);
	bool CheckSpaceForCapsule(const FVector& aFloorLocation);
	void GetLedgeEdge(const FVector& aFloorLocation, const FVector& aFloorNormal, FVector& aWallLocation, FVector& outLedgeEdge);
	void GetHopUpLocation(const FVector& aLedgeEdge, FVector& outHopUpLocation);

private:
	void OnJumpApexReached();
	// Calculates the initial velocity needed to achieve the desired height in the desired time
	FVector CalculateJumpInitialVelocity(float aTime, float aHeight);
	// Calculates the gravity scale needed to achieve the desired height in the desired time
	float CalculateJumpGravityScale(float aTime, float aHeight);
	bool IsAttemptingDoubleJump() const { return m_bInPlatformJump && m_bIsFallOriginSet; }
	void ResetJump();

protected:
	// Max Jump height if the player holds the button the required max time
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump Control | Pre Jump", meta=(ToolTip="Maximum jump height [hold button max time] (cm)"))
	float JumpMaxHeight;
	
	// Time it takes to reach max jump height.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump Control | Pre Jump", meta=(ToolTip="Time it takes to reach the maximum jump height"))
	float TimeToJumpMaxHeight;
	
	// Min Jump height if the player releases button before required max time
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump Control | Pre Jump", meta=(ToolTip="Maximum jump height [hold button max time] (cm)"))
	float JumpMinHeight;
	
	// Used to calculate a different gravity for falling after a jump, the same JumpMaxHeight is used for consistent height
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump Control | Post Jump", meta=(ToolTip="Time it takes to reach the maximum jump height"))
	float PostTimeToJumpMaxHeight;
	
	// How long jump button must be pressed to achieve MaximumJumpHeight
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump Control | Variable Jump", meta=(ToolTip="Maximum hold time needed to achieve maximum jump height (cm)"))
	float JumpKeyMaxHoldTime;


	// ledege up
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control | Wall Reach", meta=(ToolTip="Maximum reach distance a ledge can be in front of the player"))
	float WallReach;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control | Ledge Height", meta=(ToolTip="Maximum reach distance a ledge can be in front of the player"))
	float LedgeHeightOrigin;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control | Ledge Height Reach", meta=(ToolTip="Maximum reach distance a ledge can be in front of the player"))
	float LedgeHeightForwardReach;

	// ledge up 2.0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control v2 ", meta=(ToolTip="Additional offset to apply to the minimum height the ledge up jumps the player (which is high enough for the capsule to be just above ledge) "))
	float LedgeUpAdditionalHeightOffset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control v2 ", meta=(ToolTip="Time it takes (in seconds) to reach the ledge up height"))
	float TimeToReachLedgeUpHeight;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control v2 ", meta=(ToolTip="The minimum forward velocity to perform on ledge up if the player has no input ensuring we at least land on the ledge"))
	float LedgeUpForwardMinBoost;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control v2 ", meta=(ToolTip="Force Feedback Effect to use for ledge up"))
	TObjectPtr<class UForceFeedbackEffect> LedgeUpFeedback;

	// Air Dash
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air Dash", meta=(ToolTip="How far (cm) should air dashing send you"))
	float AirDashDistance;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air Dash", meta=(ToolTip="Time it takes (in seconds) to reach the air dash distance"))
	float AirDashTime;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air Dash", meta=(ToolTip="(optional) vertical force for a very slight curve"))
	float AirDashVerticalHeight;

private:
	// Jump Physics
	FVector m_PlatformJumpInitialPosition = FVector::ZeroVector;
	float m_PlatformJumpApex = 0.f;				// jump apex height
	float m_MaxPreJumpGravityScale = 0.f;		// gravity scale to reach max height
	float m_MinPreJumpGravityScale = 0.f;		// gravity scale to reach min height
	float m_PostJumpGravityScale = 0.f;			// constant "falling" gravity scale does not change once the apex of a jump has been reached
	float m_JumpKeyHoldTime = 0.f;				// keeps track of how long we hold the jump button
	bool m_bJumpApexReached = false;			// true when the apex of the jump has been reached
	bool m_bInPlatformJump = false;				// true when we are in the air because of a jump (versus falling off ledge or otherwise)
	bool m_bIncrementJumpInputHoldTime = false;	// true when we should count how long the player held the jump input button for variable jumping
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump Control | Coyote Jump", meta=(AllowPrivateAccess = "true", AllowToolTip = "determines how far we can fall (from the point when Velocity turns negative) before being able to jump. Allows for coyote time while restricting jumps after a certain velocity"))
	float m_FallDistanceJumpThreshold = 0.f;	// determines how far we can fall (from the point when Velocity turns negative) before being able to jump. Allows for coyote time while restricting jumps after a certain velocity
	FVector m_FallOrigin = FVector::ZeroVector;	// determines the last location we started falling from. NOTE: Only valid when in MOVE_Falling, and only set when Velocity becomes negative
	bool m_bIsFallOriginSet = false;			// indicates whether we set the location we started truely falling from (velocity goes from pos -> neg)
	bool m_bIsJumpButtonDown = false;
	uint8 m_JumpInputCounter = 0;
	uint8 m_JumpInputMax = 2;

	// Ledge Physics
	FVector m_ledgeEdgeCache;
	FVector m_ledgeHopUpLocationCache;
	bool m_bIsLedgingUp;
	float m_ledgeBoostTime;
	float m_ledgeBoostMaxTime;

	// Air Dash Physics
	bool m_bHasAirDashed = false;

	// Default Physics
	float m_DefaultGravityZCache = 0.f;
	float m_DefaultGravityScaleCache = 0.f;
	float m_PreviousVelocityZ = 0.f;

	void SetInternalMoveMode(EInternalMoveMode aInternalMoveMode) { m_InternalMoveMode = aInternalMoveMode; }
	EInternalMoveMode m_InternalMoveMode = EInternalMoveMode::IMOVE_None;

	FCollisionQueryParams m_CollisionQueryParams;
	FCollisionShape m_CapsuleCollisionShapeCache;
	FCollisionShape m_SphereCollisionShape;

#if DEBUG_VIEW
	void DrawDebug();
	void DebugMovement();
	void DebugPhysFalling();
	void DebugLedgeLaunch(const FVector& aStartLocation, const FVector& aLaunchVelocity, float aFlightTime, float aTimestep, FColor aDrawColor);

	void DebugPlatformJump();
	struct PlatformJumpDebug
	{
		// TODO: need to clear whenever we land or initiate a new jump
		TArray<float>	m_GravityValues; // there are going to be multiple gravities
		float			m_InitialVelocity = 0.f;
	} m_PlatformJumpDebug;

	struct LedgeDebug
	{
		FVector m_GeometryHitLocation;
		bool m_GeometryHit;
	} m_LedgeDebug;
#endif
};
