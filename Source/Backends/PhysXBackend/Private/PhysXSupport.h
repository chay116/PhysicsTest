// PhysXSupport.h - UE4's PhysXSupport pieces, re-implemented clean room from the design (02_Architecture 8.2, A.14):
// the U2P / P2U conversions with the variant's length scale, U2PSphylBasis, FPhysxUserData, the 16-byte aligned
// FPhysXAllocator with live / peak accounting and FPhysXErrorCallback, which turns PhysX error reports into engine error
// bits. [MVP M5]   PT_BACKEND_NS=PhysX
//
// Units (D1, BackendCommon PtUnitScale.h): the ABI is cm; L = native length units per cm is the variant's LengthScale
// (UE4Compat / -TGS / -Stab 1 = cm, SdkDefault 0.01 = m). Every conversion of this backend goes through FUnitScale here:
// x L lengths, velocities, forces, linear impulses; x L^2 inertia, torques, angular impulses; x 1 mass, angles.
#pragma once

#include "PhysXIncludes.h"
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtTypes.h"
#include "PtUnitScale.h"

#include <atomic>

namespace PhysicsTest::PhysXBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// ---- U2P / P2U ----------------------------------------------------------------------------------------------------
	inline physx::PxVec3 U2PVector(const PtVec3& V, const ::PhysicsTest::Abi::FUnitScale& Scale)   // length-like (x L)
	{
		return physx::PxVec3(static_cast<float>(Scale.Length(V.X)), static_cast<float>(Scale.Length(V.Y)), static_cast<float>(Scale.Length(V.Z)));
	}
	inline physx::PxVec3 U2PInertialVector(const PtVec3& V, const ::PhysicsTest::Abi::FUnitScale& Scale)   // inertia, torque, angular impulse (x L^2)
	{
		return physx::PxVec3(static_cast<float>(Scale.Inertia(V.X)), static_cast<float>(Scale.Inertia(V.Y)), static_cast<float>(Scale.Inertia(V.Z)));
	}
	inline physx::PxVec3 U2PUnitless(const PtVec3& V)   // rad/s, unit vectors
	{
		return physx::PxVec3(static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
	}
	inline physx::PxQuat U2PQuat(const PtQuat& Q)
	{
		// The ABI quaternion is unit within 1e-6 in double; renormalised in float so that PhysX's isSane / isUnit checks hold.
		return physx::PxQuat(static_cast<float>(Q.X), static_cast<float>(Q.Y), static_cast<float>(Q.Z), static_cast<float>(Q.W)).getNormalized();
	}
	inline physx::PxTransform U2PTransform(const PtTransform& T, const ::PhysicsTest::Abi::FUnitScale& Scale)
	{
		return physx::PxTransform(U2PVector(T.Translation, Scale), U2PQuat(T.Rotation));
	}
	inline PtVec3 P2UVector(const physx::PxVec3& V, const ::PhysicsTest::Abi::FUnitScale& Scale)
	{
		return PtVec3{ Scale.ToCm(static_cast<double>(V.x)), Scale.ToCm(static_cast<double>(V.y)), Scale.ToCm(static_cast<double>(V.z)) };
	}
	inline PtVec3 P2UUnitless(const physx::PxVec3& V)
	{
		return PtVec3{ static_cast<double>(V.x), static_cast<double>(V.y), static_cast<double>(V.z) };
	}
	inline PtQuat P2UQuat(const physx::PxQuat& Q)
	{
		return PtQuat{ static_cast<double>(Q.x), static_cast<double>(Q.y), static_cast<double>(Q.z), static_cast<double>(Q.w) };
	}
	inline PtTransform P2UTransform(const physx::PxTransform& T, const ::PhysicsTest::Abi::FUnitScale& Scale)
	{
		return PtTransform{ P2UQuat(T.q), P2UVector(T.p, Scale) };
	}

	// UE capsules (FKSphylElem) lie along their local Z, PhysX capsules (PxCapsuleGeometry) along X: the shape's local pose
	// is element pose * U2PSphylBasis, BackendCommon's CapsuleAxisRemapUE4SphylBasis() (180 deg about (1, 0, 1) / sqrt 2:
	// swaps X and Z and flips Y) in float. A capsule is symmetric, so this and PtUnitScale's CapsuleAxisRemap(X) (-90 deg
	// about Y) describe the same shape (02 8.1, D1 / DL47).
	inline physx::PxQuat U2PSphylBasis()
	{
		return U2PQuat(::PhysicsTest::Abi::CapsuleAxisRemapUE4SphylBasis());
	}

	// ---- FPhysxUserData -----------------------------------------------------------------------------------------------
	// What PxActor::userData / PxShape::userData point at in UE4: a type tag and an index. Here the two are packed into
	// the pointer-sized value itself (no per-body allocation, no pointer into an array that may grow): bits 0-31 the index
	// (body index for actors, harness element index for shapes), bits 32-39 the type.
	enum class EPhysxUserDataType : uint8
	{
		Invalid = 0,
		Body = 1,    // PxRigidActor: the harness body index
		Shape = 2,   // PxShape: the element index inside its body (PtContactPoint::Shape0 / Shape1)
		Joint = 3,   // PxJoint: the harness joint index (creation order, PtJointSnapshot::JointIndex; M9)
	};

	struct FPhysxUserData
	{
		EPhysxUserDataType Type = EPhysxUserDataType::Invalid;
		uint32 Index = 0;

		static void* Encode(const EPhysxUserDataType Type, const uint32 Index)
		{
			return reinterpret_cast<void*>((static_cast<uintptr_t>(static_cast<uint8>(Type)) << 32u) | static_cast<uintptr_t>(Index));
		}
		static FPhysxUserData Decode(const void* UserData)
		{
			const uintptr_t Bits = reinterpret_cast<uintptr_t>(UserData);
			FPhysxUserData Out;
			Out.Type = static_cast<EPhysxUserDataType>((Bits >> 32u) & 0xFFu);
			Out.Index = static_cast<uint32>(Bits & 0xFFFFFFFFu);
			return Out;
		}
	};

	// ---- FPhysXAllocator ----------------------------------------------------------------------------------------------
	// PhysX requires 16-byte aligned blocks (PxAllocatorCallback.h). Each block carries a 16-byte header with its size so
	// that live bytes, the peak and the allocation / free counts are exact (module-wide: PhysX has one allocator per
	// PxFoundation). Thread-safe: PhysX worker threads allocate (T >= 2).
	class FPhysXAllocator final : public physx::PxAllocatorCallback
	{
	public:
		void* allocate(size_t Size, const char* TypeName, const char* Filename, int Line) override;
		void deallocate(void* Ptr) override;

		int64 GetLiveBytes() const { return LiveBytes.load(std::memory_order_relaxed); }
		int64 GetPeakBytes() const { return PeakBytes.load(std::memory_order_relaxed); }
		int64 GetNumAllocs() const { return NumAllocs.load(std::memory_order_relaxed); }
		int64 GetNumFrees() const { return NumFrees.load(std::memory_order_relaxed); }

	private:
		std::atomic<int64> LiveBytes{ 0 };
		std::atomic<int64> PeakBytes{ 0 };
		std::atomic<int64> NumAllocs{ 0 };
		std::atomic<int64> NumFrees{ 0 };
	};

	// ---- FPhysXErrorCallback ------------------------------------------------------------------------------------------
	// Every PhysX report goes to the host log (Kit LogToHost, any thread) and is counted per PxErrorCode. Scenes compare
	// the counts with what they saw last (FPhysScene_PhysX::CheckPhysXErrors) and turn new reports into engine error bits:
	// the bit is the PxErrorCode value itself (eDEBUG_WARNING 0x2, eINVALID_PARAMETER 0x4, eINVALID_OPERATION 0x8,
	// eOUT_OF_MEMORY 0x10, eINTERNAL_ERROR 0x20, eABORT 0x40). eDEBUG_INFO and ePERF_WARNING are logged and counted but are
	// not errors. As Jolt's assert counter, the counts are module-wide: a report raised while one scene steps is seen by
	// every scene that checks after it (PhysX has no per-scene error callback).
	inline constexpr uint32 GPhysXNumErrorCodes = 8;   // bit positions of PxErrorCode 0x1 .. 0x80
	inline constexpr uint32 GPhysXErrorBitsMask = static_cast<uint32>(physx::PxErrorCode::eDEBUG_WARNING) | static_cast<uint32>(physx::PxErrorCode::eINVALID_PARAMETER)
		| static_cast<uint32>(physx::PxErrorCode::eINVALID_OPERATION) | static_cast<uint32>(physx::PxErrorCode::eOUT_OF_MEMORY)
		| static_cast<uint32>(physx::PxErrorCode::eINTERNAL_ERROR) | static_cast<uint32>(physx::PxErrorCode::eABORT);
	// "A capacity was exceeded" (bench exit code 5, effective key errors.capacity_bits): PhysX grows its pools itself, so
	// only an allocation failure is a capacity error.
	inline constexpr uint32 GPhysXCapacityErrorBits = static_cast<uint32>(physx::PxErrorCode::eOUT_OF_MEMORY);

	struct FPhysXErrorCounts
	{
		uint32 Counts[GPhysXNumErrorCodes] = {};
	};

	class FPhysXErrorCallback final : public physx::PxErrorCallback
	{
	public:
		void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override;

		// The kit whose log callback receives the reports (set by InitGamePhys, cleared by TermGamePhys).
		void SetKit(::PhysicsTest::Abi::FKitServices* InKit) { Kit.store(InKit, std::memory_order_release); }
		FPhysXErrorCounts GetCounts() const;
		uint32 GetTotal() const;

	private:
		std::atomic<::PhysicsTest::Abi::FKitServices*> Kit{ nullptr };
		std::atomic<uint32> Counts[GPhysXNumErrorCodes] = {};
	};

	// The bits (PxErrorCode values, GPhysXErrorBitsMask) whose counts grew from Before to After.
	uint32 GetNewErrorBits(const FPhysXErrorCounts& Before, const FPhysXErrorCounts& After);
	// "eINVALID_OPERATION" etc.
	const char* GetErrorCodeName(uint32 CodeBit);
}
