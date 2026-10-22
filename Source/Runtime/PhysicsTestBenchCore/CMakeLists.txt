# PT::BenchCore - measurement primitives (02_Architecture 4.5, A.12, DL53). M3: QPC clock buckets, window statistics, run
# ids, file hashes and read-only host facts (the power scheme is read with PowrProf, never changed). M7 adds the per-process
# host controls (affinity, priority, power throttling opt-out, timeBeginPeriod: winmm), the host load monitor, the process
# spawner and the allocation counters; M10 FThreadCensus and the TSC. Conformance -> BenchCore, Benchmark -> Conformance: no
# cycle.
pt_add_module(PtBenchCore STATIC ALIAS PT::BenchCore
	SOURCES
		Public/BenchCore.h
		Private/BenchCore.cpp
		Private/HostEnvironment.cpp
		Private/ThreadCensus.cpp
	PUBLIC_DEPS PT::Core
	PRIVATE_DEPS PowrProf Ole32 Winmm)
