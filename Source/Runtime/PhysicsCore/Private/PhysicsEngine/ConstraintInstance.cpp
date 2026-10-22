// ConstraintInstance.cpp - reference frames and the ball / hinge presets (UE ConstraintInstance.cpp:792-812,
// ConstraintUtils.cpp:8-27, :70-137 [V]) and, from M9, the world frames of a joint at the host's pulled body poses.
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/ConstraintUtils.h"

#include "PhysicsEngine/BodyInstance.h"

#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "Physics/Matrix33.h"

namespace PhysicsTest
{
	FTransform FConstraintInstance::GetRefFrame(const EConstraintFrame Frame) const
	{
		const bool bFrame1 = Frame == EConstraintFrame::Frame1;
		const FVector& Pri = bFrame1 ? PriAxis1 : PriAxis2;
		const FVector& Sec = bFrame1 ? SecAxis1 : SecAxis2;
		const FVector& Pos = bFrame1 ? Pos1 : Pos2;
		const Private::FMatrix33 Axes = Private::FMatrix33::FromColumns(Pri, Sec, Pri ^ Sec);
		if (FMath::Abs(Axes.Determinant() - 1.0) > 0.01)
		{
			// UE logs the same warning (ConstraintInstance.cpp:805-809).
			UE_LOG(LogPhysicsTest, Warning, "FConstraintInstance::GetRefFrame: '%s' frame axes are not orthonormal (contained scale)", JointName.c_str());
		}
		return FTransform(Private::QuatFromRotationMatrix(Axes), Pos);
	}

	FTransform FConstraintInstance::GetWorldFrame(const EConstraintFrame Frame) const
	{
		const FTransform Local = GetRefFrame(Frame);
		const FBodyInstance* Body = Frame == EConstraintFrame::Frame1 ? Body1 : Body2;
		if (Body == nullptr)
		{
			return Local;   // the world: Frame2 is already a world pose
		}
		const FTransform& BodyTM = Body->State.BodyTM;
		return FTransform((BodyTM.GetRotation() * Local.GetRotation()).GetNormalized(), BodyTM.TransformPosition(Local.GetTranslation()));
	}

	namespace ConstraintUtils
	{
		namespace
		{
			int32 CountNonLockedAngular(const FConstraintInstance& ConstraintInstance)
			{
				int32 AngularDOF = 0;
				AngularDOF += ConstraintInstance.GetAngularSwing1Motion() != EAngularConstraintMotion::ACM_Locked ? 1 : 0;
				AngularDOF += ConstraintInstance.GetAngularSwing2Motion() != EAngularConstraintMotion::ACM_Locked ? 1 : 0;
				AngularDOF += ConstraintInstance.GetAngularTwistMotion() != EAngularConstraintMotion::ACM_Locked ? 1 : 0;
				return AngularDOF;
			}

			bool IsLinearLocked(const FConstraintInstance& ConstraintInstance)
			{
				const FLinearConstraint& Linear = ConstraintInstance.ProfileInstance.LinearLimit;
				return Linear.XMotion == ELinearConstraintMotion::LCM_Locked && Linear.YMotion == ELinearConstraintMotion::LCM_Locked
					&& Linear.ZMotion == ELinearConstraintMotion::LCM_Locked;
			}

			void LockLinear(FConstraintInstance& ConstraintInstance)
			{
				FLinearConstraint& Linear = ConstraintInstance.ProfileInstance.LinearLimit;
				Linear.XMotion = ELinearConstraintMotion::LCM_Locked;
				Linear.YMotion = ELinearConstraintMotion::LCM_Locked;
				Linear.ZMotion = ELinearConstraintMotion::LCM_Locked;
			}
		}

		void ConfigureAsBallAndSocket(FConstraintInstance& ConstraintInstance, const bool bOverwriteLimits)
		{
			LockLinear(ConstraintInstance);
			FConstraintProfileProperties& Profile = ConstraintInstance.ProfileInstance;
			Profile.ConeLimit.Swing1Motion = EAngularConstraintMotion::ACM_Free;
			Profile.ConeLimit.Swing2Motion = EAngularConstraintMotion::ACM_Free;
			Profile.TwistLimit.TwistMotion = EAngularConstraintMotion::ACM_Free;
			if (bOverwriteLimits)
			{
				Profile.ConeLimit.Swing1LimitDegrees = 0.0f;
				Profile.ConeLimit.Swing2LimitDegrees = 0.0f;
				Profile.TwistLimit.TwistLimitDegrees = 0.0f;
			}
		}

		void ConfigureAsHinge(FConstraintInstance& ConstraintInstance, const bool bOverwriteLimits)
		{
			LockLinear(ConstraintInstance);
			FConstraintProfileProperties& Profile = ConstraintInstance.ProfileInstance;
			Profile.ConeLimit.Swing1Motion = EAngularConstraintMotion::ACM_Locked;
			Profile.ConeLimit.Swing2Motion = EAngularConstraintMotion::ACM_Locked;
			Profile.TwistLimit.TwistMotion = EAngularConstraintMotion::ACM_Free;
			if (bOverwriteLimits)
			{
				Profile.ConeLimit.Swing1LimitDegrees = 0.0f;
				Profile.ConeLimit.Swing2LimitDegrees = 0.0f;
				Profile.TwistLimit.TwistLimitDegrees = 45.0f;
			}
		}

		bool IsBallAndSocket(const FConstraintInstance& ConstraintInstance)
		{
			return IsLinearLocked(ConstraintInstance) && CountNonLockedAngular(ConstraintInstance) > 1;
		}

		bool IsHinge(const FConstraintInstance& ConstraintInstance)
		{
			return IsLinearLocked(ConstraintInstance) && CountNonLockedAngular(ConstraintInstance) == 1;
		}
	}
}
