////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2017 Nicholas Frechette & Animation Compression Library contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

#include "acl_compressor.h"


// Enable 64 bit file IO
#ifndef _WIN32
	#define _FILE_OFFSET_BITS 64
#endif

// Used to debug and validate that we compile without sjson-cpp
// Defaults to being enabled
#if defined(ACL_USE_SJSON)
	#include <sjson/writer.h>
	#include <sjson/parser.h>
#else
	namespace sjson { class ArrayWriter; }
#endif

#include "acl/core/ansi_allocator.h"
#include "acl/core/floating_point_exceptions.h"
#include "acl/core/iallocator.h"
#include "acl/core/range_reduction_types.h"
#include "acl/core/string.h"
#include "acl/core/impl/debug_track_writer.h"
#include "acl/compression/animation_clip.h"
#include "acl/compression/compress.h"
#include "acl/compression/skeleton.h"
#include "acl/compression/skeleton_error_metric.h"
#include "acl/compression/stream/write_decompression_stats.h"
#include "acl/compression/track_error.h"
#include "acl/compression/utils.h"
#include "acl/decompression/decompress.h"
#include "acl/io/clip_reader.h"

#include "acl/algorithm/uniformly_sampled/encoder.h"
#include "acl/algorithm/uniformly_sampled/decoder.h"

#include <cstring>
#include <cstdio>
#include <fstream>
#include <streambuf>
#include <sstream>
#include <string>
#include <memory>

#if defined(_WIN32)
	// The below excludes some other unused services from the windows headers -- see windows.h for details.
	#define NOGDICAPMASKS            // CC_*, LC_*, PC_*, CP_*, TC_*, RC_
	#define NOVIRTUALKEYCODES        // VK_*
	#define NOWINMESSAGES            // WM_*, EM_*, LB_*, CB_*
	#define NOWINSTYLES                // WS_*, CS_*, ES_*, LBS_*, SBS_*, CBS_*
	#define NOSYSMETRICS            // SM_*
	#define NOMENUS                    // MF_*
	#define NOICONS                    // IDI_*
	#define NOKEYSTATES                // MK_*
	#define NOSYSCOMMANDS            // SC_*
	#define NORASTEROPS                // Binary and Tertiary raster ops
	#define NOSHOWWINDOW            // SW_*
	#define OEMRESOURCE                // OEM Resource values
	#define NOATOM                    // Atom Manager routines
	#define NOCLIPBOARD                // Clipboard routines
	#define NOCOLOR                    // Screen colors
	#define NOCTLMGR                // Control and Dialog routines
	#define NODRAWTEXT                // DrawText() and DT_*
	#define NOGDI                    // All GDI #defines and routines
	#define NOKERNEL                // All KERNEL #defines and routines
	#define NOUSER                    // All USER #defines and routines
	#define NONLS                    // All NLS #defines and routines
	#define NOMB                    // MB_* and MessageBox()
	#define NOMEMMGR                // GMEM_*, LMEM_*, GHND, LHND, associated routines
	#define NOMETAFILE                // typedef METAFILEPICT
	#define NOMINMAX                // Macros min(a,b) and max(a,b)
	#define NOMSG                    // typedef MSG and associated routines
	#define NOOPENFILE                // OpenFile(), OemToAnsi, AnsiToOem, and OF_*
	#define NOSCROLL                // SB_* and scrolling routines
	#define NOSERVICE                // All Service Controller routines, SERVICE_ equates, etc.
	#define NOSOUND                    // Sound driver routines
	#define NOTEXTMETRIC            // typedef TEXTMETRIC and associated routines
	#define NOWH                    // SetWindowsHook and WH_*
	#define NOWINOFFSETS            // GWL_*, GCL_*, associated routines
	#define NOCOMM                    // COMM driver routines
	#define NOKANJI                    // Kanji support stuff.
	#define NOHELP                    // Help engine interface.
	#define NOPROFILER                // Profiler interface.
	#define NODEFERWINDOWPOS        // DeferWindowPos routines
	#define NOMCX                    // Modem Configuration Extensions
	#define NOCRYPT
	#define NOTAPE
	#define NOIMAGE
	#define NOPROXYSTUB
	#define NORPC

	#include <windows.h>
	#include <conio.h>

#endif    // _WIN32

using namespace acl;

struct Options
{
#if defined(__ANDROID__)
	const char*		input_buffer;
	size_t			input_buffer_size;
	bool			input_buffer_binary;
	const char*		config_buffer;
	size_t			config_buffer_size;
#else
	const char*		input_filename;
	const char*		config_filename;
#endif

	bool			output_stats;
	const char*		output_stats_filename;
	std::FILE*		output_stats_file;

	const char*		output_bin_filename;

	CompressionLevel8	compression_level;
	bool			compression_level_specified;

	bool			regression_testing;
	bool			profile_decompression;
	bool			exhaustive_compression;

	bool			is_bind_pose_relative;
	bool			is_bind_pose_additive0;
	bool			is_bind_pose_additive1;

	bool			stat_detailed_output;
	bool			stat_exhaustive_output;

	//////////////////////////////////////////////////////////////////////////

	Options()
#if defined(__ANDROID__)
		: input_buffer(nullptr)
		, input_buffer_size(0)
		, input_buffer_binary(false)
		, config_buffer(nullptr)
		, config_buffer_size(0)
#else
		: input_filename(nullptr)
		, config_filename(nullptr)
#endif
		, output_stats(false)
		, output_stats_filename(nullptr)
		, output_stats_file(nullptr)
		, output_bin_filename(nullptr)
		, compression_level(CompressionLevel8::Lowest)
		, compression_level_specified(false)
		, regression_testing(false)
		, profile_decompression(false)
		, exhaustive_compression(false)
		, is_bind_pose_relative(false)
		, is_bind_pose_additive0(false)
		, is_bind_pose_additive1(false)
		, stat_detailed_output(false)
		, stat_exhaustive_output(false)
	{}

	~Options()
	{
		if (output_stats_file != nullptr && output_stats_file != stdout)
			std::fclose(output_stats_file);
	}

	Options(Options&& other) = default;
	Options(const Options&) = delete;
	Options& operator=(Options&& other) = default;
	Options& operator=(const Options&) = delete;

	void open_output_stats_file()
	{
		std::FILE* file = nullptr;
		if (output_stats_filename != nullptr)
		{
#ifdef _WIN32
			char path[64 * 1024] = { 0 };
			snprintf(path, get_array_size(path), "\\\\?\\%s", output_stats_filename);
			fopen_s(&file, path, "w");
#else
			file = fopen(output_stats_filename, "w");
#endif
			ACL_ASSERT(file != nullptr, "Failed to open output stats file: ", output_stats_filename);
		}

		output_stats_file = file != nullptr ? file : stdout;
	}
};

static constexpr const char* k_acl_input_file_option = "-acl=";
static constexpr const char* k_config_input_file_option = "-config=";
static constexpr const char* k_stats_output_option = "-stats";
static constexpr const char* k_bin_output_option = "-out=";
static constexpr const char* k_compression_level_option = "-level=";
static constexpr const char* k_regression_test_option = "-test";
static constexpr const char* k_profile_decompression_option = "-decomp";
static constexpr const char* k_exhaustive_compression_option = "-exhaustive";
static constexpr const char* k_bind_pose_relative_option = "-bind_rel";
static constexpr const char* k_bind_pose_additive0_option = "-bind_add0";
static constexpr const char* k_bind_pose_additive1_option = "-bind_add1";
static constexpr const char* k_stat_detailed_output_option = "-stat_detailed";
static constexpr const char* k_stat_exhaustive_output_option = "-stat_exhaustive";

bool is_acl_sjson_file(const char* filename)
{
	const size_t filename_len = std::strlen(filename);
	return filename_len >= 10 && strncmp(filename + filename_len - 10, ".acl.sjson", 10) == 0;
}

bool is_acl_bin_file(const char* filename)
{
	const size_t filename_len = std::strlen(filename);
	return filename_len >= 8 && strncmp(filename + filename_len - 8, ".acl.bin", 8) == 0;
}

static bool parse_options(int argc, char** argv, Options& options)
{
	for (int arg_index = 1; arg_index < argc; ++arg_index)
	{
		const char* argument = argv[arg_index];

		size_t option_length = std::strlen(k_acl_input_file_option);
		if (std::strncmp(argument, k_acl_input_file_option, option_length) == 0)
		{
#if defined(__ANDROID__)
			unsigned int buffer_size;
			int is_acl_bin_buffer;
			sscanf(argument + option_length, "@%u,%p,%d", &buffer_size, &options.input_buffer, &is_acl_bin_buffer);
			options.input_buffer_size = buffer_size;
			options.input_buffer_binary = is_acl_bin_buffer != 0;
#else
			options.input_filename = argument + option_length;
			if (!is_acl_sjson_file(options.input_filename) && !is_acl_bin_file(options.input_filename))
			{
				printf("Input file must be an ACL SJSON file of the form: [*.acl.sjson] or a binary ACL file of the form: [*.acl.bin]\n");
				return false;
			}
#endif
			continue;
		}

		option_length = std::strlen(k_config_input_file_option);
		if (std::strncmp(argument, k_config_input_file_option, option_length) == 0)
		{
#if defined(__ANDROID__)
			unsigned int buffer_size;
			sscanf(argument + option_length, "@%u,%p", &buffer_size, &options.config_buffer);
			options.config_buffer_size = buffer_size;
#else
			options.config_filename = argument + option_length;
			const size_t filename_len = std::strlen(options.config_filename);
			if (filename_len < 13 || strncmp(options.config_filename + filename_len - 13, ".config.sjson", 13) != 0)
			{
				printf("Configuration file must be a config SJSON file of the form: [*.config.sjson]\n");
				return false;
			}
#endif
			continue;
		}

		option_length = std::strlen(k_stats_output_option);
		if (std::strncmp(argument, k_stats_output_option, option_length) == 0)
		{
			options.output_stats = true;
			if (argument[option_length] == '=')
			{
				options.output_stats_filename = argument + option_length + 1;
				const size_t filename_len = std::strlen(options.output_stats_filename);
				if (filename_len < 6 || strncmp(options.output_stats_filename + filename_len - 6, ".sjson", 6) != 0)
				{
					printf("Stats output file must be an SJSON file of the form: [*.sjson]\n");
					return false;
				}
			}
			else
				options.output_stats_filename = nullptr;

			options.open_output_stats_file();
			continue;
		}

		option_length = std::strlen(k_bin_output_option);
		if (std::strncmp(argument, k_bin_output_option, option_length) == 0)
		{
			options.output_bin_filename = argument + option_length;
			const size_t filename_len = std::strlen(options.output_bin_filename);
			if (filename_len < 8 || strncmp(options.output_bin_filename + filename_len - 8, ".acl.bin", 8) != 0)
			{
				printf("Binary output file must be an ACL binary file of the form: [*.acl.bin]\n");
				return false;
			}
			continue;
		}

		option_length = std::strlen(k_compression_level_option);
		if (std::strncmp(argument, k_compression_level_option, option_length) == 0)
		{
			const char* level_name = argument + option_length;
			if (!get_compression_level(level_name, options.compression_level))
			{
				printf("Invalid compression level name specified: %s\n", level_name);
				return false;
			}
			options.compression_level_specified = true;
			continue;
		}

		option_length = std::strlen(k_regression_test_option);
		if (std::strncmp(argument, k_regression_test_option, option_length) == 0)
		{
			options.regression_testing = true;
			continue;
		}

		option_length = std::strlen(k_profile_decompression_option);
		if (std::strncmp(argument, k_profile_decompression_option, option_length) == 0)
		{
			options.profile_decompression = true;
			continue;
		}

		option_length = std::strlen(k_exhaustive_compression_option);
		if (std::strncmp(argument, k_exhaustive_compression_option, option_length) == 0)
		{
			options.exhaustive_compression = true;
			continue;
		}

		option_length = std::strlen(k_bind_pose_relative_option);
		if (std::strncmp(argument, k_bind_pose_relative_option, option_length) == 0)
		{
			options.is_bind_pose_relative = true;
			continue;
		}

		option_length = std::strlen(k_bind_pose_additive0_option);
		if (std::strncmp(argument, k_bind_pose_additive0_option, option_length) == 0)
		{
			options.is_bind_pose_additive0 = true;
			continue;
		}

		option_length = std::strlen(k_bind_pose_additive1_option);
		if (std::strncmp(argument, k_bind_pose_additive1_option, option_length) == 0)
		{
			options.is_bind_pose_additive1 = true;
			continue;
		}

		option_length = std::strlen(k_stat_detailed_output_option);
		if (std::strncmp(argument, k_stat_detailed_output_option, option_length) == 0)
		{
			options.stat_detailed_output = true;
			continue;
		}

		option_length = std::strlen(k_stat_exhaustive_output_option);
		if (std::strncmp(argument, k_stat_exhaustive_output_option, option_length) == 0)
		{
			options.stat_exhaustive_output = true;
			continue;
		}

		printf("Unrecognized option %s\n", argument);
		return false;
	}

#if defined(__ANDROID__)
	if (options.input_buffer == nullptr || options.input_buffer_size == 0)
#else
	if (options.input_filename == nullptr || std::strlen(options.input_filename) == 0)
#endif
	{
		printf("An input file is required.\n");
		return false;
	}

	if (options.profile_decompression && options.exhaustive_compression)
	{
		printf("Exhaustive compression is not supported with decompression profiling.\n");
		return false;
	}

	return true;
}

#if defined(ACL_USE_SJSON)
template<class DecompressionContextType>
static void validate_accuracy(IAllocator& allocator, const AnimationClip& clip, const CompressionSettings& settings, DecompressionContextType& context, double regression_error_threshold)
{
	(void)regression_error_threshold;

	const uint16_t num_bones = clip.get_num_bones();
	const float clip_duration = clip.get_duration();
	const float sample_rate = clip.get_sample_rate();
	const uint32_t num_samples = calculate_num_samples(clip_duration, clip.get_sample_rate());
	const ISkeletalErrorMetric& error_metric = *settings.error_metric;
	const RigidSkeleton& skeleton = clip.get_skeleton();

	const AnimationClip* additive_base_clip = clip.get_additive_base();
	const uint32_t additive_num_samples = additive_base_clip != nullptr ? additive_base_clip->get_num_samples() : 0;
	const float additive_duration = additive_base_clip != nullptr ? additive_base_clip->get_duration() : 0.0F;

	Transform_32* raw_pose_transforms = allocate_type_array<Transform_32>(allocator, num_bones);
	Transform_32* base_pose_transforms = allocate_type_array<Transform_32>(allocator, num_bones);
	Transform_32* lossy_pose_transforms = allocate_type_array<Transform_32>(allocator, num_bones);

	DefaultOutputWriter pose_writer(lossy_pose_transforms, num_bones);

	// Regression test
	for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
	{
		const float sample_time = min(float(sample_index) / sample_rate, clip_duration);

		// We use the nearest sample to accurately measure the loss that happened, if any
		clip.sample_pose(sample_time, SampleRoundingPolicy::Nearest, raw_pose_transforms, num_bones);

		context.seek(sample_time, SampleRoundingPolicy::Nearest);
		context.decompress_pose(pose_writer);

		if (additive_base_clip != nullptr)
		{
			const float normalized_sample_time = additive_num_samples > 1 ? (sample_time / clip_duration) : 0.0F;
			const float additive_sample_time = additive_num_samples > 1 ? (normalized_sample_time * additive_duration) : 0.0F;
			additive_base_clip->sample_pose(additive_sample_time, SampleRoundingPolicy::Nearest, base_pose_transforms, num_bones);
		}

		// Validate decompress_pose
		for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
		{
			const float error = error_metric.calculate_object_bone_error(skeleton, raw_pose_transforms, base_pose_transforms, lossy_pose_transforms, bone_index);
			(void)error;
			ACL_ASSERT(is_finite(error), "Returned error is not a finite value");
			ACL_ASSERT(error < regression_error_threshold, "Error too high for bone %u: %f at time %f", bone_index, error, sample_time);
		}

		// Validate decompress_bone for rotations only
		for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
		{
			Quat_32 rotation;
			context.decompress_bone(bone_index, &rotation, nullptr, nullptr);
			ACL_ASSERT(quat_near_equal(rotation, lossy_pose_transforms[bone_index].rotation), "Failed to sample bone index: %u", bone_index);
		}

		// Validate decompress_bone for translations only
		for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
		{
			Vector4_32 translation;
			context.decompress_bone(bone_index, nullptr, &translation, nullptr);
			ACL_ASSERT(vector_all_near_equal3(translation, lossy_pose_transforms[bone_index].translation), "Failed to sample bone index: %u", bone_index);
		}

		// Validate decompress_bone for scales only
		for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
		{
			Vector4_32 scale;
			context.decompress_bone(bone_index, nullptr, nullptr, &scale);
			ACL_ASSERT(vector_all_near_equal3(scale, lossy_pose_transforms[bone_index].scale), "Failed to sample bone index: %u", bone_index);
		}

		// Validate decompress_bone
		for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
		{
			Quat_32 rotation;
			Vector4_32 translation;
			Vector4_32 scale;
			context.decompress_bone(bone_index, &rotation, &translation, &scale);
			ACL_ASSERT(quat_near_equal(rotation, lossy_pose_transforms[bone_index].rotation), "Failed to sample bone index: %u", bone_index);
			ACL_ASSERT(vector_all_near_equal3(translation, lossy_pose_transforms[bone_index].translation), "Failed to sample bone index: %u", bone_index);
			ACL_ASSERT(vector_all_near_equal3(scale, lossy_pose_transforms[bone_index].scale), "Failed to sample bone index: %u", bone_index);
		}
	}

	deallocate_type_array(allocator, raw_pose_transforms, num_bones);
	deallocate_type_array(allocator, base_pose_transforms, num_bones);
	deallocate_type_array(allocator, lossy_pose_transforms, num_bones);
}

static void validate_accuracy(IAllocator& allocator, const track_array& raw_tracks, const compressed_tracks& tracks, double regression_error_threshold)
{
	(void)allocator;
	(void)raw_tracks;
	(void)tracks;
	(void)regression_error_threshold;

#if defined(ACL_HAS_ASSERT_CHECKS)
	using namespace acl_impl;

	const float regression_error_thresholdf = static_cast<float>(regression_error_threshold);
	const rtm::vector4f regression_error_thresholdv = rtm::vector_set(regression_error_thresholdf);
	(void)regression_error_thresholdf;
	(void)regression_error_thresholdv;

	const float duration = tracks.get_duration();
	const float sample_rate = tracks.get_sample_rate();
	const uint32_t num_tracks = tracks.get_num_tracks();
	const uint32_t num_samples = tracks.get_num_samples_per_track();
	const track_type8 track_type = raw_tracks.get_track_type();

	ACL_ASSERT(scalar_near_equal(duration, raw_tracks.get_duration(), 1.0E-7F), "Duration mismatch");
	ACL_ASSERT(sample_rate == raw_tracks.get_sample_rate(), "Sample rate mismatch");
	ACL_ASSERT(num_tracks <= raw_tracks.get_num_tracks(), "Num tracks mismatch");
	ACL_ASSERT(num_samples == raw_tracks.get_num_samples_per_track(), "Num samples mismatch");

	decompression_context<debug_decompression_settings> context;
	context.initialize(tracks);

	debug_track_writer raw_tracks_writer(allocator, track_type, num_tracks);
	debug_track_writer raw_track_writer(allocator, track_type, num_tracks);
	debug_track_writer lossy_tracks_writer(allocator, track_type, num_tracks);
	debug_track_writer lossy_track_writer(allocator, track_type, num_tracks);

	const rtm::vector4f zero = rtm::vector_zero();

	// Regression test
	for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
	{
		const float sample_time = min(float(sample_index) / sample_rate, duration);

		// We use the nearest sample to accurately measure the loss that happened, if any
		raw_tracks.sample_tracks(sample_time, SampleRoundingPolicy::Nearest, raw_tracks_writer);

		context.seek(sample_time, SampleRoundingPolicy::Nearest);
		context.decompress_tracks(lossy_tracks_writer);

		// Validate decompress_tracks
		for (uint32_t track_index = 0; track_index < num_tracks; ++track_index)
		{
			const track& track_ = raw_tracks[track_index];
			const uint32_t output_index = track_.get_output_index();
			if (output_index == k_invalid_track_index)
				continue;	// Track is being stripped, ignore it

			rtm::vector4f error = zero;

			switch (track_type)
			{
			case track_type8::float1f:
			{
				const float raw_value = raw_tracks_writer.read_float1(track_index);
				const float lossy_value = lossy_tracks_writer.read_float1(output_index);
				error = rtm::vector_set(rtm::scalar_abs(raw_value - lossy_value));
				break;
			}
			case track_type8::float2f:
			{
				const rtm::vector4f raw_value = raw_tracks_writer.read_float2(track_index);
				const rtm::vector4f lossy_value = lossy_tracks_writer.read_float2(output_index);
				error = rtm::vector_abs(rtm::vector_sub(raw_value, lossy_value));
				error = rtm::vector_mix<rtm::mix4::x, rtm::mix4::y, rtm::mix4::c, rtm::mix4::d>(error, zero);
				break;
			}
			case track_type8::float3f:
			{
				const rtm::vector4f raw_value = raw_tracks_writer.read_float3(track_index);
				const rtm::vector4f lossy_value = lossy_tracks_writer.read_float3(output_index);
				error = rtm::vector_abs(rtm::vector_sub(raw_value, lossy_value));
				error = rtm::vector_mix<rtm::mix4::x, rtm::mix4::y, rtm::mix4::z, rtm::mix4::d>(error, zero);
				break;
			}
			case track_type8::float4f:
			{
				const rtm::vector4f raw_value = raw_tracks_writer.read_float4(track_index);
				const rtm::vector4f lossy_value = lossy_tracks_writer.read_float4(output_index);
				error = rtm::vector_abs(rtm::vector_sub(raw_value, lossy_value));
				break;
			}
			case track_type8::vector4f:
			{
				const rtm::vector4f raw_value = raw_tracks_writer.read_vector4(track_index);
				const rtm::vector4f lossy_value = lossy_tracks_writer.read_vector4(output_index);
				error = rtm::vector_abs(rtm::vector_sub(raw_value, lossy_value));
				break;
			}
			default:
				ACL_ASSERT(false, "Unsupported track type");
				break;
			}

			(void)error;
			ACL_ASSERT(rtm::vector_is_finite(error), "Returned error is not a finite value");
			ACL_ASSERT(rtm::vector_all_less_than(error, regression_error_thresholdv), "Error too high for track %u at time %f", track_index, sample_time);
		}

		// Validate decompress_track
		for (uint32_t track_index = 0; track_index < num_tracks; ++track_index)
		{
			const track& track_ = raw_tracks[track_index];
			const uint32_t output_index = track_.get_output_index();
			if (output_index == k_invalid_track_index)
				continue;	// Track is being stripped, ignore it

			// We use the nearest sample to accurately measure the loss that happened, if any
			raw_tracks.sample_track(track_index, sample_time, SampleRoundingPolicy::Nearest, raw_track_writer);
			context.decompress_track(output_index, lossy_track_writer);

			switch (track_type)
			{
			case track_type8::float1f:
			{
				const float raw_value_ = raw_tracks_writer.read_float1(track_index);
				const float lossy_value_ = lossy_tracks_writer.read_float1(output_index);
				const float raw_value = raw_track_writer.read_float1(track_index);
				const float lossy_value = lossy_track_writer.read_float1(output_index);
				ACL_ASSERT(rtm::scalar_near_equal(raw_value, lossy_value, regression_error_thresholdf), "Error too high for track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::scalar_near_equal(raw_value_, raw_value), "Failed to sample track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::scalar_near_equal(lossy_value_, lossy_value), "Failed to sample track %u at time %f", track_index, sample_time);
				break;
			}
			case track_type8::float2f:
			{
				const rtm::vector4f raw_value_ = raw_tracks_writer.read_float2(track_index);
				const rtm::vector4f lossy_value_ = lossy_tracks_writer.read_float2(output_index);
				const rtm::vector4f raw_value = raw_track_writer.read_float2(track_index);
				const rtm::vector4f lossy_value = lossy_track_writer.read_float2(output_index);
				ACL_ASSERT(rtm::vector_all_near_equal2(raw_value, lossy_value, regression_error_thresholdf), "Error too high for track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::vector_all_near_equal2(raw_value_, raw_value), "Failed to sample track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::vector_all_near_equal2(lossy_value_, lossy_value), "Failed to sample track %u at time %f", track_index, sample_time);
				break;
			}
			case track_type8::float3f:
			{
				const rtm::vector4f raw_value_ = raw_tracks_writer.read_float3(track_index);
				const rtm::vector4f lossy_value_ = lossy_tracks_writer.read_float3(output_index);
				const rtm::vector4f raw_value = raw_track_writer.read_float3(track_index);
				const rtm::vector4f lossy_value = lossy_track_writer.read_float3(output_index);
				ACL_ASSERT(rtm::vector_all_near_equal3(raw_value, lossy_value, regression_error_thresholdf), "Error too high for track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::vector_all_near_equal3(raw_value_, raw_value), "Failed to sample track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::vector_all_near_equal3(lossy_value_, lossy_value), "Failed to sample track %u at time %f", track_index, sample_time);
				break;
			}
			case track_type8::float4f:
			{
				const rtm::vector4f raw_value_ = raw_tracks_writer.read_float4(track_index);
				const rtm::vector4f lossy_value_ = lossy_tracks_writer.read_float4(output_index);
				const rtm::vector4f raw_value = raw_track_writer.read_float4(track_index);
				const rtm::vector4f lossy_value = lossy_track_writer.read_float4(output_index);
				ACL_ASSERT(rtm::vector_all_near_equal(raw_value, lossy_value, regression_error_thresholdf), "Error too high for track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::vector_all_near_equal(raw_value_, raw_value), "Failed to sample track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::vector_all_near_equal(lossy_value_, lossy_value), "Failed to sample track %u at time %f", track_index, sample_time);
				break;
			}
			case track_type8::vector4f:
			{
				const rtm::vector4f raw_value_ = raw_tracks_writer.read_vector4(track_index);
				const rtm::vector4f lossy_value_ = lossy_tracks_writer.read_vector4(output_index);
				const rtm::vector4f raw_value = raw_track_writer.read_vector4(track_index);
				const rtm::vector4f lossy_value = lossy_track_writer.read_vector4(output_index);
				ACL_ASSERT(rtm::vector_all_near_equal(raw_value, lossy_value, regression_error_thresholdf), "Error too high for track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::vector_all_near_equal(raw_value_, raw_value), "Failed to sample track %u at time %f", track_index, sample_time);
				ACL_ASSERT(rtm::vector_all_near_equal(lossy_value_, lossy_value), "Failed to sample track %u at time %f", track_index, sample_time);
				break;
			}
			default:
				ACL_ASSERT(false, "Unsupported track type");
				break;
			}
		}
	}
#endif	// defined(ACL_HAS_ASSERT_CHECKS)
}

static void try_algorithm(const Options& options, IAllocator& allocator, const AnimationClip& clip, const CompressionSettings& settings, AlgorithmType8 algorithm_type, StatLogging logging, sjson::ArrayWriter* runs_writer, double regression_error_threshold)
{
	(void)runs_writer;

	auto try_algorithm_impl = [&](sjson::ObjectWriter* stats_writer)
	{
		if (clip.get_num_samples() == 0)
			return;

		OutputStats stats(logging, stats_writer);
		CompressedClip* compressed_clip = nullptr;
		ErrorResult error_result; (void)error_result;
		switch (algorithm_type)
		{
		case AlgorithmType8::UniformlySampled:
			error_result = uniformly_sampled::compress_clip(allocator, clip, settings, compressed_clip, stats);
			break;
		}

		ACL_ASSERT(error_result.empty(), error_result.c_str());
		ACL_ASSERT(compressed_clip->is_valid(true).empty(), "Compressed clip is invalid");

#if defined(SJSON_CPP_WRITER)
		if (logging != StatLogging::None)
		{
			// Disable floating point exceptions since decompression assumes it
			scope_disable_fp_exceptions fp_off;

			// Use the compressed clip to make sure the decoder works properly
			BoneError bone_error;
			switch (algorithm_type)
			{
			case AlgorithmType8::UniformlySampled:
			{
				uniformly_sampled::DecompressionContext<uniformly_sampled::DebugDecompressionSettings> context;
				context.initialize(*compressed_clip);
				bone_error = calculate_compressed_clip_error(allocator, clip, *settings.error_metric, context);
				break;
			}
			}

			stats_writer->insert("max_error", bone_error.error);
			stats_writer->insert("worst_bone", bone_error.index);
			stats_writer->insert("worst_time", bone_error.sample_time);

			if (are_any_enum_flags_set(logging, StatLogging::SummaryDecompression))
				write_decompression_performance_stats(allocator, settings, *compressed_clip, logging, *stats_writer);
		}
#endif

		if (options.regression_testing)
		{
			// Disable floating point exceptions since decompression assumes it
			scope_disable_fp_exceptions fp_off;

			switch (algorithm_type)
			{
			case AlgorithmType8::UniformlySampled:
			{
				uniformly_sampled::DecompressionContext<uniformly_sampled::DebugDecompressionSettings> context;
				context.initialize(*compressed_clip);
				validate_accuracy(allocator, clip, settings, context, regression_error_threshold);
				break;
			}
			}
		}

		if (options.output_bin_filename != nullptr)
		{
			std::ofstream output_file_stream(options.output_bin_filename, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
			if (output_file_stream.is_open())
				output_file_stream.write(reinterpret_cast<const char*>(compressed_clip), compressed_clip->get_size());
		}

		allocator.deallocate(compressed_clip, compressed_clip->get_size());
	};

#if defined(SJSON_CPP_WRITER)
	if (runs_writer != nullptr)
		runs_writer->push([&](sjson::ObjectWriter& writer) { try_algorithm_impl(&writer); });
	else
#endif
		try_algorithm_impl(nullptr);
}

static void try_algorithm(const Options& options, IAllocator& allocator, const track_array& track_list, StatLogging logging, sjson::ArrayWriter* runs_writer, double regression_error_threshold)
{
	(void)runs_writer;

	auto try_algorithm_impl = [&](sjson::ObjectWriter* stats_writer)
	{
		if (track_list.get_num_tracks() == 0)
			return;

		compression_settings settings;

		OutputStats stats(logging, stats_writer);
		compressed_tracks* compressed_tracks_ = nullptr;
		const ErrorResult error_result = compress_track_list(allocator, track_list, settings, compressed_tracks_, stats);

		ACL_ASSERT(error_result.empty(), error_result.c_str()); (void)error_result;
		ACL_ASSERT(compressed_tracks_->is_valid(true).empty(), "Compressed tracks are invalid");

#if defined(SJSON_CPP_WRITER)
		if (logging != StatLogging::None)
		{
			// Disable floating point exceptions since decompression assumes it
			scope_disable_fp_exceptions fp_off;

			const track_error error = calculate_compression_error(allocator, track_list, *compressed_tracks_);

			stats_writer->insert("max_error", error.error);
			stats_writer->insert("worst_track", error.index);
			stats_writer->insert("worst_time", error.sample_time);

			// TODO: measure decompression performance
			//if (are_any_enum_flags_set(logging, StatLogging::SummaryDecompression))
				//write_decompression_performance_stats(allocator, settings, *compressed_clip, logging, *stats_writer);
		}
#endif

		if (options.regression_testing)
		{
			// Disable floating point exceptions since decompression assumes it
			scope_disable_fp_exceptions fp_off;

			validate_accuracy(allocator, track_list, *compressed_tracks_, regression_error_threshold);
		}

		if (options.output_bin_filename != nullptr)
		{
			std::ofstream output_file_stream(options.output_bin_filename, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
			if (output_file_stream.is_open())
				output_file_stream.write(reinterpret_cast<const char*>(compressed_tracks_), compressed_tracks_->get_size());
		}

		allocator.deallocate(compressed_tracks_, compressed_tracks_->get_size());
	};

#if defined(SJSON_CPP_WRITER)
	if (runs_writer != nullptr)
		runs_writer->push([&](sjson::ObjectWriter& writer) { try_algorithm_impl(&writer); });
	else
#endif
		try_algorithm_impl(nullptr);
}

static bool read_acl_sjson_file(IAllocator& allocator, const Options& options,
	sjson_file_type& out_file_type,
	sjson_raw_clip& out_raw_clip,
	sjson_raw_track_list& out_raw_track_list)
{
	char* sjson_file_buffer = nullptr;
	size_t file_size = 0;

#if defined(__ANDROID__)
	ClipReader reader(allocator, options.input_buffer, options.input_buffer_size - 1);
#else
	// Use the raw C API with a large buffer to ensure this is as fast as possible
	std::FILE* file = nullptr;

#ifdef _WIN32
	char path[64 * 1024] = { 0 };
	snprintf(path, get_array_size(path), "\\\\?\\%s", options.input_filename);
	fopen_s(&file, path, "rb");
#else
	file = fopen(options.input_filename, "rb");
#endif

	if (file == nullptr)
		return false;

	// Make sure to enable buffering with a large buffer
	const int setvbuf_result = setvbuf(file, NULL, _IOFBF, 1 * 1024 * 1024);
	if (setvbuf_result != 0)
		return false;

	const int fseek_result = fseek(file, 0, SEEK_END);
	if (fseek_result != 0)
	{
		fclose(file);
		return false;
	}

#ifdef _WIN32
	file_size = static_cast<size_t>(_ftelli64(file));
#else
	file_size = static_cast<size_t>(ftello(file));
#endif

	if (file_size == static_cast<size_t>(-1L))
	{
		fclose(file);
		return false;
	}

	rewind(file);

	sjson_file_buffer = allocate_type_array<char>(allocator, file_size);
	const size_t result = fread(sjson_file_buffer, 1, file_size, file);
	fclose(file);

	if (result != file_size)
	{
		deallocate_type_array(allocator, sjson_file_buffer, file_size);
		return false;
	}

	ClipReader reader(allocator, sjson_file_buffer, file_size - 1);
#endif

	const sjson_file_type ftype = reader.get_file_type();
	out_file_type = ftype;

	bool success = false;
	switch (ftype)
	{
	case sjson_file_type::unknown:
	default:
		printf("\nUnknown file type\n");
		break;
	case sjson_file_type::raw_clip:
		success = reader.read_raw_clip(out_raw_clip);
		break;
	case sjson_file_type::raw_track_list:
		success = reader.read_raw_track_list(out_raw_track_list);
		break;
	}

	if (!success)
	{
		const ClipReaderError err = reader.get_error();
		if (err.error != ClipReaderError::None)
			printf("\nError on line %d column %d: %s\n", err.line, err.column, err.get_description());
	}

	deallocate_type_array(allocator, sjson_file_buffer, file_size);
	return success;
}

static bool read_config(IAllocator& allocator, const Options& options, AlgorithmType8& out_algorithm_type, CompressionSettings& out_settings, double& out_regression_error_threshold)
{
#if defined(__ANDROID__)
	sjson::Parser parser(options.config_buffer, options.config_buffer_size - 1);
#else
	std::ifstream t(options.config_filename);
	std::stringstream buffer;
	buffer << t.rdbuf();
	std::string str = buffer.str();

	sjson::Parser parser(str.c_str(), str.length());
#endif

	double version = 0.0;
	if (!parser.read("version", version))
	{
		uint32_t line;
		uint32_t column;
		parser.get_position(line, column);

		printf("Error on line %d column %d: Missing config version\n", line, column);
		return false;
	}

	if (version != 1.0)
	{
		printf("Unsupported version: %f\n", version);
		return false;
	}

	sjson::StringView algorithm_name;
	if (!parser.read("algorithm_name", algorithm_name))
	{
		uint32_t line;
		uint32_t column;
		parser.get_position(line, column);

		printf("Error on line %d column %d: Missing algorithm name\n", line, column);
		return false;
	}

	if (!get_algorithm_type(algorithm_name.c_str(), out_algorithm_type))
	{
		printf("Invalid algorithm name: %s\n", String(allocator, algorithm_name.c_str(), algorithm_name.size()).c_str());
		return false;
	}

	CompressionSettings default_settings;

	sjson::StringView compression_level;
	parser.try_read("level", compression_level, get_compression_level_name(default_settings.level));
	if (!get_compression_level(compression_level.c_str(), out_settings.level))
	{
		printf("Invalid compression level: %s\n", String(allocator, compression_level.c_str(), compression_level.size()).c_str());
		return false;
	}

	sjson::StringView rotation_format;
	parser.try_read("rotation_format", rotation_format, get_rotation_format_name(default_settings.rotation_format));
	if (!get_rotation_format(rotation_format.c_str(), out_settings.rotation_format))
	{
		printf("Invalid rotation format: %s\n", String(allocator, rotation_format.c_str(), rotation_format.size()).c_str());
		return false;
	}

	sjson::StringView translation_format;
	parser.try_read("translation_format", translation_format, get_vector_format_name(default_settings.translation_format));
	if (!get_vector_format(translation_format.c_str(), out_settings.translation_format))
	{
		printf("Invalid translation format: %s\n", String(allocator, translation_format.c_str(), translation_format.size()).c_str());
		return false;
	}

	sjson::StringView scale_format;
	parser.try_read("scale_format", scale_format, get_vector_format_name(default_settings.scale_format));
	if (!get_vector_format(scale_format.c_str(), out_settings.scale_format))
	{
		printf("Invalid scale format: %s\n", String(allocator, scale_format.c_str(), scale_format.size()).c_str());
		return false;
	}

	RangeReductionFlags8 range_reduction = RangeReductionFlags8::None;

	bool rotation_range_reduction;
	parser.try_read("rotation_range_reduction", rotation_range_reduction, are_any_enum_flags_set(default_settings.range_reduction, RangeReductionFlags8::Rotations));
	if (rotation_range_reduction)
		range_reduction |= RangeReductionFlags8::Rotations;

	bool translation_range_reduction;
	parser.try_read("translation_range_reduction", translation_range_reduction, are_any_enum_flags_set(default_settings.range_reduction, RangeReductionFlags8::Translations));
	if (translation_range_reduction)
		range_reduction |= RangeReductionFlags8::Translations;

	bool scale_range_reduction;
	parser.try_read("scale_range_reduction", scale_range_reduction, are_any_enum_flags_set(default_settings.range_reduction, RangeReductionFlags8::Scales));
	if (scale_range_reduction)
		range_reduction |= RangeReductionFlags8::Scales;

	out_settings.range_reduction = range_reduction;

	if (parser.object_begins("segmenting"))
	{
		parser.try_read("enabled", out_settings.segmenting.enabled, false);

		range_reduction = RangeReductionFlags8::None;
		parser.try_read("rotation_range_reduction", rotation_range_reduction, are_any_enum_flags_set(default_settings.segmenting.range_reduction, RangeReductionFlags8::Rotations));
		parser.try_read("translation_range_reduction", translation_range_reduction, are_any_enum_flags_set(default_settings.segmenting.range_reduction, RangeReductionFlags8::Translations));
		parser.try_read("scale_range_reduction", scale_range_reduction, are_any_enum_flags_set(default_settings.segmenting.range_reduction, RangeReductionFlags8::Scales));

		if (rotation_range_reduction)
			range_reduction |= RangeReductionFlags8::Rotations;

		if (translation_range_reduction)
			range_reduction |= RangeReductionFlags8::Translations;

		if (scale_range_reduction)
			range_reduction |= RangeReductionFlags8::Scales;

		out_settings.segmenting.range_reduction = range_reduction;

		if (!parser.object_ends())
		{
			uint32_t line;
			uint32_t column;
			parser.get_position(line, column);

			printf("Error on line %d column %d: Expected segmenting object to end\n", line, column);
			return false;
		}
	}

	parser.try_read("constant_rotation_threshold_angle", out_settings.constant_rotation_threshold_angle, default_settings.constant_rotation_threshold_angle);
	parser.try_read("constant_translation_threshold", out_settings.constant_translation_threshold, default_settings.constant_translation_threshold);
	parser.try_read("constant_scale_threshold", out_settings.constant_scale_threshold, default_settings.constant_scale_threshold);
	parser.try_read("error_threshold", out_settings.error_threshold, default_settings.error_threshold);

	parser.try_read("regression_error_threshold", out_regression_error_threshold, 0.0);

	if (!parser.is_valid() || !parser.remainder_is_comments_and_whitespace())
	{
		uint32_t line;
		uint32_t column;
		parser.get_position(line, column);

		printf("Error on line %d column %d: Expected end of file\n", line, column);
		return false;
	}

	return true;
}

static ISkeletalErrorMetric* create_additive_error_metric(IAllocator& allocator, AdditiveClipFormat8 format)
{
	switch (format)
	{
	case AdditiveClipFormat8::Relative:
		return allocate_type<AdditiveTransformErrorMetric<AdditiveClipFormat8::Relative>>(allocator);
	case AdditiveClipFormat8::Additive0:
		return allocate_type<AdditiveTransformErrorMetric<AdditiveClipFormat8::Additive0>>(allocator);
	case AdditiveClipFormat8::Additive1:
		return allocate_type<AdditiveTransformErrorMetric<AdditiveClipFormat8::Additive1>>(allocator);
	default:
		return nullptr;
	}
}

static void create_additive_base_clip(const Options& options, AnimationClip& clip, const RigidSkeleton& skeleton, AnimationClip& out_base_clip)
{
	// Convert the animation clip to be relative to the bind pose
	const uint16_t num_bones = clip.get_num_bones();
	const uint32_t num_samples = clip.get_num_samples();
	AnimatedBone* bones = clip.get_bones();

	AdditiveClipFormat8 additive_format = AdditiveClipFormat8::None;
	if (options.is_bind_pose_relative)
		additive_format = AdditiveClipFormat8::Relative;
	else if (options.is_bind_pose_additive0)
		additive_format = AdditiveClipFormat8::Additive0;
	else if (options.is_bind_pose_additive1)
		additive_format = AdditiveClipFormat8::Additive1;

	for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
	{
		AnimatedBone& anim_bone = bones[bone_index];

		// Get the bind transform and make sure it has no scale
		const RigidBone& skel_bone = skeleton.get_bone(bone_index);
		const Transform_64 bind_transform = transform_set(skel_bone.bind_transform.rotation, skel_bone.bind_transform.translation, vector_set(1.0));

		for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
		{
			const Quat_64 rotation = quat_normalize(anim_bone.rotation_track.get_sample(sample_index));
			const Vector4_64 translation = anim_bone.translation_track.get_sample(sample_index);
			const Vector4_64 scale = anim_bone.scale_track.get_sample(sample_index);

			const Transform_64 bone_transform = transform_set(rotation, translation, scale);

			Transform_64 bind_local_transform = bone_transform;
			if (options.is_bind_pose_relative)
				bind_local_transform = convert_to_relative(bind_transform, bone_transform);
			else if (options.is_bind_pose_additive0)
				bind_local_transform = convert_to_additive0(bind_transform, bone_transform);
			else if (options.is_bind_pose_additive1)
				bind_local_transform = convert_to_additive1(bind_transform, bone_transform);

			anim_bone.rotation_track.set_sample(sample_index, bind_local_transform.rotation);
			anim_bone.translation_track.set_sample(sample_index, bind_local_transform.translation);
			anim_bone.scale_track.set_sample(sample_index, bind_local_transform.scale);
		}

		AnimatedBone& base_bone = out_base_clip.get_animated_bone(bone_index);
		base_bone.rotation_track.set_sample(0, bind_transform.rotation);
		base_bone.translation_track.set_sample(0, bind_transform.translation);
		base_bone.scale_track.set_sample(0, bind_transform.scale);
	}

	clip.set_additive_base(&out_base_clip, additive_format);
}

static CompressionSettings make_settings(RotationFormat8 rotation_format, VectorFormat8 translation_format, VectorFormat8 scale_format,
	RangeReductionFlags8 clip_range_reduction,
	bool use_segmenting = false, RangeReductionFlags8 segment_range_reduction = RangeReductionFlags8::None)
{
	CompressionSettings settings;
	settings.rotation_format = rotation_format;
	settings.translation_format = translation_format;
	settings.scale_format = scale_format;
	settings.range_reduction = clip_range_reduction;
	settings.segmenting.enabled = use_segmenting;
	settings.segmenting.range_reduction = segment_range_reduction;
	return settings;
}
#endif	// defined(ACL_USE_SJSON)

static int safe_main_impl(int argc, char* argv[])
{
	Options options;

	if (!parse_options(argc, argv, options))
		return -1;

	if (options.profile_decompression)
	{
#if defined(_WIN32)
		// Set the process affinity to core 2, we'll use core 0 for the python script
		SetProcessAffinityMask(GetCurrentProcess(), 1 << 2);
#endif
	}

#if defined(ACL_USE_SJSON)
	ANSIAllocator allocator;
	std::unique_ptr<AnimationClip, Deleter<AnimationClip>> clip;
	std::unique_ptr<RigidSkeleton, Deleter<RigidSkeleton>> skeleton;

#if defined(__ANDROID__)
	const bool is_input_acl_bin_file = options.input_buffer_binary;
#else
	const bool is_input_acl_bin_file = is_acl_bin_file(options.input_filename);
#endif

	bool use_external_config = false;
	AlgorithmType8 algorithm_type = AlgorithmType8::UniformlySampled;
	CompressionSettings settings;

	sjson_file_type sjson_type = sjson_file_type::unknown;
	sjson_raw_clip sjson_clip;
	sjson_raw_track_list sjson_track_list;

	if (!is_input_acl_bin_file)
	{
		if (!read_acl_sjson_file(allocator, options, sjson_type, sjson_clip, sjson_track_list))
			return -1;

		clip = std::move(sjson_clip.clip);
		skeleton = std::move(sjson_clip.skeleton);
		use_external_config = sjson_clip.has_settings;
		algorithm_type = sjson_clip.algorithm_type;
		settings = sjson_clip.settings;
	}

	double regression_error_threshold = 0.1;

#if defined(__ANDROID__)
	if (options.config_buffer != nullptr && options.config_buffer_size != 0)
#else
	if (options.config_filename != nullptr && std::strlen(options.config_filename) != 0)
#endif
	{
		// Override whatever the ACL SJSON file might have contained
		algorithm_type = AlgorithmType8::UniformlySampled;
		settings = CompressionSettings();

		if (!read_config(allocator, options, algorithm_type, settings, regression_error_threshold))
			return -1;

		use_external_config = true;
	}

	// TODO: Make a unique_ptr
	AnimationClip* base_clip = nullptr;

	if (!is_input_acl_bin_file && sjson_type == sjson_file_type::raw_clip)
	{
		// Grab whatever clip we might have read from the sjson file and cast the const away so we can manage the memory
		base_clip = const_cast<AnimationClip*>(clip->get_additive_base());
		if (base_clip == nullptr)
		{
			base_clip = allocate_type<AnimationClip>(allocator, allocator, *skeleton, 1, 30.0F, String(allocator, "Base Clip"));

			if (options.is_bind_pose_relative || options.is_bind_pose_additive0 || options.is_bind_pose_additive1)
				create_additive_base_clip(options, *clip, *skeleton, *base_clip);
		}

		// First try to create an additive error metric
		settings.error_metric = create_additive_error_metric(allocator, clip->get_additive_format());

		if (settings.error_metric == nullptr)
			settings.error_metric = allocate_type<TransformErrorMetric>(allocator);
	}

	// Compress & Decompress
	auto exec_algos = [&](sjson::ArrayWriter* runs_writer)
	{
		StatLogging logging = options.output_stats ? StatLogging::Summary : StatLogging::None;

		if (options.stat_detailed_output)
			logging |= StatLogging::Detailed;

		if (options.stat_exhaustive_output)
			logging |= StatLogging::Exhaustive;

		if (options.profile_decompression)
			logging |= StatLogging::SummaryDecompression | StatLogging::ExhaustiveDecompression;

		if (is_input_acl_bin_file)
		{
#if defined(SJSON_CPP_WRITER)
			if (options.profile_decompression && runs_writer != nullptr)
			{
				// Disable floating point exceptions since decompression assumes it
				scope_disable_fp_exceptions fp_off;

				const CompressionSettings default_settings = get_default_compression_settings();

#if defined(__ANDROID__)
				const CompressedClip* compressed_clip = reinterpret_cast<const CompressedClip*>(options.input_buffer);
				ACL_ASSERT(compressed_clip->is_valid(true).empty(), "Compressed clip is invalid");

				runs_writer->push([&](sjson::ObjectWriter& writer)
				{
					write_decompression_performance_stats(allocator, default_settings, *compressed_clip, logging, writer);
				});
#else
				std::ifstream input_file_stream(options.input_filename, std::ios_base::in | std::ios_base::binary);
				if (input_file_stream.is_open())
				{
					input_file_stream.seekg(0, std::ios_base::end);
					const size_t buffer_size = size_t(input_file_stream.tellg());
					input_file_stream.seekg(0, std::ios_base::beg);

					char* buffer = (char*)allocator.allocate(buffer_size, alignof(CompressedClip));
					input_file_stream.read(buffer, buffer_size);

					const CompressedClip* compressed_clip = reinterpret_cast<const CompressedClip*>(buffer);
					ACL_ASSERT(compressed_clip->is_valid(true).empty(), "Compressed clip is invalid");

					runs_writer->push([&](sjson::ObjectWriter& writer)
					{
						write_decompression_performance_stats(allocator, default_settings, *compressed_clip, logging, writer);
					});

					allocator.deallocate(buffer, buffer_size);
				}
#endif
			}
#endif
		}
		else if (sjson_type == sjson_file_type::raw_clip)
		{
			if (use_external_config)
			{
				ACL_ASSERT(algorithm_type == AlgorithmType8::UniformlySampled, "Only UniformlySampled is supported for now");

				if (options.compression_level_specified)
					settings.level = options.compression_level;

				try_algorithm(options, allocator, *clip, settings, AlgorithmType8::UniformlySampled, logging, runs_writer, regression_error_threshold);
			}
			else if (options.exhaustive_compression)
			{
				const bool use_segmenting_options[] = { false, true };
				for (size_t segmenting_option_index = 0; segmenting_option_index < get_array_size(use_segmenting_options); ++segmenting_option_index)
				{
					const bool use_segmenting = use_segmenting_options[segmenting_option_index];

					CompressionSettings uniform_tests[] =
					{
						make_settings(RotationFormat8::Quat_128, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::None, use_segmenting),
						make_settings(RotationFormat8::Quat_128, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations, use_segmenting),
						make_settings(RotationFormat8::Quat_128, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Translations, use_segmenting),
						make_settings(RotationFormat8::Quat_128, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations, use_segmenting),

						make_settings(RotationFormat8::QuatDropW_96, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::None, use_segmenting),
						make_settings(RotationFormat8::QuatDropW_96, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations, use_segmenting),
						make_settings(RotationFormat8::QuatDropW_96, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Translations, use_segmenting),
						make_settings(RotationFormat8::QuatDropW_96, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations, use_segmenting),

						make_settings(RotationFormat8::QuatDropW_Variable, VectorFormat8::Vector3_Variable, VectorFormat8::Vector3_96, RangeReductionFlags8::Translations, use_segmenting),
						make_settings(RotationFormat8::QuatDropW_Variable, VectorFormat8::Vector3_Variable, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations, use_segmenting),

						make_settings(RotationFormat8::QuatDropW_Variable, VectorFormat8::Vector3_Variable, VectorFormat8::Vector3_Variable, RangeReductionFlags8::AllTracks, use_segmenting),
					};

					for (CompressionSettings test_settings : uniform_tests)
					{
						test_settings.error_metric = settings.error_metric;

						try_algorithm(options, allocator, *clip, test_settings, AlgorithmType8::UniformlySampled, logging, runs_writer, regression_error_threshold);
					}
				}

				{
					CompressionSettings uniform_tests[] =
					{
						make_settings(RotationFormat8::Quat_128, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations, true, RangeReductionFlags8::Rotations),
						make_settings(RotationFormat8::Quat_128, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Translations, true, RangeReductionFlags8::Translations),
						make_settings(RotationFormat8::Quat_128, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations, true, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations),

						make_settings(RotationFormat8::QuatDropW_96, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations, true, RangeReductionFlags8::Rotations),
						make_settings(RotationFormat8::QuatDropW_96, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Translations, true, RangeReductionFlags8::Translations),
						make_settings(RotationFormat8::QuatDropW_96, VectorFormat8::Vector3_96, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations, true, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations),
						make_settings(RotationFormat8::QuatDropW_Variable, VectorFormat8::Vector3_Variable, VectorFormat8::Vector3_96, RangeReductionFlags8::Translations, true, RangeReductionFlags8::Translations),
						make_settings(RotationFormat8::QuatDropW_Variable, VectorFormat8::Vector3_Variable, VectorFormat8::Vector3_96, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations, true, RangeReductionFlags8::Rotations | RangeReductionFlags8::Translations),

						make_settings(RotationFormat8::QuatDropW_Variable, VectorFormat8::Vector3_Variable, VectorFormat8::Vector3_Variable, RangeReductionFlags8::AllTracks, true, RangeReductionFlags8::AllTracks),
					};

					for (CompressionSettings test_settings : uniform_tests)
					{
						test_settings.error_metric = settings.error_metric;

						if (options.compression_level_specified)
							test_settings.level = options.compression_level;

						try_algorithm(options, allocator, *clip, test_settings, AlgorithmType8::UniformlySampled, logging, runs_writer, regression_error_threshold);
					}
				}
			}
			else
			{
				CompressionSettings default_settings = get_default_compression_settings();
				default_settings.error_metric = settings.error_metric;

				if (options.compression_level_specified)
					default_settings.level = options.compression_level;

				try_algorithm(options, allocator, *clip, default_settings, AlgorithmType8::UniformlySampled, logging, runs_writer, regression_error_threshold);
			}
		}
		else if (sjson_type == sjson_file_type::raw_track_list)
		{
			try_algorithm(options, allocator, sjson_track_list.track_list, logging, runs_writer, regression_error_threshold);
		}
	};

#if defined(SJSON_CPP_WRITER)
	if (options.output_stats)
	{
		sjson::FileStreamWriter stream_writer(options.output_stats_file);
		sjson::Writer writer(stream_writer);

		writer["runs"] = [&](sjson::ArrayWriter& runs_writer) { exec_algos(&runs_writer); };
	}
	else
#endif
		exec_algos(nullptr);

	deallocate_type(allocator, settings.error_metric);
	deallocate_type(allocator, base_clip);
#endif	// defined(ACL_USE_SJSON)

	return 0;
}

#ifdef _WIN32
static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS *info)
{
	(void)info;

	if (IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main_impl(int argc, char* argv[])
{
#ifdef _WIN32
	// Disables Windows OS generated error dialogs and reporting
	SetErrorMode(SEM_FAILCRITICALERRORS);
	SetUnhandledExceptionFilter(&unhandled_exception_filter);
	_set_abort_behavior(0, _CALL_REPORTFAULT);
#endif

	// Enable floating point exceptions when possible to detect errors when regression testing
	scope_enable_fp_exceptions fp_on;

	int result = -1;
#if defined(ACL_ON_ASSERT_THROW) || defined(SJSON_CPP_ON_ASSERT_THROW)
	try
#endif
	{
		result = safe_main_impl(argc, argv);
	}
#if defined(ACL_ON_ASSERT_THROW)
	catch (const runtime_assert& exception)
	{
		printf("Assert occurred: %s\n", exception.what());
		result = -1;
	}
#endif
#if defined(SJSON_CPP_ON_ASSERT_THROW)
	catch (const sjson::runtime_assert& exception)
	{
		printf("Assert occurred: %s\n", exception.what());
		result = -1;
	}
#endif

#ifdef _WIN32
	if (IsDebuggerPresent())
	{
		printf("Press any key to continue...\n");
		while (_kbhit() == 0);
	}
#endif

	return result;
}
