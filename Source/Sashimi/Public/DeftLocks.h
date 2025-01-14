#pragma once

#include "CoreMinimal.h"

class SASHIMI_API DeftLocks
{
	public:
		DeftLocks() {}
		virtual ~DeftLocks(){}

		static bool IsMoveInputForwardBackLocked();
		static void IncrementMoveInputForwardBackLockRef();
		static void DecrementMoveInputForwardBackLockRef();

		static bool IsMoveInputRightLeftLocked();
		static void IncrementMoveInputRightLeftockRef();
		static void DecrementMoveInputRightLeftLockRef();

	private:
		static int8 m_MoveInputForwardBackLock;
		static int8 m_MoveInputRightLeftLock;

#if !UE_BUILD_SHIPPING
public:
	static void DrawLockDebug();
	static void UnlockAll();
#endif
};