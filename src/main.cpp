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

struct __attribute__((packed)) IdHeader {
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

static int ilog(unsigned int v) {
	int ret = 0;
	while(v) {
		++ret;
		v >>= 1;
	}
	return ret;
};


struct StreamSetup {
	// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
	uint16_t vorbis_codebook_count;
	uint8_t vorbis_time_count;
	uint8_t vorbis_floor_count;
	
	OkOrError parse_setup(BitReader& reader) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisStreamDecoder.cs LoadBooks
		// https://github.com/runningwild/gorbis/blob/master/vorbis/setup_header.go
		vorbis_codebook_count = uint16_t(reader.readBitsT<8>()) + 1;
		// TODO decode codebooks
		vorbis_time_count = reader.readBitsT<6>() + 1;
		
		// TODO...
		return OkOrError();
	}
};

struct StreamInfo {
	uint32_t packet_counts_;
	// getCodec(page)
	// packet buffer...
	
	StreamSetup setup;

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
		CHECK(data_len - 7 == sizeof(IdHeader));
		IdHeader header;
		memcpy(&header, &data[7], sizeof(IdHeader));
		std::cout << "vorbis version: " << header.vorbis_version
		<< ", channels: " << (int) header.audio_channels
		<< ", sample rate: " << header.audio_sample_rate << std::endl;
		CHECK(header.framing_flag & 1);
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
		stream->setup.parse_setup(reader);
		CHECK(reader.reachedEnd());  // correct?
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
