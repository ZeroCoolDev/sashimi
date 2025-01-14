// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "Sashimi/Sashimi.h"

#include "PlayerCharacter.generated.h"

//DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJumpPressed);
//DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJumpReleased);

UCLASS()
class SASHIMI_API APlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	APlayerCharacter(const FObjectInitializer& aObjectInitializer);

	virtual void Tick(float DeltaTime) override;
	virtual void Jump() override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	void Move(const FInputActionValue& aValue);
	void Look(const FInputActionValue& aValue);
	void OnJumpPressed();
	void OnJumpReleased();
	void AirDash();

protected:

	// Spring arm component to follow the camera camera behind the player
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Camera)
	class USpringArmComponent* SpringArmComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Camera)
	class UCameraComponent* CameraComp;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	class UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	class UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	class UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	class UInputAction* LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	class UInputAction* AirDashAction;

private:


	bool m_bIsInJump = false;
#if DEBUG_VIEW
	void DrawDebug();
	FVector2D m_MoveInputVector;
#endif	
};
