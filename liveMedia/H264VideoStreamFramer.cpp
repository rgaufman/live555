/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2013 Live Networks, Inc.  All rights reserved.
// A filter that breaks up a H.264 Video Elementary Stream into NAL units.
// Implementation

#include "H264VideoStreamFramer.hh"
#include "MPEGVideoStreamParser.hh"
#include "BitVector.hh"
#include "H264VideoRTPSource.hh" // for "parseSPropParameterSets()"

////////// H264VideoStreamParser definition //////////

class H264VideoStreamParser: public MPEGVideoStreamParser {
public:
  H264VideoStreamParser(H264VideoStreamFramer* usingSource, FramedSource* inputSource, Boolean includeStartCodeInOutput);
  virtual ~H264VideoStreamParser();

private: // redefined virtual functions:
  virtual void flushInput();
  virtual unsigned parse();

private:
  H264VideoStreamFramer* usingSource() {
    return (H264VideoStreamFramer*)fUsingSource;
  }

  void removeEmulationBytes(u_int8_t* nalUnitCopy, unsigned maxSize, unsigned& nalUnitCopySize);

  void analyze_seq_parameter_set_data(unsigned& num_units_in_tick, unsigned& time_scale, unsigned& fixed_frame_rate_flag);
  void analyze_vui_parameters(BitVector& bv,
			      unsigned& num_units_in_tick, unsigned& time_scale, unsigned& fixed_frame_rate_flag);
#ifdef DO_FULL_SPS_PARSING
  void analyze_hrd_parameters(BitVector& bv);
#endif
  void analyze_sei_data();

private:
  unsigned fOutputStartCodeSize;
  Boolean fHaveSeenFirstStartCode, fHaveSeenFirstByteOfNALUnit;
  u_int8_t fFirstByteOfNALUnit;

  // Fields in H.264 headers, used in parsing:
  unsigned log2_max_frame_num; // log2_max_frame_num_minus4 + 4
  Boolean separate_colour_plane_flag;
  Boolean frame_mbs_only_flag;
};


////////// H264VideoStreamFramer implementation //////////

H264VideoStreamFramer* H264VideoStreamFramer
::createNew(UsageEnvironment& env, FramedSource* inputSource, Boolean includeStartCodeInOutput) {
  return new H264VideoStreamFramer(env, inputSource, True, includeStartCodeInOutput);
}

H264VideoStreamFramer
::H264VideoStreamFramer(UsageEnvironment& env, FramedSource* inputSource, Boolean createParser, Boolean includeStartCodeInOutput)
  : MPEGVideoStreamFramer(env, inputSource),
    fLastSeenSPS(NULL), fLastSeenSPSSize(0), fLastSeenPPS(NULL), fLastSeenPPSSize(0) {
  fParser = createParser
    ? new H264VideoStreamParser(this, inputSource, includeStartCodeInOutput)
    : NULL;
  fNextPresentationTime = fPresentationTimeBase;
  fFrameRate = 25.0; // We assume a frame rate of 25 fps, unless we learn otherwise (from parsing a Sequence Parameter Set NAL unit)
}

H264VideoStreamFramer::~H264VideoStreamFramer() {
  delete[] fLastSeenSPS;
  delete[] fLastSeenPPS;
}

void H264VideoStreamFramer::setSPSandPPS(char const* sPropParameterSetsStr) {
  unsigned numSPropRecords;
  SPropRecord* sPropRecords = parseSPropParameterSets(sPropParameterSetsStr, numSPropRecords);
  for (unsigned i = 0; i < numSPropRecords; ++i) {
    if (sPropRecords[i].sPropLength == 0) continue; // bad data
    u_int8_t nal_unit_type = (sPropRecords[i].sPropBytes[0])&0x1F;
    if (nal_unit_type == 7/*SPS*/) {
      saveCopyOfSPS(sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength);
    } else if (nal_unit_type == 8/*PPS*/) {
      saveCopyOfPPS(sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength);
    }
  }
  delete[] sPropRecords;
}

void H264VideoStreamFramer::saveCopyOfSPS(u_int8_t* from, unsigned size) {
  delete[] fLastSeenSPS;
  fLastSeenSPS = new u_int8_t[size];
  memmove(fLastSeenSPS, from, size);

  fLastSeenSPSSize = size;
}

void H264VideoStreamFramer::saveCopyOfPPS(u_int8_t* from, unsigned size) {
  delete[] fLastSeenPPS;
  fLastSeenPPS = new u_int8_t[size];
  memmove(fLastSeenPPS, from, size);

  fLastSeenPPSSize = size;
}

Boolean H264VideoStreamFramer::isH264VideoStreamFramer() const {
  return True;
}


////////// H264VideoStreamParser implementation //////////

H264VideoStreamParser
::H264VideoStreamParser(H264VideoStreamFramer* usingSource, FramedSource* inputSource, Boolean includeStartCodeInOutput)
  : MPEGVideoStreamParser(usingSource, inputSource),
    fOutputStartCodeSize(includeStartCodeInOutput ? 4 : 0), fHaveSeenFirstStartCode(False), fHaveSeenFirstByteOfNALUnit(False),
    // Default values for our parser variables (in case they're not set explicitly in headers that we parse:
    log2_max_frame_num(5), separate_colour_plane_flag(False), frame_mbs_only_flag(True) {
}

H264VideoStreamParser::~H264VideoStreamParser() {
}

void H264VideoStreamParser::removeEmulationBytes(u_int8_t* nalUnitCopy, unsigned maxSize, unsigned& nalUnitCopySize) {
  u_int8_t* nalUnitOrig = fStartOfFrame + fOutputStartCodeSize;
  unsigned const NumBytesInNALunit = fTo - nalUnitOrig;
  nalUnitCopySize = 0;
  if (NumBytesInNALunit > maxSize) return;
  for (unsigned i = 0; i < NumBytesInNALunit; ++i) {
    if (i+2 < NumBytesInNALunit && nalUnitOrig[i] == 0 && nalUnitOrig[i+1] == 0 && nalUnitOrig[i+2] == 3) {
      nalUnitCopy[nalUnitCopySize++] = nalUnitOrig[i++];
      nalUnitCopy[nalUnitCopySize++] = nalUnitOrig[i++];
    } else {
      nalUnitCopy[nalUnitCopySize++] = nalUnitOrig[i];
    }
  }
}

#ifdef DEBUG
char const* nal_unit_type_description[32] = {
  "Unspecified", //0
  "Coded slice of a non-IDR picture", //1
  "Coded slice data partition A", //2
  "Coded slice data partition B", //3
  "Coded slice data partition C", //4
  "Coded slice of an IDR picture", //5
  "Supplemental enhancement information (SEI)", //6
  "Sequence parameter set", //7
  "Picture parameter set", //8
  "Access unit delimiter", //9
  "End of sequence", //10
  "End of stream", //11
  "Filler data", //12
  "Sequence parameter set extension", //13
  "Prefix NAL unit", //14
  "Subset sequence parameter set", //15
  "Reserved", //16
  "Reserved", //17
  "Reserved", //18
  "Coded slice of an auxiliary coded picture without partitioning", //19
  "Coded slice extension", //20
  "Reserved", //21
  "Reserved", //22
  "Reserved", //23
  "Unspecified", //24
  "Unspecified", //25
  "Unspecified", //26
  "Unspecified", //27
  "Unspecified", //28
  "Unspecified", //29
  "Unspecified", //30
  "Unspecified" //31
};
#endif

#ifdef DEBUG
static unsigned numDebugTabs = 1;
#define DEBUG_PRINT_TABS for (unsigned _i = 0; _i < numDebugTabs; ++_i) fprintf(stderr, "\t")
#define DEBUG_PRINT(x) do { DEBUG_PRINT_TABS; fprintf(stderr, "%s: %d\n", #x, x); } while (0)
#define DEBUG_STR(x) do { DEBUG_PRINT_TABS; fprintf(stderr, "%s\n", x); } while (0)
class DebugTab {
public:
  DebugTab() {++numDebugTabs;}
  ~DebugTab() {--numDebugTabs;}
};
#define DEBUG_TAB DebugTab dummy
#else
#define DEBUG_PRINT(x) do {x = x;} while (0)
    // Note: the "x=x;" statement is intended to eliminate "unused variable" compiler warning messages
#define DEBUG_STR(x) do {} while (0)
#define DEBUG_TAB do {} while (0)
#endif

#ifdef DO_FULL_SPS_PARSING
void H264VideoStreamParser::analyze_hrd_parameters(BitVector& bv) {
  DEBUG_STR("BEGIN hrd_parameters");
  unsigned cpb_cnt_minus1 = bv.get_expGolomb();
  DEBUG_PRINT(cpb_cnt_minus1);
  unsigned bit_rate_scale = bv.getBits(4);
  DEBUG_PRINT(bit_rate_scale);
  unsigned cpb_size_scale = bv.getBits(4);
  DEBUG_PRINT(cpb_size_scale);
  for (unsigned SchedSelIdx = 0; SchedSelIdx <= cpb_cnt_minus1; ++SchedSelIdx) {
    DEBUG_TAB;
    unsigned bit_rate_value_minus1 = bv.get_expGolomb();
    DEBUG_PRINT(bit_rate_value_minus1);
    unsigned cpb_size_value_minus1 = bv.get_expGolomb();
    DEBUG_PRINT(cpb_size_value_minus1);
    unsigned cbr_flag = bv.get1Bit();
    DEBUG_PRINT(cbr_flag);
  }
  unsigned initial_cpb_removal_delay_length_minus1 = bv.getBits(5);
  DEBUG_PRINT(initial_cpb_removal_delay_length_minus1);
  unsigned cpb_removal_delay_length_minus1 = bv.getBits(5);
  DEBUG_PRINT(cpb_removal_delay_length_minus1);
  unsigned dpb_output_delay_length_minus1 = bv.getBits(5);
  DEBUG_PRINT(dpb_output_delay_length_minus1);
  unsigned time_offset_length = bv.getBits(5);
  DEBUG_PRINT(time_offset_length);
  DEBUG_STR("END hrd_parameters");
}
#endif

void H264VideoStreamParser
::analyze_vui_parameters(BitVector& bv,
			 unsigned& num_units_in_tick, unsigned& time_scale, unsigned& fixed_frame_rate_flag) {
  DEBUG_STR("BEGIN vui_parameters");
  unsigned aspect_ratio_info_present_flag = bv.get1Bit();
  DEBUG_PRINT(aspect_ratio_info_present_flag);
  if (aspect_ratio_info_present_flag) {
    DEBUG_TAB;
    unsigned aspect_ratio_idc = bv.getBits(8);
    DEBUG_PRINT(aspect_ratio_idc);
    if (aspect_ratio_idc == 255/*Extended_SAR*/) {
      bv.skipBits(32); // sar_width; sar_height
    }
  }
  unsigned overscan_info_present_flag = bv.get1Bit();
  DEBUG_PRINT(overscan_info_present_flag);
  if (overscan_info_present_flag) {
    bv.skipBits(1); // overscan_appropriate_flag
  }
  unsigned video_signal_type_present_flag = bv.get1Bit();
  DEBUG_PRINT(video_signal_type_present_flag);
  if (video_signal_type_present_flag) {
    DEBUG_TAB;
    bv.skipBits(4); // video_format; video_full_range_flag
    unsigned colour_description_present_flag = bv.get1Bit();
    DEBUG_PRINT(colour_description_present_flag);
    if (colour_description_present_flag) {
      bv.skipBits(24); // colour_primaries; transfer_characteristics; matrix_coefficients
    }
  }
  unsigned chroma_loc_info_present_flag = bv.get1Bit();
  DEBUG_PRINT(chroma_loc_info_present_flag);
  if (chroma_loc_info_present_flag) {
    (void)bv.get_expGolomb(); // chroma_sample_loc_type_top_field
    (void)bv.get_expGolomb(); // chroma_sample_loc_type_bottom_field
  }
  unsigned timing_info_present_flag = bv.get1Bit();
  DEBUG_PRINT(timing_info_present_flag);
  if (timing_info_present_flag) {
    DEBUG_TAB;
    num_units_in_tick = bv.getBits(32);
    DEBUG_PRINT(num_units_in_tick);
    time_scale = bv.getBits(32);
    DEBUG_PRINT(time_scale);
    fixed_frame_rate_flag = bv.get1Bit();
    DEBUG_PRINT(fixed_frame_rate_flag);
  }
#ifdef DO_FULL_SPS_PARSING
  unsigned nal_hrd_parameters_present_flag = bv.get1Bit();
  DEBUG_PRINT(nal_hrd_parameters_present_flag);
  if (nal_hrd_parameters_present_flag) {
    DEBUG_TAB;
    analyze_hrd_parameters(bv);
  }
  unsigned vcl_hrd_parameters_present_flag = bv.get1Bit();
  DEBUG_PRINT(vcl_hrd_parameters_present_flag);
  if (vcl_hrd_parameters_present_flag) {
    DEBUG_TAB;
    analyze_hrd_parameters(bv);
  }
  if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag) {
    bv.skipBits(1); // low_delay_hrd_flag
  }
  bv.skipBits(1); // pic_struct_present_flag
  unsigned bitstream_restriction_flag = bv.get1Bit();
  DEBUG_PRINT(bitstream_restriction_flag);
  if (bitstream_restriction_flag) {
    bv.skipBits(1); // motion_vectors_over_pic_boundaries_flag
    (void)bv.get_expGolomb(); // max_bytes_per_pic_denom
    (void)bv.get_expGolomb(); // max_bits_per_mb_denom
    (void)bv.get_expGolomb(); // log2_max_mv_length_horizontal
    (void)bv.get_expGolomb(); // log2_max_mv_length_vertical
    (void)bv.get_expGolomb(); // num_reorder_frames
    (void)bv.get_expGolomb(); // max_dec_frame_buffering
  }
  DEBUG_STR("END vui_parameters");
#endif
}

#define SPS_MAX_SIZE 1000 // larger than the largest possible SPS (Sequence Parameter Set) NAL unit

void H264VideoStreamParser
::analyze_seq_parameter_set_data(unsigned& num_units_in_tick, unsigned& time_scale, unsigned& fixed_frame_rate_flag) {
  num_units_in_tick = time_scale = fixed_frame_rate_flag = 0; // default values

  // Begin by making a copy of the NAL unit data, removing any 'emulation prevention' bytes:
  u_int8_t sps[SPS_MAX_SIZE];
  unsigned spsSize;
  removeEmulationBytes(sps, sizeof sps, spsSize);

  BitVector bv(sps, 0, 8*spsSize);

  bv.skipBits(8); // forbidden_zero_bit; nal_ref_idc; nal_unit_type
  unsigned profile_idc = bv.getBits(8);
  DEBUG_PRINT(profile_idc);
  unsigned constraint_setN_flag = bv.getBits(8); // also "reserved_zero_2bits" at end
  DEBUG_PRINT(constraint_setN_flag);
  unsigned level_idc = bv.getBits(8);
  DEBUG_PRINT(level_idc);
  unsigned seq_parameter_set_id = bv.get_expGolomb();
  DEBUG_PRINT(seq_parameter_set_id);
  if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 || profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 || profile_idc == 128 ) {
    DEBUG_TAB;
    unsigned chroma_format_idc = bv.get_expGolomb();
    DEBUG_PRINT(chroma_format_idc);
    if (chroma_format_idc == 3) {
      DEBUG_TAB;
      separate_colour_plane_flag = bv.get1BitBoolean();
      DEBUG_PRINT(separate_colour_plane_flag);
    }
    (void)bv.get_expGolomb(); // bit_depth_luma_minus8
    (void)bv.get_expGolomb(); // bit_depth_chroma_minus8
    bv.skipBits(1); // qpprime_y_zero_transform_bypass_flag
    unsigned seq_scaling_matrix_present_flag = bv.get1Bit();
    DEBUG_PRINT(seq_scaling_matrix_present_flag);
    if (seq_scaling_matrix_present_flag) {
      for (int i = 0; i < ((chroma_format_idc != 3) ? 8 : 12); ++i) {
	DEBUG_TAB;
	DEBUG_PRINT(i);
	unsigned seq_scaling_list_present_flag = bv.get1Bit();
	DEBUG_PRINT(seq_scaling_list_present_flag);
	if (seq_scaling_list_present_flag) {
	  DEBUG_TAB;
	  unsigned sizeOfScalingList = i < 6 ? 16 : 64;
	  unsigned lastScale = 8;
	  unsigned nextScale = 8;
	  for (unsigned j = 0; j < sizeOfScalingList; ++j) {
	    DEBUG_TAB;
	    DEBUG_PRINT(j);
	    DEBUG_PRINT(nextScale);
	    if (nextScale != 0) {
	      DEBUG_TAB;
	      unsigned delta_scale = bv.get_expGolomb();
	      DEBUG_PRINT(delta_scale);
	      nextScale = (lastScale + delta_scale + 256) % 256;
	    }
	    lastScale = (nextScale == 0) ? lastScale : nextScale;
	    DEBUG_PRINT(lastScale);
	  }
	}
      }
    }
  }
  unsigned log2_max_frame_num_minus4 = bv.get_expGolomb();
  DEBUG_PRINT(log2_max_frame_num_minus4);
  log2_max_frame_num = log2_max_frame_num_minus4 + 4;
  unsigned pic_order_cnt_type = bv.get_expGolomb();
  DEBUG_PRINT(pic_order_cnt_type);
  if (pic_order_cnt_type == 0) {
    DEBUG_TAB;
    unsigned log2_max_pic_order_cnt_lsb_minus4 = bv.get_expGolomb();
    DEBUG_PRINT(log2_max_pic_order_cnt_lsb_minus4);
  } else if (pic_order_cnt_type == 1) {
    DEBUG_TAB;
    bv.skipBits(1); // delta_pic_order_always_zero_flag
    (void)bv.get_expGolomb(); // offset_for_non_ref_pic
    (void)bv.get_expGolomb(); // offset_for_top_to_bottom_field
    unsigned num_ref_frames_in_pic_order_cnt_cycle = bv.get_expGolomb();
    DEBUG_PRINT(num_ref_frames_in_pic_order_cnt_cycle);
    for (unsigned i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i) {
      (void)bv.get_expGolomb(); // offset_for_ref_frame[i]
    }
  }
  unsigned max_num_ref_frames = bv.get_expGolomb();
  DEBUG_PRINT(max_num_ref_frames);
  unsigned gaps_in_frame_num_value_allowed_flag = bv.get1Bit();
  DEBUG_PRINT(gaps_in_frame_num_value_allowed_flag);
  unsigned pic_width_in_mbs_minus1 = bv.get_expGolomb();
  DEBUG_PRINT(pic_width_in_mbs_minus1);
  unsigned pic_height_in_map_units_minus1 = bv.get_expGolomb();
  DEBUG_PRINT(pic_height_in_map_units_minus1);
  frame_mbs_only_flag = bv.get1BitBoolean();
  DEBUG_PRINT(frame_mbs_only_flag);
  if (!frame_mbs_only_flag) {
    bv.skipBits(1); // mb_adaptive_frame_field_flag
  }
  bv.skipBits(1); // direct_8x8_inference_flag
  unsigned frame_cropping_flag = bv.get1Bit();
  DEBUG_PRINT(frame_cropping_flag);
  if (frame_cropping_flag) {
    (void)bv.get_expGolomb(); // frame_crop_left_offset
    (void)bv.get_expGolomb(); // frame_crop_right_offset
    (void)bv.get_expGolomb(); // frame_crop_top_offset
    (void)bv.get_expGolomb(); // frame_crop_bottom_offset
  }
  unsigned vui_parameters_present_flag = bv.get1Bit();
  DEBUG_PRINT(vui_parameters_present_flag);
  if (vui_parameters_present_flag) {
    DEBUG_TAB;
    analyze_vui_parameters(bv,num_units_in_tick, time_scale, fixed_frame_rate_flag);
  }
}

#define SEI_MAX_SIZE 5000 // larger than the largest possible SEI NAL unit

#ifdef DEBUG
#define MAX_SEI_PAYLOAD_TYPE_DESCRIPTION 46
char const* sei_payloadType_description[MAX_SEI_PAYLOAD_TYPE_DESCRIPTION+1] = {
  "buffering_period", //0
  "pic_timing", //1
  "pan_scan_rect", //2
  "filler_payload", //3
  "user_data_registered_itu_t_t35", //4
  "user_data_unregistered", //5
  "recovery_point", //6
  "dec_ref_pic_marking_repetition", //7
  "spare_pic", //8
  "scene_info", //9
  "sub_seq_info", //10
  "sub_seq_layer_characteristics", //11
  "sub_seq_characteristics", //12
  "full_frame_freeze", //13
  "full_frame_freeze_release", //14
  "full_frame_snapshot", //15
  "progressive_refinement_segment_start", //16
  "progressive_refinement_segment_end", //17
  "motion_constrained_slice_group_set", //18
  "film_grain_characteristics", //19
  "deblocking_filter_display_preference", //20
  "stereo_video_info", //21
  "post_filter_hint", //22
  "tone_mapping_info", //23
  "scalability_info", //24
  "sub_pic_scalable_layer", //25
  "non_required_layer_rep", //26
  "priority_layer_info", //27
  "layers_not_present", //28
  "layer_dependency_change", //29
  "scalable_nesting", //30
  "base_layer_temporal_hrd", //31
  "quality_layer_integrity_check", //32
  "redundant_pic_property", //33
  "tl0_dep_rep_index", //34
  "tl_switching_point", //35
  "parallel_decoding_info", //36
  "mvc_scalable_nesting", //37
  "view_scalability_info", //38
  "multiview_scene_info", //39
  "multiview_acquisition_info", //40
  "non_required_view_component", //41
  "view_dependency_change", //42
  "operation_points_not_present", //43
  "base_view_temporal_hrd", //44
  "frame_packing_arrangement", //45
  "reserved_sei_message" // 46 or higher
};
#endif

void H264VideoStreamParser::analyze_sei_data() {
  // Begin by making a copy of the NAL unit data, removing any 'emulation prevention' bytes:
  u_int8_t sei[SEI_MAX_SIZE];
  unsigned seiSize;
  removeEmulationBytes(sei, sizeof sei, seiSize);

  unsigned j = 1; // skip the initial byte (forbidden_zero_bit; nal_ref_idc; nal_unit_type); we've already seen it 
  while (j < seiSize) {
    unsigned payloadType = 0;
    do {
      payloadType += sei[j];
    } while (sei[j++] == 255 && j < seiSize);
    if (j >= seiSize) break;

    unsigned payloadSize = 0;
    do {
      payloadSize += sei[j];
    } while (sei[j++] == 255 && j < seiSize);
    if (j >= seiSize) break;

#ifdef DEBUG
    unsigned descriptionNum = payloadType <= MAX_SEI_PAYLOAD_TYPE_DESCRIPTION ? payloadType : MAX_SEI_PAYLOAD_TYPE_DESCRIPTION;
    fprintf(stderr, "\tpayloadType %d (\"%s\"); payloadSize %d\n", payloadType, sei_payloadType_description[descriptionNum], payloadSize);
#endif
    j += payloadSize;
  }
}

void H264VideoStreamParser::flushInput() {
  fHaveSeenFirstStartCode = False;
  fHaveSeenFirstByteOfNALUnit = False;

  StreamParser::flushInput();
}

#define NUM_NEXT_SLICE_HEADER_BYTES_TO_ANALYZE 12

unsigned H264VideoStreamParser::parse() {
  try {
    // The stream must start with a 0x00000001:
    if (!fHaveSeenFirstStartCode) {
      // Skip over any input bytes that precede the first 0x00000001:
      u_int32_t first4Bytes;
      while ((first4Bytes = test4Bytes()) != 0x00000001) {
	get1Byte(); setParseState(); // ensures that we progress over bad data
      }
      skipBytes(4); // skip this initial code
      
      setParseState();
      fHaveSeenFirstStartCode = True; // from now on
    }
    
    if (fOutputStartCodeSize > 0 && curFrameSize() == 0 && !haveSeenEOF()) {
      // Include a start code in the output:
      save4Bytes(0x00000001);
    }

    // Then save everything up until the next 0x00000001 (4 bytes) or 0x000001 (3 bytes), or we hit EOF.
    // Also make note of the first byte, because it contains the "nal_unit_type": 
    if (haveSeenEOF()) {
      // We hit EOF the last time that we tried to parse this data, so we know that any remaining unparsed data
      // forms a complete NAL unit, and that there's no 'start code' at the end:
      unsigned remainingDataSize = totNumValidBytes() - curOffset();
#ifdef DEBUG
      unsigned const trailingNALUnitSize = remainingDataSize;
#endif
      while (remainingDataSize > 0) {
	u_int8_t nextByte = get1Byte();
	if (!fHaveSeenFirstByteOfNALUnit) {
	  fFirstByteOfNALUnit = nextByte;
	  fHaveSeenFirstByteOfNALUnit = True;
	}
	saveByte(nextByte);
	--remainingDataSize;
      }

#ifdef DEBUG
      u_int8_t nal_ref_idc = (fFirstByteOfNALUnit&0x60)>>5;
      u_int8_t nal_unit_type = fFirstByteOfNALUnit&0x1F;
      fprintf(stderr, "Parsed trailing %d-byte NAL-unit (nal_ref_idc: %d, nal_unit_type: %d (\"%s\"))\n",
	      trailingNALUnitSize, nal_ref_idc, nal_unit_type, nal_unit_type_description[nal_unit_type]);
#endif

      (void)get1Byte(); // forces another read, which will cause EOF to get handled for real this time
      return 0;
    } else {
      u_int32_t next4Bytes = test4Bytes();
      if (!fHaveSeenFirstByteOfNALUnit) {
	fFirstByteOfNALUnit = next4Bytes>>24;
	fHaveSeenFirstByteOfNALUnit = True;
      }
      while (next4Bytes != 0x00000001 && (next4Bytes&0xFFFFFF00) != 0x00000100) {
	// We save at least some of "next4Bytes".
	if ((unsigned)(next4Bytes&0xFF) > 1) {
	  // Common case: 0x00000001 or 0x000001 definitely doesn't begin anywhere in "next4Bytes", so we save all of it:
	  save4Bytes(next4Bytes);
	  skipBytes(4);
	} else {
	  // Save the first byte, and continue testing the rest:
	  saveByte(next4Bytes>>24);
	  skipBytes(1);
	}
	setParseState(); // ensures forward progress
	next4Bytes = test4Bytes();
      }
      // Assert: next4Bytes starts with 0x00000001 or 0x000001, and we've saved all previous bytes (forming a complete NAL unit).
      // Skip over these remaining bytes, up until the start of the next NAL unit:
      if (next4Bytes == 0x00000001) {
	skipBytes(4);
      } else {
	skipBytes(3);
      }
    }

#ifdef DEBUG
    u_int8_t nal_ref_idc = (fFirstByteOfNALUnit&0x60)>>5;
#endif
    u_int8_t nal_unit_type = fFirstByteOfNALUnit&0x1F;
    fHaveSeenFirstByteOfNALUnit = False; // for the next NAL unit that we parse
#ifdef DEBUG
    fprintf(stderr, "Parsed %d-byte NAL-unit (nal_ref_idc: %d, nal_unit_type: %d (\"%s\"))\n",
	    curFrameSize()-fOutputStartCodeSize, nal_ref_idc, nal_unit_type, nal_unit_type_description[nal_unit_type]);
#endif

    switch (nal_unit_type) {
      case 6: { // Supplemental enhancement information (SEI)
	analyze_sei_data();
	// Later, perhaps adjust "fPresentationTime" if we saw a "pic_timing" SEI payload??? #####
	break;
      }
      case 7: { // Sequence parameter set
	// First, save a copy of this NAL unit, in case the downstream object wants to see it:
	usingSource()->saveCopyOfSPS(fStartOfFrame + fOutputStartCodeSize, fTo - fStartOfFrame - fOutputStartCodeSize);

	// Parse this NAL unit to check whether frame rate information is present:
	unsigned num_units_in_tick, time_scale, fixed_frame_rate_flag;
	analyze_seq_parameter_set_data(num_units_in_tick, time_scale, fixed_frame_rate_flag);
	if (time_scale > 0 && num_units_in_tick > 0) {
	  usingSource()->fFrameRate = time_scale/(2.0*num_units_in_tick);
#ifdef DEBUG
	  fprintf(stderr, "Set frame rate to %f fps\n", usingSource()->fFrameRate);
	  if (fixed_frame_rate_flag == 0) {
	    fprintf(stderr, "\tWARNING: \"fixed_frame_rate_flag\" was not set\n");
	  }
#endif
	} else {
#ifdef DEBUG
	  fprintf(stderr, "\tThis \"Sequence Parameter Set\" NAL unit contained no frame rate information, so we use a default frame rate of %f fps\n", usingSource()->fFrameRate);
#endif
	}
	break;
      }
      case 8: { // Picture parameter set
	// Save a copy of this NAL unit, in case the downstream object wants to see it:
	usingSource()->saveCopyOfPPS(fStartOfFrame + fOutputStartCodeSize, fTo - fStartOfFrame - fOutputStartCodeSize);
      }
    }

    usingSource()->setPresentationTime();
#ifdef DEBUG
    unsigned long secs = (unsigned long)usingSource()->fPresentationTime.tv_sec;
    unsigned uSecs = (unsigned)usingSource()->fPresentationTime.tv_usec;
    fprintf(stderr, "\tPresentation time: %lu.%06u\n", secs, uSecs);
#endif

    // We check whether this NAL unit ends an 'access unit'.
    // (RTP streamers need to know this in order to figure out whether or not to set the "M" bit.)
    Boolean thisNALUnitEndsAccessUnit;
    if (haveSeenEOF() || nal_unit_type == 11/*End of stream*/) {
      // There is no next NAL unit, so we assume that this one ends the current 'access unit':
      thisNALUnitEndsAccessUnit = True;
    } else if ((nal_unit_type >= 6 && nal_unit_type <= 9) || (nal_unit_type >= 14 && nal_unit_type <= 18)) {
      // These NAL units usually *begin* an access unit, so we assume that they don't end one here:
      thisNALUnitEndsAccessUnit = False;
    } else {
      // We need to check the *next* NAL unit to figure out whether the current NAL unit ends an 'access unit':
      u_int8_t firstBytesOfNextNALUnit[2];
      testBytes(firstBytesOfNextNALUnit, 2);

      u_int8_t const& next_nal_unit_type = firstBytesOfNextNALUnit[0]&0x1F;
      Boolean const nextNALUnitIsVCL = next_nal_unit_type <= 5 && nal_unit_type > 0;
      if (nextNALUnitIsVCL) {
	// The high-order bit of the 2nd byte tells us whether this is the start of a new 'access unit':
	thisNALUnitEndsAccessUnit = (firstBytesOfNextNALUnit[1]&0x80) != 0;
      } else if ((next_nal_unit_type >= 6 && next_nal_unit_type <= 9) || (next_nal_unit_type >= 14 && next_nal_unit_type <= 18)) {
	// The next NAL unit's type is one that usually appears at the start of an 'access unit',
	// so we assume that the current NAL unit ends an 'access unit':
	thisNALUnitEndsAccessUnit = True;
      } else {
	// The next NAL unit definitely doesn't start a new 'access unit', which means that the current NAL unit doesn't end one:
	thisNALUnitEndsAccessUnit = False;
      }
    }
	
    if (thisNALUnitEndsAccessUnit) {
#ifdef DEBUG
      fprintf(stderr, "*****This NAL unit ends the current access unit*****\n");
#endif
      usingSource()->fPictureEndMarker = True;
      ++usingSource()->fPictureCount;

      // Note that the presentation time for the next NAL unit will be different:
      struct timeval& nextPT = usingSource()->fNextPresentationTime; // alias
      nextPT = usingSource()->fPresentationTime;
      double nextFraction = nextPT.tv_usec/1000000.0 + 1/usingSource()->fFrameRate;
      unsigned nextSecsIncrement = (long)nextFraction;
      nextPT.tv_sec += (long)nextSecsIncrement;
      nextPT.tv_usec = (long)((nextFraction - nextSecsIncrement)*1000000);
    }
    setParseState();

    return curFrameSize();
  } catch (int /*e*/) {
#ifdef DEBUG
    fprintf(stderr, "H264VideoStreamParser::parse() EXCEPTION (This is normal behavior - *not* an error)\n");
#endif
    return 0;  // the parsing got interrupted
  }
}
