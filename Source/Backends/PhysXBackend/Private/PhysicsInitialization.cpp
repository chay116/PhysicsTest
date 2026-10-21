// PhysicsInitialization.cpp - InitGamePhys / TermGamePhys of the PhysX backend (see PhysicsInitialization.h).
#include "PhysicsInitialization.h"

#include "PhysXParse.h"

#include <stdio.h>

namespace PhysicsTest::PhysXBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		// UE4's GPhysXFoundation / GPhysXSDK. The allocator and the error callback outlive every foundation (static storage).
		FPhysXAllocator GPhysXAllocator;
		FPhysXErrorCallback GPhysXErrorCallback;
		physx::PxFoundation* GPhysXFoundation = nullptr;
		physx::PxPhysics* GPhysXSDK = nullptr;
		bool bExtensionsInitialized = false;
		uint32 GNumInitializations = 0;
	}

	PtResult ApplyInitOption(FPhysXInitOptions& Options, const Abi::FStringRef Key, const Abi::FStringRef Value, char (&Error)[256])
	{
		if (!EqualsNoCase("ToleranceScale", Key))
		{
			snprintf(Error, sizeof(Error), "PhysX: unknown module option '%.*s' (the only [PhysicsTest.Backend.PhysX.Init] key is ToleranceScale=(Length=..,Speed=..))",
				static_cast<int>(Key.Length), Key.Data);
			return PT_E_INVALID_ARG;
		}
		// (Length=100,Speed=1000): UE struct syntax, both fields required, values > 0.
		Abi::FStringRef Text = TrimSpaces(Value);
		if (Text.Length < 2 || Text.Data[0] != '(' || Text.Data[Text.Length - 1] != ')')
		{
			snprintf(Error, sizeof(Error), "PhysX: ToleranceScale must be (Length=<cm>,Speed=<cm/s>), got '%.*s'", static_cast<int>(Value.Length), Value.Data);
			return PT_E_INVALID_ARG;
		}
		Text.Data += 1;
		Text.Length -= 2;
		double Length = -1.0;
		double Speed = -1.0;
		while (Text.Length > 0)
		{
			uint32 Comma = 0;
			while (Comma < Text.Length && Text.Data[Comma] != ',')
			{
				++Comma;
			}
			const Abi::FStringRef Field{ Text.Data, Comma };
			uint32 Equals = 0;
			while (Equals < Field.Length && Field.Data[Equals] != '=')
			{
				++Equals;
			}
			const Abi::FStringRef Name = TrimSpaces(Abi::FStringRef{ Field.Data, Equals });
			const Abi::FStringRef Number = Equals < Field.Length ? TrimSpaces(Abi::FStringRef{ Field.Data + Equals + 1, Field.Length - Equals - 1 }) : Abi::FStringRef{ Field.Data, 0 };
			double Parsed = 0.0;
			if (!ParseDouble(Number, Parsed) || !(Parsed > 0.0) || Parsed > 1.0e9)
			{
				snprintf(Error, sizeof(Error), "PhysX: ToleranceScale field '%.*s' needs a number in (0, 1e9]", static_cast<int>(Field.Length), Field.Data);
				return PT_E_INVALID_ARG;
			}
			if (EqualsNoCase("Length", Name))
			{
				Length = Parsed;
			}
			else if (EqualsNoCase("Speed", Name))
			{
				Speed = Parsed;
			}
			else
			{
				snprintf(Error, sizeof(Error), "PhysX: ToleranceScale has no field '%.*s' (Length, Speed)", static_cast<int>(Name.Length), Name.Data);
				return PT_E_INVALID_ARG;
			}
			const uint32 Consumed = Comma < Text.Length ? Comma + 1 : Comma;
			Text.Data += Consumed;
			Text.Length -= Consumed;
		}
		if (Length < 0.0 || Speed < 0.0)
		{
			snprintf(Error, sizeof(Error), "PhysX: ToleranceScale needs both Length and Speed, got '%.*s'", static_cast<int>(Value.Length), Value.Data);
			return PT_E_INVALID_ARG;
		}
		Options.ToleranceLength = static_cast<float>(Length);
		Options.ToleranceSpeed = static_cast<float>(Speed);
		Options.bToleranceScaleGiven = true;
		return PT_OK;
	}

	PtResult InitGamePhys(const FPhysXInitOptions& Options, Abi::FKitServices& Kit, char (&Error)[256])
	{
		if (GPhysXSDK != nullptr || GPhysXFoundation != nullptr)
		{
			// The kit refuses a second InitGamePhys of this module; another PxFoundation in the process (a second PhysX
			// module) would be refused by PxCreateFoundation itself.
			snprintf(Error, sizeof(Error), "PhysX: InitGamePhys while a PxPhysics with tolerances (%g, %g) exists (one PxPhysics per process)",
				GPhysXSDK != nullptr ? static_cast<double>(GPhysXSDK->getTolerancesScale().length) : 0.0,
				GPhysXSDK != nullptr ? static_cast<double>(GPhysXSDK->getTolerancesScale().speed) : 0.0);
			return PT_E_BAD_STATE;
		}
		GPhysXErrorCallback.SetKit(&Kit);
		GPhysXFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GPhysXAllocator, GPhysXErrorCallback);
		if (GPhysXFoundation == nullptr)
		{
			GPhysXErrorCallback.SetKit(nullptr);
			snprintf(Error, sizeof(Error), "PhysX: PxCreateFoundation failed (another PxFoundation exists in this process?)");
			return PT_E_ENGINE;
		}
		const physx::PxTolerancesScale Scale(Options.ToleranceLength, Options.ToleranceSpeed);
		if (!Scale.isValid())
		{
			GPhysXFoundation->release();
			GPhysXFoundation = nullptr;
			GPhysXErrorCallback.SetKit(nullptr);
			snprintf(Error, sizeof(Error), "PhysX: PxTolerancesScale(%g, %g) is not valid", static_cast<double>(Options.ToleranceLength), static_cast<double>(Options.ToleranceSpeed));
			return PT_E_INVALID_ARG;
		}
		GPhysXSDK = PxCreatePhysics(PX_PHYSICS_VERSION, *GPhysXFoundation, Scale, false, nullptr);
		if (GPhysXSDK == nullptr)
		{
			GPhysXFoundation->release();
			GPhysXFoundation = nullptr;
			GPhysXErrorCallback.SetKit(nullptr);
			snprintf(Error, sizeof(Error), "PhysX: PxCreatePhysics failed");
			return PT_E_ENGINE;
		}
		bExtensionsInitialized = PxInitExtensions(*GPhysXSDK, nullptr);
		++GNumInitializations;
		Kit.LogToHost(PT_LOG_LOG, "LogPhysXBackend", "PhysX %d.%d.%d initialised: PxTolerancesScale(length %g, speed %g)%s", PX_PHYSICS_VERSION_MAJOR,
			PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX, static_cast<double>(Scale.length), static_cast<double>(Scale.speed),
			bExtensionsInitialized ? "" : ", PxInitExtensions failed");
		return PT_OK;
	}

	void TermGamePhys()
	{
		if (bExtensionsInitialized)
		{
			PxCloseExtensions();
			bExtensionsInitialized = false;
		}
		if (GPhysXSDK != nullptr)
		{
			GPhysXSDK->release();
			GPhysXSDK = nullptr;
		}
		if (GPhysXFoundation != nullptr)
		{
			GPhysXFoundation->release();
			GPhysXFoundation = nullptr;
		}
		GPhysXErrorCallback.SetKit(nullptr);
	}

	bool IsPhysXInitialized()
	{
		return GPhysXSDK != nullptr;
	}

	physx::PxPhysics& GetPhysXSDK()
	{
		return *GPhysXSDK;
	}

	physx::PxFoundation& GetPhysXFoundation()
	{
		return *GPhysXFoundation;
	}

	const FPhysXAllocator& GetPhysXAllocator()
	{
		return GPhysXAllocator;
	}

	const FPhysXErrorCallback& GetPhysXErrorCallback()
	{
		return GPhysXErrorCallback;
	}

	uint32 GetNumPhysXInitializations()
	{
		return GNumInitializations;
	}
}
