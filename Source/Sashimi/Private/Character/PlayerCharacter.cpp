#include "Character/PlayerCharacter.h"
#include "DeftMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"

// Sets default values
APlayerCharacter::APlayerCharacter(const FObjectInitializer& aObjectInitializer)
: Super(aObjectInitializer.SetDefaultSubobjectClass<UDeftMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SpringArmComp = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmComp"));
	CameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComp"));

	// Set the location and rotation of the character mesh transform
	if (USkeletalMeshComponent* skeletalMesh = GetMesh())
	{
		skeletalMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FQuat(FRotator(0.f, -90.f, 0.f)));

		// attach your class components to the default characters skeletal mesh component
		SpringArmComp->SetupAttachment(skeletalMesh);
	}

	CameraComp->SetupAttachment(SpringArmComp, USpringArmComponent::SocketName);
	CameraComp->bUsePawnControlRotation = true;
}

// Called when the game starts or when spawned
void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	if (APlayerController* playerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* inputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(playerController->GetLocalPlayer()))
		{
			inputSubsystem->AddMappingContext(DefaultMappingContext, 0);
		}

		playerController->PlayerCameraManager->ViewPitchMin = -179.f;
		playerController->PlayerCameraManager->ViewPitchMax = 179.f;
	}

	if (UDeftMovementComponent* deftCharacterMovementComponent = Cast<UDeftMovementComponent>(GetCharacterMovement()))
	{
		// landed from air callback
	}
}


void APlayerCharacter::Move(const FInputActionValue& aValue)
{
	FVector2D inputVector = aValue.Get<FVector2D>();
	UE_LOG(LogTemp, Warning, TEXT("%s"), *inputVector.ToString());
	// TODO: add custom player controller
	if (Controller)
	{
		// find out which way is forward
		const FRotator rotation = Controller->GetControlRotation();
		const FRotator yawRotation(0, rotation.Yaw, 0);

		// get forward vector
		// note: is there a benefit from getting forward form the rotation and not just ActorForwardVector?
		const FVector forwardDir = FRotationMatrix(yawRotation).GetUnitAxis(EAxis::X);
		const FVector rightDir = FRotationMatrix(yawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(forwardDir, inputVector.Y);
		AddMovementInput(rightDir, inputVector.X);
	}
}


void APlayerCharacter::Look(const FInputActionValue& aValue)
{
	FVector2D inputVector = aValue.Get<FVector2D>();

	if (Controller)
	{
		AddControllerYawInput(inputVector.X);
		AddControllerPitchInput(inputVector.Y);
	}
}


void APlayerCharacter::PerformJump()
{
	// TODO: obviously change to be _my_ jump
	Jump();
}

// Called every frame
void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* inputComp = static_cast<UEnhancedInputComponent*>(PlayerInputComponent))
	{
		// Jump
		inputComp->BindAction(JumpAction, ETriggerEvent::Started, this, &APlayerCharacter::Jump);
		//inputComp->BindAction(JumpAction, ETriggerEvent::Completed, this, &AJakCharacter::StopJumpProxy);

		// 
		inputComp->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
		inputComp->BindAction(MoveAction, ETriggerEvent::Completed, this, &APlayerCharacter::Move);

		// Look
		inputComp->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
	}
}

