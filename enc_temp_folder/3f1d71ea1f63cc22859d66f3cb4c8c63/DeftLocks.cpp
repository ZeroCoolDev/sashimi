#include "DeftLocks.h"

uint8 DeftLocks::m_MoveInputForwardBackLock = 0;
bool DeftLocks::IsMoveInputForwardBackLocked() { return m_MoveInputForwardBackLock > 0; }
void DeftLocks::IncrementMoveInputForwardBackLockRef() { ++m_MoveInputForwardBackLock; }
void DeftLocks::DecrementMoveInputForwardBackLockRef() { m_MoveInputForwardBackLock = FMath::Max((uint8)0, --m_MoveInputForwardBackLock); }

uint8 DeftLocks::m_MoveInputRightLeftLock = 0;
bool DeftLocks::IsMoveInputRightLeftLocked() { return m_MoveInputRightLeftLock > 0; }
void DeftLocks::IncrementMoveInputRightLeftockRef() { ++m_MoveInputRightLeftLock; }
void DeftLocks::DecrementMoveInputRightLeftLockRef() { m_MoveInputRightLeftLock = FMath::Max((uint8)0, --m_MoveInputRightLeftLock); }

#if !UE_BUILD_SHIPPING
void DeftLocks::DrawLockDebug()
{
	GEngine->AddOnScreenDebugMessage(-1, 0.005f, DeftLocks::IsMoveInputForwardBackLocked() ? FColor::Red : FColor::White, FString::Printf(TEXT("\tMove Input Forward/Back: %u"), m_MoveInputForwardBackLock));
	GEngine->AddOnScreenDebugMessage(-1, 0.005f, DeftLocks::IsMoveInputRightLeftLocked() ? FColor::Red : FColor::White, FString::Printf(TEXT("\tMove Input Right/Left: %u"), m_MoveInputRightLeftLock));
	GEngine->AddOnScreenDebugMessage(-1, 0.005f, FColor::White, TEXT("\n-Locks-"));
}
void DeftLocks::UnlockAll()
{
	m_MoveInputForwardBackLock = 0;
	m_MoveInputRightLeftLock = 0;
}
#endif//!UE_BUILD_SHIPPING