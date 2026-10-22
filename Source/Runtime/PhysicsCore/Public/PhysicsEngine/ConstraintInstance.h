// ConstraintInstance.h - FConstraintInstance and FConstraintProfileProperties, Phase-1 subset (UE: Engine/Classes/
// PhysicsEngine/ConstraintInstance.h, ConstraintTypes.h). [MVP M1: data; M9: FPhysScene creates them in the backend
// (FlushDeferredActors -> CreateConstraints, docs/Abi.md 9.4)]
// UE's nesting is kept (ConstraintInstance.h:105-115 [V]): ProfileInstance.LinearLimit / ConeLimit / TwistLimit and
// bDisableCollision. Defaults: ConstraintTypes.cpp:43-70 [V]. Drives, soft limits, projection and breakage are not
// part of Phase 1 (ball and hinge only, DL31). Body1 is the child (ConstraintBone1), Body2 the parent
// (ConstraintBone2, nullptr = world) (ConstraintInstance.h:269-310 [V]).
#pragma once

#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Physics/PhysicsInterfaceTypes.h"

#include <string>

namespace PhysicsTest
{
	struct FBodyInstance;

	enum class ELinearConstraintMotion : uint8
	{
		LCM_Free,
		LCM_Limited,
		LCM_Locked,
	};

	enum class EAngularConstraintMotion : uint8
	{
		ACM_Free,
		ACM_Limited,
		ACM_Locked,
	};

	enum class EConstraintFrame : uint8
	{
		Frame1,   // child (Body1)
		Frame2,   // parent (Body2)
	};

	struct FLinearConstraint
	{
		float Limit = 0.0f;   // cm
		ELinearConstraintMotion XMotion = ELinearConstraintMotion::LCM_Locked;
		ELinearConstraintMotion YMotion = ELinearConstraintMotion::LCM_Locked;
		ELinearConstraintMotion ZMotion = ELinearConstraintMotion::LCM_Locked;
	};

	struct FConeConstraint
	{
		float Swing1LimitDegrees = 45.0f;
		float Swing2LimitDegrees = 45.0f;
		EAngularConstraintMotion Swing1Motion = EAngularConstraintMotion::ACM_Free;
		EAngularConstraintMotion Swing2Motion = EAngularConstraintMotion::ACM_Free;
	};

	struct FTwistConstraint
	{
		float TwistLimitDegrees = 45.0f;
		EAngularConstraintMotion TwistMotion = EAngularConstraintMotion::ACM_Free;
	};

	struct FConstraintProfileProperties
	{
		FLinearConstraint LinearLimit;
		FConeConstraint ConeLimit;
		FTwistConstraint TwistLimit;
		bool bDisableCollision = false;
	};

	struct FConstraintInstance
	{
		std::string JointName;
		FBodyInstance* Body1 = nullptr;   // child (ConstraintBone1)
		FBodyInstance* Body2 = nullptr;   // parent (ConstraintBone2); nullptr = world
		// Reference frames in the bodies' spaces (UE ConstraintInstance.h:288-310 [V]): position, primary (X) and
		// secondary (Y) axis; the third axis is Pri ^ Sec.
		FVector Pos1;
		FVector PriAxis1 = FVector(1.0, 0.0, 0.0);
		FVector SecAxis1 = FVector(0.0, 1.0, 0.0);
		FVector Pos2;
		FVector PriAxis2 = FVector(1.0, 0.0, 0.0);
		FVector SecAxis2 = FVector(0.0, 1.0, 0.0);
		FConstraintProfileProperties ProfileInstance;
		FPhysicsConstraintHandle ConstraintHandle;

		// UE: FConstraintInstance::GetRefFrame - rotation with columns (Pri, Sec, Pri ^ Sec) and translation Pos.
		[[nodiscard]] FTransform GetRefFrame(EConstraintFrame Frame) const;
		// M9: the frame in world space at the host's last pulled pose of its body (FBodyInstance::State.BodyTM x GetRefFrame;
		// Frame2 itself for the world). The host view of PtJointSnapshot::Frame1World / Frame2World (docs/Abi.md 9.4).
		[[nodiscard]] FTransform GetWorldFrame(EConstraintFrame Frame) const;
		// M9: the backend simulates this joint (FPhysScene created it there; a released body or a backend without the joint's
		// capability leaves it host-only).
		[[nodiscard]] bool IsSimulated() const { return ConstraintHandle.IsValid(); }

		[[nodiscard]] EAngularConstraintMotion GetAngularSwing1Motion() const { return ProfileInstance.ConeLimit.Swing1Motion; }
		[[nodiscard]] EAngularConstraintMotion GetAngularSwing2Motion() const { return ProfileInstance.ConeLimit.Swing2Motion; }
		[[nodiscard]] EAngularConstraintMotion GetAngularTwistMotion() const { return ProfileInstance.TwistLimit.TwistMotion; }
	};
}
