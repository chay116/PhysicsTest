// BulletMath.h - the unit conversion and the frame algebra of the Bullet backend (02_Architecture 8.1, 8.4). [MVP M5]
//
// Units (D1): Bullet runs in metres - its constants are metres (box margin 0.04 m, contact breaking threshold 0.02 m, sleep
// threshold 0.8 m/s, btConvexInternalShape.h / btPersistentManifold.cpp:26 / btRigidBody.h [V]). Every length crosses the
// boundary through GScale (BackendCommon's unit-scale table, EEngineUnits::Bullet = GMetres) and nowhere else.
//
// Frames: a btRigidBody's world transform is its centre-of-mass frame with the principal axes (Bullet takes a diagonal
// local inertia, btRigidBody::setMassProps). The harness gives the actor pose and the COM frame in actor space
// (PtActorCreateInfo::CenterOfMassLocal), so
//   body (COM) pose  = actor pose * ComLocal
//   actor pose       = body pose * inverse(ComLocal)
//   child in body    = inverse(ComLocal) * element pose              (btCompoundShape children, 02 8.4)
// The algebra runs in double on the ABI types; only the result is rounded to Bullet's float.
#pragma once

#include "BulletIncludes.h"
#include "PtTypes.h"
#include "PtUnitScale.h"

#include <math.h>

namespace PhysicsTest::BulletBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// The one unit conversion of this backend: cm -> m (x0.01; inertia, torque and angular impulse x1e-4).
	inline constexpr ::PhysicsTest::Abi::FUnitScale GScale = ::PhysicsTest::Abi::GetEngineUnitScale(::PhysicsTest::Abi::EEngineUnits::Bullet);

	// ---- double algebra on the ABI types ------------------------------------------------------------------------------
	inline PtVec3 Add(const PtVec3& A, const PtVec3& B) { return PtVec3{ A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
	inline PtVec3 Sub(const PtVec3& A, const PtVec3& B) { return PtVec3{ A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
	inline PtVec3 Scale(const PtVec3& A, const double S) { return PtVec3{ A.X * S, A.Y * S, A.Z * S }; }
	inline PtVec3 Cross(const PtVec3& A, const PtVec3& B) { return PtVec3{ A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }

	// A * B: rotate by B, then by A.
	inline PtQuat QuatMultiply(const PtQuat& A, const PtQuat& B)
	{
		return PtQuat{ A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y, A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
			A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W, A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z };
	}
	inline PtQuat QuatConjugate(const PtQuat& Q) { return PtQuat{ -Q.X, -Q.Y, -Q.Z, Q.W }; }
	// q v q*, for a unit q: v + 2w (u x v) + 2 u x (u x v), u = (x, y, z).
	inline PtVec3 QuatRotate(const PtQuat& Q, const PtVec3& V)
	{
		const PtVec3 U{ Q.X, Q.Y, Q.Z };
		const PtVec3 T = Scale(Cross(U, V), 2.0);
		return Add(Add(V, Scale(T, Q.W)), Cross(U, T));
	}
	inline PtQuat QuatNormalize(const PtQuat& Q)
	{
		const double Size = sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
		return Size > 0.0 ? PtQuat{ Q.X / Size, Q.Y / Size, Q.Z / Size, Q.W / Size } : PtQuat{ 0.0, 0.0, 0.0, 1.0 };
	}
	// A * B: the frame B expressed in A's parent (p -> A(B(p))).
	inline PtTransform TransformMultiply(const PtTransform& A, const PtTransform& B)
	{
		return PtTransform{ QuatNormalize(QuatMultiply(A.Rotation, B.Rotation)), Add(QuatRotate(A.Rotation, B.Translation), A.Translation) };
	}
	inline PtTransform TransformInverse(const PtTransform& A)
	{
		const PtQuat Inverse = QuatConjugate(A.Rotation);
		return PtTransform{ Inverse, Scale(QuatRotate(Inverse, A.Translation), -1.0) };
	}
	// The angular velocity that turns From into To in Dt (the shorter way): axis * angle / Dt from the relative rotation
	// To * From^-1, with angle = 2 atan2(|u|, w), which stays accurate for tiny rotations (acos(w) does not).
	inline PtVec3 AngularVelocityBetween(const PtQuat& From, const PtQuat& To, const double Dt)
	{
		PtQuat Delta = QuatMultiply(To, QuatConjugate(From));
		if (Delta.W < 0.0)
		{
			Delta = PtQuat{ -Delta.X, -Delta.Y, -Delta.Z, -Delta.W };
		}
		const double SinHalf = sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y + Delta.Z * Delta.Z);
		if (!(SinHalf > 0.0) || !(Dt > 0.0))
		{
			return PtVec3{ 0.0, 0.0, 0.0 };
		}
		const double Angle = 2.0 * atan2(SinHalf, Delta.W);
		return Scale(PtVec3{ Delta.X, Delta.Y, Delta.Z }, Angle / (SinHalf * Dt));
	}

	// ---- ABI (cm) <-> Bullet (m) ------------------------------------------------------------------------------------
	inline btVector3 ToBtLength(const PtVec3& Cm)   // length, velocity, acceleration, force, linear impulse
	{
		return btVector3(btScalar(GScale.Length(Cm.X)), btScalar(GScale.Length(Cm.Y)), btScalar(GScale.Length(Cm.Z)));
	}
	inline btVector3 ToBtInertial(const PtVec3& KgCm2)   // inertia, torque, angular impulse
	{
		return btVector3(btScalar(GScale.Inertia(KgCm2.X)), btScalar(GScale.Inertia(KgCm2.Y)), btScalar(GScale.Inertia(KgCm2.Z)));
	}
	inline btVector3 ToBtVector(const PtVec3& V)   // unitless, rad/s
	{
		return btVector3(btScalar(V.X), btScalar(V.Y), btScalar(V.Z));
	}
	inline btQuaternion ToBtQuat(const PtQuat& Q)
	{
		return btQuaternion(btScalar(Q.X), btScalar(Q.Y), btScalar(Q.Z), btScalar(Q.W));
	}
	inline btTransform ToBtTransform(const PtTransform& Cm)
	{
		return btTransform(ToBtQuat(Cm.Rotation), ToBtLength(Cm.Translation));
	}
	inline PtVec3 FromBtLength(const btVector3& V)
	{
		return PtVec3{ GScale.ToCm(double(V.getX())), GScale.ToCm(double(V.getY())), GScale.ToCm(double(V.getZ())) };
	}
	inline PtVec3 FromBtVector(const btVector3& V)
	{
		return PtVec3{ double(V.getX()), double(V.getY()), double(V.getZ()) };
	}
	inline PtQuat FromBtQuat(const btQuaternion& Q)
	{
		return PtQuat{ double(Q.getX()), double(Q.getY()), double(Q.getZ()), double(Q.getW()) };
	}
	// Bullet keeps the basis as a matrix; getRotation converts it (btMatrix3x3::getRotation).
	inline PtTransform FromBtTransform(const btTransform& T)
	{
		btQuaternion Rotation;
		T.getBasis().getRotation(Rotation);
		return PtTransform{ QuatNormalize(FromBtQuat(Rotation)), FromBtLength(T.getOrigin()) };
	}
}
