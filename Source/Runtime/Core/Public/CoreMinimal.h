// CoreMinimal.h - the harness Core in one include (UE: Core/Public/CoreMinimal.h).
// Core is a minimal implementation of the UE idioms the harness uses (DL18, S5); names and semantics follow
// UE 5.8.3, the code is our own. As in UE, Containers/StringConv.h, HAL/IConsoleManager.h and Misc/ConfigCacheIni.h
// (M1) are included where they are used, not here. FCycleCounter arrives in M10.
#pragma once

#include "CoreTypes.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Hash/Fnv.h"
#include "Hash/Sha256.h"
#include "Logging/LogMacros.h"
#include "Math/Box.h"
#include "Math/Quat.h"
#include "Math/RandomStream.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/UnrealMathUtility.h"
#include "Math/Vector.h"
#include "Misc/AssertionMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
