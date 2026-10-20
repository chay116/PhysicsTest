# SDK smoke tests (03_WorkPlan M0 task 6, criteria 6, 7, 11): one console exe per SDK, native SI, Z-up, g = 9.80.
# They are SDK-side code like the backends: PT::BackendCommon + their own SDK, never harness Core (R4).
function(pt_add_sdk_smoke Name Source SdkTarget)
	pt_add_program(${Name} SOURCES ${Source} SmokeCommon.h DEPS PT::BackendCommon ${SdkTarget} FOLDER "PhysicsTest/Tests")
	# RunSmoke.cmake: exit code 0 AND a valid PT_SMOKE_JSON line with "pass": true.
	pt_add_script_test(PhysicsTest.Smoke.${ARGN}
		SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunSmoke.cmake"
		ARGS "-DPT_SMOKE_EXE=$<TARGET_FILE:${Name}>"
		LABELS "smoke")
endfunction()

if(PT_WITH_JOLT)
	pt_add_sdk_smoke(PtSmokeJolt JoltSmoke.cpp PT::Jolt Jolt)
endif()
if(PT_WITH_BULLET)
	pt_add_sdk_smoke(PtSmokeBullet BulletSmoke.cpp PT::Bullet Bullet)
endif()
if(PT_WITH_PHYSX)
	pt_add_sdk_smoke(PtSmokePhysX PhysXSmoke.cpp PT::PhysX PhysX)
endif()
