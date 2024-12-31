// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DeftMovementComponent.generated.h"

/**
 * 
 */
UCLASS()
class SASHIMI_API UDeftMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
	
	
public:
	virtual void TickComponent(float aDeltaTime, enum ELevelTick aTickType, FActorComponentTickFunction* aThisTickFunction) override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	// Override reason: No reason at the moment
	void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	void StartJump();

	void DrawDebug();
	
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump Control", meta=(ToolTip="Maximum jump height (cm)"))
	float JumpMaxHeight;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump Control", meta=(ToolTip="Time it takes to reach the maximum jump height"))
	float TimeToJumpMaxHeight;

private:
};
