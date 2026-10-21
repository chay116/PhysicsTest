// BenchRecorders.h - the M7 recorders of a bench run (02_Architecture 12.3, docs/ResultsSchema.md): hashes.csv, states.ptst +
// states.dtype.json, contacts.ptcn, plus the file helpers the runner shares. Every recorder sizes its buffers or opens its
// file BEFORE frame 0 and records between frames, outside the timed buckets (FPhysScene::Tick); a headline run (HashEvery =
// StatesEvery = CountersEvery = 0, no contacts tier) records nothing in the loop except the pre-sized timing row.
#pragma once

#include "CoreTypes.h"
#include "PhysicsTestAbi/PtBackendAbi.h"

#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace PhysicsTest
{
	class FPhysScene;

	namespace BenchRecorders
	{
		// Writes Text to Path (binary), flushed and closed. False when the file cannot be written.
		bool WriteText(const std::filesystem::path& Path, const std::string& Text);
		// The same through <Path>.tmp + MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH): readers never see a partial file.
		bool WriteTextAtomic(const std::filesystem::path& Path, const std::string& Text);

		// h_quantized (02 12.3): FNV-1a 64 over every body in creation order - its actor position rounded to 1e-4 cm and
		// its rotation normalised with w >= 0 and rounded to 1e-6, as little-endian int64 values. Never compared across
		// engines or builds (02 12.3); it absorbs last-bit noise within one engine and build.
		[[nodiscard]] uint64 ComputeQuantizedStateHash(const FPhysScene& Scene);
		// h_state (M7, harness addition): FNV-1a 64 over the exact bits of every body's pulled state (position, rotation,
		// linear and angular velocity as doubles, object state) in creation order - what states.ptst would hold, bit for
		// bit. G11 compares it per frame ("비트 단위로 같다").
		[[nodiscard]] uint64 ComputeExactStateHash(const FPhysScene& Scene);

		// hashes.csv: frame, h_native, h_quantized, h_state (hex; h_native empty without PT_CAP_NATIVE_HASH).
		struct FHashRow
		{
			uint32 Frame = 0;
			bool bHasNative = false;
			uint64 Native = 0;
			uint64 Quantized = 0;
			uint64 State = 0;
		};

		class FHashRecorder
		{
		public:
			void Reserve(size_t NumRows) { Rows.reserve(NumRows); }
			void Record(const FHashRow& Row) { Rows.push_back(Row); }
			bool WriteCsv(const std::filesystem::path& Path) const;
			[[nodiscard]] const std::vector<FHashRow>& GetRows() const { return Rows; }

		private:
			std::vector<FHashRow> Rows;
		};

		// states.ptst (PTST v1, little-endian, 02 12.3): a 64-byte header, then per recorded frame a 16-byte frame header
		// {u32 frame, u32 step, f64 sim_time} and body_count records of 112 bytes {f64 position[3], f64 rotation[4] (x, y,
		// z, w), f64 linear_velocity[3] (cm/s; dynamic: COM, kinematic: actor origin), f64 angular_velocity[3] (rad/s), u8
		// object_state, u8 pad[7]}. The file is written through a 4 MiB stdio buffer between frames; Close patches the frame
		// count into the header.
		class FStateRecorder
		{
		public:
			static constexpr uint32 HeaderSize = 64;
			static constexpr uint32 FrameHeaderSize = 16;
			static constexpr uint32 RecordStride = 112;

			~FStateRecorder();
			bool Open(const std::filesystem::path& Path, uint32 BodyCount, double DtEngine, const std::string& SceneSha256Hex, const std::string& DescSha256Hex);
			void Append(uint32 Frame, uint32 Step, double SimTime, const FPhysScene& Scene);
			bool Close();
			[[nodiscard]] uint32 GetFrameCount() const { return FrameCount; }
			// states.dtype.json: the layout above for numpy (np.dtype lists) and the header values.
			static bool WriteDtypeJson(const std::filesystem::path& Path, uint32 BodyCount, uint32 FrameCount, double DtEngine);

		private:
			std::FILE* File = nullptr;
			std::vector<char> Buffer;
			std::vector<uint8> Scratch;
			uint32 BodyCount = 0;
			uint32 FrameCount = 0;
			bool bFailed = false;
		};

		// contacts.ptcn (PTCN v1, little-endian, 02 12.3): a 32-byte header {char magic[4] "PTCN", u32 version 1, u32
		// record_size 128, u32 frame_count, u32 flags, u32 abi_provisional_revision, u32 point_pose, u32 reserved}, then per
		// recorded frame {u32 frame, u32 count} and count PtContactPoint records as the backend wrote them (128 bytes each,
		// docs/Abi.md 7.1 conventions). abi_provisional_revision is PT_ABI_PROVISIONAL_REVISION (PtContactPoint is a
		// PROVISIONAL(M9) struct: a reader rejects a file of another revision even at the same record size); point_pose is the
		// backend's effective key contact.point_pose (0 start-of-step, 1 predicted; UnknownPointPose when not declared). Both
		// fill reserved words of the M7 header (M8, M7 review): the layout and version 1 are unchanged.
		class FContactRecorder
		{
		public:
			static constexpr uint32 HeaderSize = 32;
			static constexpr uint32 UnknownPointPose = 0xFFFFFFFFu;

			~FContactRecorder();
			bool Open(const std::filesystem::path& Path, uint32 PointPose);
			void Append(uint32 Frame, std::span<const PtContactPoint> Contacts);
			bool Close();
			[[nodiscard]] uint32 GetFrameCount() const { return FrameCount; }
			[[nodiscard]] uint64 GetPointCount() const { return PointCount; }

		private:
			std::FILE* File = nullptr;
			std::vector<char> Buffer;
			uint32 FrameCount = 0;
			uint64 PointCount = 0;
			bool bFailed = false;
		};
	}
}
