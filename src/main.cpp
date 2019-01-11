//
//  main.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 07.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "Utils.hpp"


// Documentation: https://xiph.org/vorbis/doc/

// Page + page header is described here:
// https://xiph.org/vorbis/doc/framing.html

enum {
	HeaderFlag_Continued = 0x1,
	HeaderFlag_First = 0x2,
	HeaderFlag_Last = 0x4
};

struct __attribute__((packed)) PageHeader {
	char capture_pattern[4]; // should be "OggS"
	uint8_t stream_structure_version; // should be 0
	uint8_t header_type_flag; // 0x1: continued, 0x2: first (bos), 0x4: last (eos)
	uint64_t absolute_granule_pos;
	uint32_t stream_serial_num;
	uint32_t page_sequence_num;
	uint32_t page_crc_checksum;
	uint8_t page_segments_num;
};

struct Page {
	PageHeader header;
	uint8_t segment_table[256]; // page_segments_num in len
	uint32_t data_len;
	uint8_t data[256 * 256];
	
	enum ReadHeaderResult { Ok, Eof, Error };
	ReadHeaderResult read_header(IReader* reader) {
		size_t c = reader->read(&header, sizeof(PageHeader), 1);
		if(c == 1) return ReadHeaderResult::Ok;
		if(reader->reachedEnd()) return ReadHeaderResult::Eof;
		return ReadHeaderResult::Error;
	}
	
	OkOrError read(IReader* reader) {
		CHECK(memcmp(header.capture_pattern, "OggS", 4) == 0);
		CHECK(header.stream_structure_version == 0);
		endian_swap_to_little_endian(header.absolute_granule_pos);
		endian_swap_to_little_endian(header.stream_serial_num);
		endian_swap_to_little_endian(header.page_sequence_num);
		endian_swap_to_little_endian(header.page_crc_checksum);
		
		CHECK(reader->read(segment_table, header.page_segments_num, 1) == 1);
		data_len = 0;
		for(uint8_t i = 0; i < header.page_segments_num; ++i)
			data_len += segment_table[i];
		if(header.page_segments_num > 0)
			CHECK(segment_table[header.page_segments_num - 1] != 255); // packets spanning pages not supported currently...
		CHECK(reader->read(data, data_len, 1) == 1);
		
		uint32_t expected_crc = header.page_crc_checksum;
		header.page_crc_checksum = 0; // required by API
		uint32_t calculated_crc = 0;
		calculated_crc = update_crc(calculated_crc, (uint8_t*) &header, sizeof(PageHeader));
		calculated_crc = update_crc(calculated_crc, segment_table, header.page_segments_num);
		calculated_crc = update_crc(calculated_crc, data, data_len);
		CHECK(expected_crc == calculated_crc);
		return OkOrError();
	}
};

struct __attribute__((packed)) VorbisIdHeader {
	// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.2. Identification header
	uint32_t vorbis_version;
	uint8_t audio_channels;
	uint32_t audio_sample_rate;
	uint32_t bitrate_maximum;
	uint32_t bitrate_nominal;
	uint32_t bitrate_minimum;
	uint8_t blocksize;
	uint8_t framing_flag;
};


struct VorbisCodebook {
	uint16_t dimensions;
	uint32_t num_entries;
	bool ordered;
	bool sparse;
	std::vector<uint8_t> codeword_lengths; // 0 is unused
	uint8_t lookup_type;
	double minimum_value;
	double delta_value;
	uint8_t value_bits;
	bool sequence_p;
	uint32_t num_lookup_values;
	std::vector<uint32_t> multiplicands;
	
	OkOrError parse(BitReader& reader) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 3.2.1
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codebook.go
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisCodebook.cs
		// https://github.com/xiph/vorbis/blob/master/lib/codebook.c
		// https://github.com/susimus/ogg_vorbis/
		// https://github.com/Samulus/hz/blob/master/src/lib/vorbis.d
		// https://github.com/latelee/my_live555/blob/master/liveMedia/OggFileParser.cpp
		CHECK(reader.readBitsT<24>() == 0x564342); // sync pattern
		dimensions = reader.readBitsT<16>();
		CHECK(dimensions > 0);
		num_entries = reader.readBitsT<24>();
		CHECK(num_entries > 0);
		codeword_lengths.resize(num_entries);
		ordered = reader.readBitsT<1>();
		
		if(!ordered) {
			sparse = reader.readBitsT<1>();
			if(sparse) {
				for(int i = 0; i < num_entries; ++i) {
					bool flag = reader.readBitsT<1>();
					if(flag)
						codeword_lengths[i] = reader.readBitsT<5>() + 1;
					else
						codeword_lengths[i] = 0; // unused
				}
			}
			else { // not sparse
				for(int i = 0; i < num_entries; ++i)
					codeword_lengths[i] = reader.readBitsT<5>() + 1;
			}
		}
		else { // ordered flag is set
			sparse = false; // not used
			for(int cur_entry = 0; cur_entry < num_entries;) {
				int cur_len = reader.readBitsT<5>() + 1;
				int number = reader.readBits<int>(highest_bit(num_entries - cur_entry));
				for(int i = cur_entry; i < cur_entry + number; ++i)
					codeword_lengths[i] = cur_len;
				cur_entry += number;
				++cur_len;
				CHECK(cur_entry <= num_entries);
			}
		}
		
		lookup_type = reader.readBitsT<4>();
		CHECK(lookup_type == 0 || lookup_type == 1 || lookup_type == 2);
		if(lookup_type == 0) {
			// not used
			minimum_value = 0; delta_value = 0;
			value_bits = 0; sequence_p = false;
			num_lookup_values = 0;
		}
		else if(lookup_type == 1 || lookup_type == 2) {
			minimum_value = float32_unpack(reader.readBitsT<32>());
			delta_value = float32_unpack(reader.readBitsT<32>());
			value_bits = reader.readBitsT<4>() + 1;
			sequence_p = reader.readBitsT<1>();
			if(lookup_type == 1) {
				// lookup1_values:
				// the greatest integer value for which [num_lookup_values] to the power of [codebook_dimensions] is less than or equal to [codebook_entries]’.
				num_lookup_values = 0;
				while(powIntExp(num_lookup_values + 1, dimensions) <= num_entries)
					++num_lookup_values;
			}
			else
				num_lookup_values = num_entries * dimensions;
		}
		else
			assert(false);
		multiplicands.resize(num_lookup_values);
		for(int i = 0; i < num_lookup_values; ++i)
			multiplicands[i] = reader.readBits<uint32_t>(value_bits);
		
		CHECK(!reader.reachedEnd());
		return OkOrError();
	}
};

struct VorbisFloor {
	uint16_t floor_type;
	
	OkOrError parse(BitReader& reader) {
		floor_type = reader.readBitsT<16>();
		CHECK(floor_type == 0 || floor_type == 1);
		// TODO floor0/floor1 decoding ...
		return OkOrError();
	}
};

struct VorbisResidue {
	OkOrError parse(BitReader& reader) {
		// TODO...
		return OkOrError();
	}
};

struct VorbisMapping {
	OkOrError parse(BitReader& reader) {
		// TODO...
		return OkOrError();
	}
};

struct VorbisModeNumber {
	uint8_t block_flag;
	uint16_t window_type;
	uint16_t transform_type;
	uint8_t mapping;
	
	OkOrError parse(BitReader& reader) {
		block_flag = reader.readBitsT<1>();
		window_type = reader.readBitsT<16>();
		transform_type = reader.readBitsT<16>();
		mapping = reader.readBitsT<8>();
		CHECK(reader.readBitsT<1>() == 1); // framing
		return OkOrError();
	}
};

struct VorbisStreamSetup {
	// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
	std::vector<VorbisCodebook> codebooks;
	std::vector<VorbisFloor> floors;
	std::vector<VorbisResidue> residues;
	std::vector<VorbisMapping> mappings;
	std::vector<VorbisModeNumber> modes;
	
	OkOrError parse(BitReader& reader) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisStreamDecoder.cs LoadBooks
		// https://github.com/runningwild/gorbis/blob/master/vorbis/setup_header.go
		
		// Codebooks
		{
			int count = int(reader.readBitsT<8>()) + 1;
			codebooks.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(codebooks[i].parse(reader));
			CHECK(!reader.reachedEnd());
		}

		// Time domain transforms
		{
			int count = reader.readBitsT<6>() + 1;
			for(int i = 0; i < count; ++i)
				// Just placeholders, but we expect them to be 0.
				CHECK(reader.readBitsT<16>() == 0);
			CHECK(!reader.reachedEnd());
		}

		// Floors
		{
			int count = reader.readBitsT<6>() + 1;
			floors.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(floors[i].parse(reader));
			CHECK(!reader.reachedEnd());
		}

		// Residues
		{
			int count = reader.readBitsT<6>() + 1;
			residues.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(residues[i].parse(reader));
			CHECK(!reader.reachedEnd());
		}

		// Mappings
		{
			int count = reader.readBitsT<6>() + 1;
			mappings.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(mappings[i].parse(reader));
			CHECK(!reader.reachedEnd());
		}

		// Modes
		{
			int count = reader.readBitsT<6>() + 1;
			modes.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(modes[i].parse(reader));
			CHECK(!reader.reachedEnd());
		}

		CHECK(reader.readBitsT<1>() == 1); // framing
		CHECK(!reader.reachedEnd()); // not yet...
		// Check that we are at the end now.
		CHECK(reader.readBitsT<8>() == 0);
		CHECK(reader.reachedEnd());
		return OkOrError();
	}
};

struct StreamInfo {
	uint32_t packet_counts_;
	// getCodec(page)
	// packet buffer...
	
	VorbisIdHeader header;
	VorbisStreamSetup setup;

	StreamInfo() : packet_counts_(0) {}
};


struct VorbisPacket {
	StreamInfo* stream;
	uint8_t* data;
	uint32_t data_len; // never more than 256*256
	
	OkOrError parse_id() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.2
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 1);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		CHECK(data_len - 7 == sizeof(VorbisIdHeader));
		VorbisIdHeader& header = stream->header;
		memcpy(&header, &data[7], sizeof(VorbisIdHeader));
		std::cout << "vorbis version: " << header.vorbis_version
		<< ", channels: " << (int) header.audio_channels
		<< ", sample rate: " << header.audio_sample_rate << std::endl;
		CHECK(header.framing_flag == 1);
		return OkOrError();
	}
	
	OkOrError parse_comment() {
		// https://xiph.org/vorbis/doc/v-comment.html
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.3
		// Meta tags, etc.
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 3);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		// ignore for now...
		return OkOrError();
	}

	OkOrError parse_setup() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 5);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		ConstDataReader reader(data + 7, data_len - 7);
		BitReader bitReader(&reader);
		CHECK_ERR(stream->setup.parse(bitReader));
		CHECK(reader.reachedEnd());
		return OkOrError();
	}
	
	OkOrError parse_audio() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		// TODO ...
		return OkOrError();
	}
};


struct OggReader {
	char buffer_[255];
	Page buffer_page_;
	std::map<uint32_t, StreamInfo> streams_;
	size_t packet_counts_;
	std::shared_ptr<IReader> reader_;

	OggReader() : packet_counts_(0) {}
	
	OkOrError full_read(const char* filename) {
		CHECK_ERR(_open_file(filename));
		size_t page_count = 0;
		while(true) {
			Page::ReadHeaderResult res = buffer_page_.read_header(reader_.get());
			if(res == Page::ReadHeaderResult::Ok)
				CHECK_ERR(_read_page());
			else if(res == Page::ReadHeaderResult::Eof)
				break;
			else
				return OkOrError("read error");
			++page_count;
		}
		std::cout << "page count: " << page_count << std::endl;
		std::cout << "packets count: " << packet_counts_ << std::endl;
		return OkOrError();
	}
	
	OkOrError _open_file(const char* fn) {
		reader_ = std::make_shared<FileReader>(fn);
		CHECK_ERR(reader_->isValid());
		return OkOrError();
	}
	
	OkOrError _read_page() {
		CHECK_ERR(buffer_page_.read(reader_.get()));
		if(buffer_page_.header.header_type_flag & HeaderFlag_First) {
			CHECK(streams_.find(buffer_page_.header.stream_serial_num) == streams_.end());
			streams_[buffer_page_.header.stream_serial_num] = StreamInfo();
			std::cout << "new stream: " << buffer_page_.header.stream_serial_num << std::endl;
		}
		CHECK(streams_.find(buffer_page_.header.stream_serial_num) != streams_.end());
		StreamInfo& stream = streams_[buffer_page_.header.stream_serial_num];
		
		// pack packets: join seg table with size 255 and first with <255, each is one packet
		size_t offset = 0;
		uint32_t len = 0;
		for(uint8_t segment_i = 0; segment_i < buffer_page_.header.page_segments_num; ++segment_i) {
			len += buffer_page_.segment_table[segment_i];
			if(buffer_page_.segment_table[segment_i] < 255) {
				// new packet
				VorbisPacket packet;
				packet.stream = &stream;
				packet.data = buffer_page_.data + offset;
				packet.data_len = len;
				if(stream.packet_counts_ == 0)
					CHECK_ERR(packet.parse_id());
				else if(stream.packet_counts_ == 1)
					CHECK_ERR(packet.parse_comment());
				else if(stream.packet_counts_ == 2)
					CHECK_ERR(packet.parse_setup());
				else
					CHECK_ERR(packet.parse_audio());
				++stream.packet_counts_;
				++packet_counts_;
				offset += len;
				len = 0;
			}
		}
		assert(len == 0 && offset == buffer_page_.data_len);
		
		if(buffer_page_.header.header_type_flag & HeaderFlag_Last) {
			streams_.erase(buffer_page_.header.stream_serial_num);
		}
		
		return OkOrError();
	}
};


int main(int argc, const char* argv[]) {
	OggReader reader;
	OkOrError result = reader.full_read(argv[1]);
	if(result.is_error_)
		std::cerr << "error: " << result.err_msg_ << std::endl;
	else
		std::cout << "ok" << std::endl;
	return (int) result.is_error_;
}
