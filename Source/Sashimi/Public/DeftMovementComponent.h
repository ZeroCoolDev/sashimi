// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sashimi/Sashimi.h"
#include "DeftMovementComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDeftMovement, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogDeftLedge, Log, All);

namespace CustomMovement
{
	UENUM(BlueprintType)
	enum ECustomMovementMode
	{
		PlatformJump	UMETA(DisplayName="Platform Jump"),
		COUNT			UMETA(Hidden)
	};
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
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual bool CanAttemptJump() const override;

	void OnJumpPressed();
	void OnJumpReleased();

protected:
	virtual void PhysFalling(float aDeltaTime, int32 aIterations) override;
	void ResetFromFalling();

	// TODO: rename
	// changes the gravity if need be based off how long the player holds the jump button
	void PhysPlatformJump(float aDeltaTime);

	bool FindLedge();

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



	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control | Wall Reach", meta=(ToolTip="Maximum reach distance a ledge can be in front of the player"))
	float WallReach;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control | Ledge Height", meta=(ToolTip="Maximum reach distance a ledge can be in front of the player"))
	float LedgeHeightOrigin;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control | Ledge Height Reach", meta=(ToolTip="Maximum reach distance a ledge can be in front of the player"))
	float LedgeHeightForwardReach;


	// Ledge
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control | Reach Distance", meta=(ToolTip="Maximum reach distance a ledge can be in front of the player"))
	float LedgeForwardReachMax;
	// Height added to the capsule half height where ledges should be checked for
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ledge Control | Reach Distance", meta=(ToolTip="Maximum reach distance ledge can be above the player"))
	float LedgeVerticalReachMax;

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

	// Ledge Physics

	// Default Physics
	float m_DefaultGravityZCache = 0.f;
	float m_DefaultGravityScaleCache = 0.f;
	float m_PreviousVelocityZ = 0.f;

	FCollisionQueryParams m_CollisionQueryParams;
	FCollisionShape m_CapsuleCollisionShapeCache;
	FCollisionShape m_SphereCollisionShape;

#if DEBUG_VIEW
	void DrawDebug();
	void DebugMovement();
	void DebugPhysFalling();

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
