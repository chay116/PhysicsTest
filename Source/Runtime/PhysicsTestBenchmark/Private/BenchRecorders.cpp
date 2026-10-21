// BenchRecorders.cpp - see BenchRecorders.h.
#include "BenchRecorders.h"

#include "Containers/StringConv.h"
#include "Hash/Fnv.h"
#include "PhysScene.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <Windows.h>

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <fstream>

namespace PhysicsTest::BenchRecorders
{
	namespace
	{
		void AppendBytes(std::vector<uint8>& Out, const void* Data, const size_t Size)
		{
			const auto* Bytes = static_cast<const uint8*>(Data);
			Out.insert(Out.end(), Bytes, Bytes + Size);
		}

		template <typename T>
		void AppendValue(std::vector<uint8>& Out, const T& Value)
		{
			AppendBytes(Out, &Value, sizeof(T));
		}

		// The first 8 bytes of a 64-character SHA-256 hex digest (0 bytes when it is not hex).
		void HexPrefix(const std::string& Hex, uint8 (&Out)[8])
		{
			std::memset(Out, 0, sizeof(Out));
			for (size_t Index = 0; Index < 8 && 2 * Index + 1 < Hex.size(); ++Index)
			{
				const auto Nibble = [](const char C) -> int
				{
					return C >= '0' && C <= '9' ? C - '0' : (C >= 'a' && C <= 'f' ? C - 'a' + 10 : (C >= 'A' && C <= 'F' ? C - 'A' + 10 : -1));
				};
				const int High = Nibble(Hex[2 * Index]);
				const int Low = Nibble(Hex[2 * Index + 1]);
				if (High < 0 || Low < 0)
				{
					std::memset(Out, 0, sizeof(Out));
					return;
				}
				Out[Index] = static_cast<uint8>((High << 4) | Low);
			}
		}

		uint64 FnvValue(const uint64 Hash, const void* Data, const size_t Size)
		{
			return FFnv::MemFnv64(Data, Size, Hash);
		}

		// One body's 112-byte record (states.ptst) from its pulled state.
		void WriteBodyRecord(const FBodyInstance& Body, uint8 (&Out)[FStateRecorder::RecordStride])
		{
			std::memset(Out, 0, sizeof(Out));
			const FBodyInstanceState& State = Body.State;
			const FVector& P = State.BodyTM.GetTranslation();
			const FQuat& Q = State.BodyTM.GetRotation();
			const double Values[13] = { P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W, State.LinearVelocity.X, State.LinearVelocity.Y, State.LinearVelocity.Z,
				State.AngularVelocityInRadians.X, State.AngularVelocityInRadians.Y, State.AngularVelocityInRadians.Z };
			std::memcpy(Out, Values, sizeof(Values));
			Out[104] = static_cast<uint8>(State.ObjectState);
		}
	}

	bool WriteText(const std::filesystem::path& Path, const std::string& Text)
	{
		std::ofstream File(Path, std::ios::binary | std::ios::trunc);
		File.write(Text.data(), static_cast<std::streamsize>(Text.size()));
		File.flush();
		return static_cast<bool>(File);
	}

	bool WriteTextAtomic(const std::filesystem::path& Path, const std::string& Text)
	{
		std::filesystem::path Temporary = Path;
		Temporary += L".tmp";
		{
			std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
			File.write(Text.data(), static_cast<std::streamsize>(Text.size()));
			File.flush();
			if (!File)
			{
				return false;
			}
		}
		return MoveFileExW(Temporary.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	}

	uint64 ComputeQuantizedStateHash(const FPhysScene& Scene)
	{
		uint64 Hash = FFnv::Offset64;
		for (const FBodyInstance* Body : Scene.GetBodies())
		{
			const FVector& P = Body->State.BodyTM.GetTranslation();
			FQuat Q = Body->State.BodyTM.GetRotation();
			const double Size = std::sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
			const double Sign = Q.W < 0.0 ? -1.0 : 1.0;
			const double Scale = Size > 0.0 ? Sign / Size : 0.0;
			const int64 Values[7] = { std::llround(P.X * 1.0e4), std::llround(P.Y * 1.0e4), std::llround(P.Z * 1.0e4), std::llround(Q.X * Scale * 1.0e6),
				std::llround(Q.Y * Scale * 1.0e6), std::llround(Q.Z * Scale * 1.0e6), std::llround(Q.W * Scale * 1.0e6) };
			Hash = FnvValue(Hash, Values, sizeof(Values));
		}
		return Hash;
	}

	uint64 ComputeExactStateHash(const FPhysScene& Scene)
	{
		uint64 Hash = FFnv::Offset64;
		uint8 Record[FStateRecorder::RecordStride];
		for (const FBodyInstance* Body : Scene.GetBodies())
		{
			WriteBodyRecord(*Body, Record);
			Hash = FnvValue(Hash, Record, 105);   // the 13 doubles and the object state; the padding is always 0
		}
		return Hash;
	}

	// ---- hashes.csv ------------------------------------------------------------------------------------------------------
	bool FHashRecorder::WriteCsv(const std::filesystem::path& Path) const
	{
		std::string Text = "frame,h_native,h_quantized,h_state\n";
		Text.reserve(Text.size() + Rows.size() * 64);
		char Line[128];
		for (const FHashRow& Row : Rows)
		{
			char Native[24] = "";
			if (Row.bHasNative)
			{
				std::snprintf(Native, sizeof(Native), "0x%016" PRIx64, Row.Native);
			}
			std::snprintf(Line, sizeof(Line), "%u,%s,0x%016" PRIx64 ",0x%016" PRIx64 "\n", Row.Frame, Native, Row.Quantized, Row.State);
			Text += Line;
		}
		return WriteText(Path, Text);
	}

	// ---- states.ptst -----------------------------------------------------------------------------------------------------
	FStateRecorder::~FStateRecorder()
	{
		if (File != nullptr)
		{
			std::fclose(File);
		}
	}

	bool FStateRecorder::Open(const std::filesystem::path& Path, const uint32 InBodyCount, const double DtEngine, const std::string& SceneSha256Hex,
		const std::string& DescSha256Hex)
	{
		BodyCount = InBodyCount;
		FrameCount = 0;
		_wfopen_s(&File, Path.c_str(), L"wb");
		if (File == nullptr)
		{
			bFailed = true;
			return false;
		}
		Buffer.resize(4u << 20);
		std::setvbuf(File, Buffer.data(), _IOFBF, Buffer.size());
		Scratch.reserve(FrameHeaderSize + static_cast<size_t>(BodyCount) * RecordStride);
		std::vector<uint8> Header;
		Header.reserve(HeaderSize);
		AppendBytes(Header, "PTST", 4);
		AppendValue(Header, uint32(1));                 // version
		AppendValue(Header, uint32(0));                 // flags (bit 0: frame_count final, set by Close)
		AppendValue(Header, BodyCount);
		AppendValue(Header, uint32(0));                 // frame_count (patched by Close)
		AppendValue(Header, RecordStride);
		AppendValue(Header, uint32(1));                 // units: 1 = cm, kg, s, rad
		AppendValue(Header, FrameHeaderSize);
		AppendValue(Header, DtEngine);
		uint8 Prefix[8];
		HexPrefix(SceneSha256Hex, Prefix);
		AppendBytes(Header, Prefix, 8);
		HexPrefix(DescSha256Hex, Prefix);
		AppendBytes(Header, Prefix, 8);
		AppendValue(Header, uint64(0));                 // reserved
		if (Header.size() != HeaderSize || std::fwrite(Header.data(), 1, Header.size(), File) != Header.size())
		{
			bFailed = true;
		}
		return !bFailed;
	}

	void FStateRecorder::Append(const uint32 Frame, const uint32 Step, const double SimTime, const FPhysScene& Scene)
	{
		if (File == nullptr || bFailed)
		{
			return;
		}
		Scratch.clear();
		AppendValue(Scratch, Frame);
		AppendValue(Scratch, Step);
		AppendValue(Scratch, SimTime);
		uint8 Record[RecordStride];
		uint32 Written = 0;
		for (const FBodyInstance* Body : Scene.GetBodies())
		{
			if (Written++ >= BodyCount)
			{
				break;
			}
			WriteBodyRecord(*Body, Record);
			AppendBytes(Scratch, Record, RecordStride);
		}
		if (std::fwrite(Scratch.data(), 1, Scratch.size(), File) != Scratch.size())
		{
			bFailed = true;
		}
		++FrameCount;
	}

	bool FStateRecorder::Close()
	{
		if (File == nullptr)
		{
			return !bFailed;
		}
		const uint32 Flags = 1u;
		bool bOk = !bFailed && std::fflush(File) == 0;
		bOk = bOk && std::fseek(File, 8, SEEK_SET) == 0 && std::fwrite(&Flags, sizeof(Flags), 1, File) == 1;
		bOk = bOk && std::fseek(File, 16, SEEK_SET) == 0 && std::fwrite(&FrameCount, sizeof(FrameCount), 1, File) == 1;
		bOk = std::fclose(File) == 0 && bOk;
		File = nullptr;
		return bOk;
	}

	bool FStateRecorder::WriteDtypeJson(const std::filesystem::path& Path, const uint32 InBodyCount, const uint32 InFrameCount, const double DtEngine)
	{
		using FJson = nlohmann::ordered_json;
		FJson Json;
		Json["schema"] = "physicstest.states_dtype/1";
		Json["file"] = "states.ptst";
		Json["byteorder"] = "little";
		Json["header_size"] = HeaderSize;
		Json["header"] = FJson::array({ FJson::array({ "magic", "S4" }), FJson::array({ "version", "<u4" }), FJson::array({ "flags", "<u4" }),
			FJson::array({ "body_count", "<u4" }), FJson::array({ "frame_count", "<u4" }), FJson::array({ "record_stride", "<u4" }),
			FJson::array({ "units", "<u4" }), FJson::array({ "frame_header_size", "<u4" }), FJson::array({ "dt", "<f8" }),
			FJson::array({ "scene_sha256_prefix", "V8" }), FJson::array({ "desc_sha256_prefix", "V8" }), FJson::array({ "reserved", "<u8" }) });
		Json["frame_header"] = FJson::array({ FJson::array({ "frame", "<u4" }), FJson::array({ "step", "<u4" }), FJson::array({ "sim_time", "<f8" }) });
		Json["body"] = FJson::array({ FJson::array({ "position", "<f8", FJson::array({ 3 }) }), FJson::array({ "rotation_xyzw", "<f8", FJson::array({ 4 }) }),
			FJson::array({ "linear_velocity", "<f8", FJson::array({ 3 }) }), FJson::array({ "angular_velocity", "<f8", FJson::array({ 3 }) }),
			FJson::array({ "object_state", "u1" }), FJson::array({ "pad", "V7" }) });
		Json["frame_header_size"] = FrameHeaderSize;
		Json["record_stride"] = RecordStride;
		Json["body_count"] = InBodyCount;
		Json["frame_count"] = InFrameCount;
		Json["dt_engine_s"] = DtEngine;
		Json["units"] = { { "length", "cm" }, { "velocity", "cm/s" }, { "angular_velocity", "rad/s" }, { "time", "s" } };
		Json["object_state"] = { { "1", "Sleeping" }, { "2", "Kinematic" }, { "3", "Static" }, { "4", "Dynamic" } };
		Json["frame_bytes"] = FrameHeaderSize + static_cast<uint64>(InBodyCount) * RecordStride;
		Json["notes"] = "frame block i starts at header_size + i * frame_bytes; linear_velocity is the COM velocity of a dynamic body and the actor "
			"origin's of a kinematic one (PtBodyState); bodies in creation order (body index)";
		return WriteText(Path, Json.dump(2) + "\n");
	}

	// ---- contacts.ptcn ---------------------------------------------------------------------------------------------------
	FContactRecorder::~FContactRecorder()
	{
		if (File != nullptr)
		{
			std::fclose(File);
		}
	}

	bool FContactRecorder::Open(const std::filesystem::path& Path)
	{
		FrameCount = 0;
		PointCount = 0;
		_wfopen_s(&File, Path.c_str(), L"wb");
		if (File == nullptr)
		{
			bFailed = true;
			return false;
		}
		Buffer.resize(4u << 20);
		std::setvbuf(File, Buffer.data(), _IOFBF, Buffer.size());
		std::vector<uint8> Header;
		AppendBytes(Header, "PTCN", 4);
		AppendValue(Header, uint32(1));                                     // version
		AppendValue(Header, static_cast<uint32>(sizeof(PtContactPoint)));   // record size 128
		AppendValue(Header, uint32(0));                                     // frame_count (patched by Close)
		AppendValue(Header, uint32(0));                                     // flags (bit 0: frame_count final)
		AppendValue(Header, uint32(0));
		AppendValue(Header, uint32(0));
		AppendValue(Header, uint32(0));
		if (Header.size() != HeaderSize || std::fwrite(Header.data(), 1, Header.size(), File) != Header.size())
		{
			bFailed = true;
		}
		return !bFailed;
	}

	void FContactRecorder::Append(const uint32 Frame, const std::span<const PtContactPoint> Contacts)
	{
		if (File == nullptr || bFailed)
		{
			return;
		}
		const uint32 Count = static_cast<uint32>(Contacts.size());
		bool bOk = std::fwrite(&Frame, sizeof(Frame), 1, File) == 1 && std::fwrite(&Count, sizeof(Count), 1, File) == 1;
		if (Count > 0)
		{
			bOk = bOk && std::fwrite(Contacts.data(), sizeof(PtContactPoint), Contacts.size(), File) == Contacts.size();
		}
		bFailed = !bOk;
		++FrameCount;
		PointCount += Count;
	}

	bool FContactRecorder::Close()
	{
		if (File == nullptr)
		{
			return !bFailed;
		}
		const uint32 Flags = 1u;
		bool bOk = !bFailed && std::fflush(File) == 0;
		bOk = bOk && std::fseek(File, 12, SEEK_SET) == 0 && std::fwrite(&FrameCount, sizeof(FrameCount), 1, File) == 1;
		bOk = bOk && std::fseek(File, 16, SEEK_SET) == 0 && std::fwrite(&Flags, sizeof(Flags), 1, File) == 1;
		bOk = std::fclose(File) == 0 && bOk;
		File = nullptr;
		return bOk;
	}
}
