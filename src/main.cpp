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
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#ifdef __APPLE__
#include <machine/endian.h>
#endif
#ifndef BYTE_ORDER
#error BYTE_ORDER not defined
#endif


struct OkOrError {
	bool is_error_;
	std::string err_msg_;
	explicit OkOrError(const std::string& err_msg) : is_error_(true), err_msg_(err_msg) {}
	explicit OkOrError() : is_error_(false) {}
};

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CHECK(v) do { if(!(v)) return OkOrError(__FILE__ ":" TOSTRING(__LINE__) ": " #v); } while(0)
#define CHECK_ERR(v) do { OkOrError res = (v); if(res.is_error_) return res; } while(0)


inline void endian_swap(uint16_t& x) {
	x = ((x>>8) & 0x00FF) | ((x<<8) & 0xFF00);
}

inline void endian_swap(uint32_t& x) {
	x = ((x<<24) & 0xFF000000) |
		((x<<8)  & 0x00FF0000) |
		((x>>8)  & 0x0000FF00) |
		((x>>24) & 0x000000FF);
}

inline void endian_swap(uint64_t& x) {
	x = ((x<<56) & 0xFF00000000000000) |
		((x<<40) & 0x00FF000000000000) |
		((x<<24) & 0x0000FF0000000000) |
		((x<<8)  & 0x000000FF00000000) |
		((x>>8)  & 0x00000000FF000000) |
		((x>>24) & 0x0000000000FF0000) |
		((x>>40) & 0x000000000000FF00) |
		((x>>56) & 0x00000000000000FF);
}

template<typename T>
inline void endian_swap_to_big_endian(T& x) {
#if BYTE_ORDER == LITTLE_ENDIAN
	endian_swap(x);
#endif
}

template<typename T>
inline void endian_swap_to_little_endian(T& x) {
#if BYTE_ORDER == BIG_ENDIAN
	endian_swap(x);
#endif
}

#include "crctable.h"

static uint32_t update_crc(uint32_t crc, uint8_t* buffer, int size){
	while(size >= 8) {
		crc ^= buffer[0]<<24 | buffer[1]<<16 | buffer[2]<<8 | buffer[3];
		
		crc = crc_lookup[7][ crc>>24      ] ^ crc_lookup[6][(crc>>16)&0xff] ^
			  crc_lookup[5][(crc>> 8)&0xff] ^ crc_lookup[4][ crc     &0xff] ^
			  crc_lookup[3][buffer[4]     ] ^ crc_lookup[2][buffer[5]     ] ^
			  crc_lookup[1][buffer[6]     ] ^ crc_lookup[0][buffer[7]     ];
		
		buffer += 8;
		size -= 8;
	}
	
	while(size--)
		crc = (crc<<8) ^ crc_lookup[0][((crc>>24)&0xff) ^ *buffer++];
	return crc;
}

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
	ReadHeaderResult read_header(FILE* fp) {
		size_t c = fread(&header, sizeof(PageHeader), 1, fp);
		if(c == 1) return ReadHeaderResult::Ok;
		if(feof(fp)) return ReadHeaderResult::Eof;
		return ReadHeaderResult::Error;
	}
	
	OkOrError read(FILE* fp) {
		CHECK(memcmp(header.capture_pattern, "OggS", 4) == 0);
		CHECK(header.stream_structure_version == 0);
		endian_swap_to_little_endian(header.absolute_granule_pos);
		endian_swap_to_little_endian(header.stream_serial_num);
		endian_swap_to_little_endian(header.page_sequence_num);
		endian_swap_to_little_endian(header.page_crc_checksum);
		
		CHECK(fread(segment_table, header.page_segments_num, 1, fp) == 1);
		data_len = 0;
		for(uint8_t i = 0; i < header.page_segments_num; ++i)
			data_len += segment_table[i];
		if(header.page_segments_num > 0)
			CHECK(segment_table[header.page_segments_num - 1] != 255); // packets spanning pages not supported currently...
		CHECK(fread(data, data_len, 1, fp) == 1);
		
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

struct Packet {
	uint8_t* data;
	uint32_t data_len; // never more than 256*256
	
	OkOrError parse_id() {
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
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 3);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		// ignore for now...
		return OkOrError();
	}

	OkOrError parse_setup() {
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 5);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		// TODO: get the important things...
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		return OkOrError();
	}
	
	OkOrError parse_audio() {
		// TODO ...
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		return OkOrError();
	}
};

struct StreamInfo {
	uint32_t packet_counts_;
	// getCodec(page)
	// packet buffer...
	
	StreamInfo() : packet_counts_(0) {}
};

struct Reader {
	char buffer_[255];
	Page buffer_page_;
	std::map<uint32_t, StreamInfo> streams_;
	size_t packet_counts_;
	FILE* fp_;

	Reader() : packet_counts_(0) {}
	
	OkOrError full_read(const char* filename) {
		CHECK_ERR(_open_file(filename));
		size_t page_count = 0;
		while(true) {
			Page::ReadHeaderResult res = buffer_page_.read_header(fp_);
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
		fp_ = fopen(fn, "r");
		CHECK(fp_ != nullptr);
		return OkOrError();
	}
	
	OkOrError _read_page() {
		CHECK_ERR(buffer_page_.read(fp_));
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
				Packet packet;
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
	Reader reader;
	OkOrError result = reader.full_read(argv[1]);
	if(result.is_error_)
		std::cerr << "error: " << result.err_msg_ << std::endl;
	else
		std::cout << "ok" << std::endl;
	return (int) result.is_error_;
}
