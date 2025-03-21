#pragma once

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

#if defined(SJSON_CPP_WRITER)

#include "acl/core/compiler_utils.h"
#include "acl/core/memory_cache.h"
#include "acl/core/utils.h"
#include "acl/algorithm/uniformly_sampled/decoder.h"
#include "acl/decompression/default_output_writer.h"
#include "acl/compression/stream/clip_context.h"
#include "acl/compression/skeleton_error_metric.h"
#include "acl/compression/utils.h"

#include <cstdint>
#include <thread>
#include <chrono>

ACL_IMPL_FILE_PRAGMA_PUSH

namespace acl
{
	inline void write_summary_segment_stats(const SegmentContext& segment, RotationFormat8 rotation_format, VectorFormat8 translation_format, VectorFormat8 scale_format, sjson::ObjectWriter& writer)
	{
		writer["segment_index"] = segment.segment_index;
		writer["num_samples"] = segment.num_samples;

		const uint32_t format_per_track_data_size = get_format_per_track_data_size(*segment.clip, rotation_format, translation_format, scale_format);

		uint32_t segment_size = 0;
		segment_size += format_per_track_data_size;						// Format per track data
		segment_size = align_to(segment_size, 2);						// Align range data
		segment_size += segment.range_data_size;						// Range data
		segment_size = align_to(segment_size, 4);						// Align animated data
		segment_size += segment.animated_data_size;						// Animated track data

		writer["segment_size"] = segment_size;
		writer["animated_frame_size"] = double(segment.animated_data_size) / double(segment.num_samples);
	}

	inline void write_detailed_segment_stats(const SegmentContext& segment, sjson::ObjectWriter& writer)
	{
		uint32_t bit_rate_counts[k_num_bit_rates] = {0};

		for (const BoneStreams& bone_stream : segment.const_bone_iterator())
		{
			const uint8_t rotation_bit_rate = bone_stream.rotations.get_bit_rate();
			if (rotation_bit_rate != k_invalid_bit_rate)
				bit_rate_counts[rotation_bit_rate]++;

			const uint8_t translation_bit_rate = bone_stream.translations.get_bit_rate();
			if (translation_bit_rate != k_invalid_bit_rate)
				bit_rate_counts[translation_bit_rate]++;

			const uint8_t scale_bit_rate = bone_stream.scales.get_bit_rate();
			if (scale_bit_rate != k_invalid_bit_rate)
				bit_rate_counts[scale_bit_rate]++;
		}

		writer["bit_rate_counts"] = [&](sjson::ArrayWriter& bitrate_writer)
		{
			for (uint8_t bit_rate = 0; bit_rate < k_num_bit_rates; ++bit_rate)
				bitrate_writer.push(bit_rate_counts[bit_rate]);
		};

		// We assume that we always interpolate between 2 poses
		const uint32_t animated_pose_byte_size = align_to(segment.animated_pose_bit_size * 2, 8) / 8;
		constexpr uint32_t k_cache_line_byte_size = 64;

		const uint32_t num_segment_header_cache_lines = align_to(segment.total_header_size, k_cache_line_byte_size) / k_cache_line_byte_size;
		const uint32_t num_animated_pose_cache_lines = align_to(animated_pose_byte_size, k_cache_line_byte_size) / k_cache_line_byte_size;
		writer["decomp_touched_bytes"] = segment.clip->decomp_touched_bytes + segment.total_header_size + animated_pose_byte_size;
		writer["decomp_touched_cache_lines"] = segment.clip->decomp_touched_cache_lines + num_segment_header_cache_lines + num_animated_pose_cache_lines;
	}

	inline void write_exhaustive_segment_stats(IAllocator& allocator, const SegmentContext& segment, const ClipContext& raw_clip_context, const ClipContext& additive_base_clip_context, const RigidSkeleton& skeleton, const CompressionSettings& settings, sjson::ObjectWriter& writer)
	{
		const uint16_t num_bones = skeleton.get_num_bones();
		const bool has_scale = segment_context_has_scale(segment);

		Transform_32* raw_local_pose = allocate_type_array<Transform_32>(allocator, num_bones);
		Transform_32* base_local_pose = allocate_type_array<Transform_32>(allocator, num_bones);
		Transform_32* lossy_local_pose = allocate_type_array<Transform_32>(allocator, num_bones);

		const float sample_rate = raw_clip_context.sample_rate;
		const float ref_duration = calculate_duration(raw_clip_context.num_samples, sample_rate);

		BoneError worst_bone_error;

		writer["error_per_frame_and_bone"] = [&](sjson::ArrayWriter& frames_writer)
		{
			for (uint32_t sample_index = 0; sample_index < segment.num_samples; ++sample_index)
			{
				const float sample_time = min(float(segment.clip_sample_offset + sample_index) / sample_rate, ref_duration);

				sample_streams(raw_clip_context.segments[0].bone_streams, num_bones, sample_time, raw_local_pose);
				sample_streams(segment.bone_streams, num_bones, sample_time, lossy_local_pose);

				if (raw_clip_context.has_additive_base)
				{
					const float normalized_sample_time = additive_base_clip_context.num_samples > 1 ? (sample_time / ref_duration) : 0.0F;
					const float additive_sample_time = additive_base_clip_context.num_samples > 1 ? (normalized_sample_time * additive_base_clip_context.duration) : 0.0F;
					sample_streams(additive_base_clip_context.segments[0].bone_streams, num_bones, additive_sample_time, base_local_pose);
				}

				frames_writer.push_newline();
				frames_writer.push([&](sjson::ArrayWriter& frame_writer)
				{
					for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
					{
						float error;
						if (has_scale)
							error = settings.error_metric->calculate_object_bone_error(skeleton, raw_local_pose, base_local_pose, lossy_local_pose, bone_index);
						else
							error = settings.error_metric->calculate_object_bone_error_no_scale(skeleton, raw_local_pose, base_local_pose, lossy_local_pose, bone_index);

						frame_writer.push(error);

						if (error > worst_bone_error.error)
						{
							worst_bone_error.error = error;
							worst_bone_error.index = bone_index;
							worst_bone_error.sample_time = sample_time;
						}
					}
				});
			}
		};

		writer["max_error"] = worst_bone_error.error;
		writer["worst_bone"] = worst_bone_error.index;
		writer["worst_time"] = worst_bone_error.sample_time;

		deallocate_type_array(allocator, raw_local_pose, num_bones);
		deallocate_type_array(allocator, base_local_pose, num_bones);
		deallocate_type_array(allocator, lossy_local_pose, num_bones);
	}

	inline void write_stats(IAllocator& allocator, const AnimationClip& clip, const ClipContext& clip_context, const RigidSkeleton& skeleton,
		const CompressedClip& compressed_clip, const CompressionSettings& settings, const ClipHeader& header, const ClipContext& raw_clip_context,
		const ClipContext& additive_base_clip_context, const ScopeProfiler& compression_time,
		OutputStats& stats)
	{
		ACL_ASSERT(stats.writer != nullptr, "Attempted to log stats without a writer");

		const uint32_t raw_size = clip.get_raw_size();
		const uint32_t compressed_size = compressed_clip.get_size();
		const double compression_ratio = double(raw_size) / double(compressed_size);

		sjson::ObjectWriter& writer = *stats.writer;
		writer["algorithm_name"] = get_algorithm_name(AlgorithmType8::UniformlySampled);
		writer["algorithm_uid"] = settings.get_hash();
		writer["clip_name"] = clip.get_name().c_str();
		writer["raw_size"] = raw_size;
		writer["compressed_size"] = compressed_size;
		writer["compression_ratio"] = compression_ratio;
		writer["compression_time"] = compression_time.get_elapsed_seconds();
		writer["duration"] = clip.get_duration();
		writer["num_samples"] = clip.get_num_samples();
		writer["num_bones"] = clip.get_num_bones();
		writer["rotation_format"] = get_rotation_format_name(settings.rotation_format);
		writer["translation_format"] = get_vector_format_name(settings.translation_format);
		writer["scale_format"] = get_vector_format_name(settings.scale_format);
		writer["range_reduction"] = get_range_reduction_name(settings.range_reduction);
		writer["has_scale"] = clip_context.has_scale;
		writer["error_metric"] = settings.error_metric->get_name();

		if (are_all_enum_flags_set(stats.logging, StatLogging::Detailed) || are_all_enum_flags_set(stats.logging, StatLogging::Exhaustive))
		{
			uint32_t num_default_rotation_tracks = 0;
			uint32_t num_default_translation_tracks = 0;
			uint32_t num_default_scale_tracks = 0;
			uint32_t num_constant_rotation_tracks = 0;
			uint32_t num_constant_translation_tracks = 0;
			uint32_t num_constant_scale_tracks = 0;
			uint32_t num_animated_rotation_tracks = 0;
			uint32_t num_animated_translation_tracks = 0;
			uint32_t num_animated_scale_tracks = 0;

			for (const BoneStreams& bone_stream : clip_context.segments[0].bone_iterator())
			{
				if (bone_stream.is_rotation_default)
					num_default_rotation_tracks++;
				else if (bone_stream.is_rotation_constant)
					num_constant_rotation_tracks++;
				else
					num_animated_rotation_tracks++;

				if (bone_stream.is_translation_default)
					num_default_translation_tracks++;
				else if (bone_stream.is_translation_constant)
					num_constant_translation_tracks++;
				else
					num_animated_translation_tracks++;

				if (bone_stream.is_scale_default)
					num_default_scale_tracks++;
				else if (bone_stream.is_scale_constant)
					num_constant_scale_tracks++;
				else
					num_animated_scale_tracks++;
			}

			const uint32_t num_default_tracks = num_default_rotation_tracks + num_default_translation_tracks + num_default_scale_tracks;
			const uint32_t num_constant_tracks = num_constant_rotation_tracks + num_constant_translation_tracks + num_constant_scale_tracks;
			const uint32_t num_animated_tracks = num_animated_rotation_tracks + num_animated_translation_tracks + num_animated_scale_tracks;

			writer["num_default_rotation_tracks"] = num_default_rotation_tracks;
			writer["num_default_translation_tracks"] = num_default_translation_tracks;
			writer["num_default_scale_tracks"] = num_default_scale_tracks;

			writer["num_constant_rotation_tracks"] = num_constant_rotation_tracks;
			writer["num_constant_translation_tracks"] = num_constant_translation_tracks;
			writer["num_constant_scale_tracks"] = num_constant_scale_tracks;

			writer["num_animated_rotation_tracks"] = num_animated_rotation_tracks;
			writer["num_animated_translation_tracks"] = num_animated_translation_tracks;
			writer["num_animated_scale_tracks"] = num_animated_scale_tracks;

			writer["num_default_tracks"] = num_default_tracks;
			writer["num_constant_tracks"] = num_constant_tracks;
			writer["num_animated_tracks"] = num_animated_tracks;
		}

		if (settings.segmenting.enabled)
		{
			writer["segmenting"] = [&](sjson::ObjectWriter& segmenting_writer)
			{
				segmenting_writer["num_segments"] = header.num_segments;
				segmenting_writer["range_reduction"] = get_range_reduction_name(settings.segmenting.range_reduction);
				segmenting_writer["ideal_num_samples"] = settings.segmenting.ideal_num_samples;
				segmenting_writer["max_num_samples"] = settings.segmenting.max_num_samples;
			};
		}

		writer["segments"] = [&](sjson::ArrayWriter& segments_writer)
		{
			for (const SegmentContext& segment : clip_context.const_segment_iterator())
			{
				segments_writer.push([&](sjson::ObjectWriter& segment_writer)
				{
					write_summary_segment_stats(segment, settings.rotation_format, settings.translation_format, settings.scale_format, segment_writer);

					if (are_all_enum_flags_set(stats.logging, StatLogging::Detailed))
						write_detailed_segment_stats(segment, segment_writer);

					if (are_all_enum_flags_set(stats.logging, StatLogging::Exhaustive))
						write_exhaustive_segment_stats(allocator, segment, raw_clip_context, additive_base_clip_context, skeleton, settings, segment_writer);
				});
			}
		};
	}
}

ACL_IMPL_FILE_PRAGMA_POP

#endif	// #if defined(SJSON_CPP_WRITER)
