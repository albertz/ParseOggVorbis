//
//  main.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 07.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include <iostream>
#include <string>
#include <vector>
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
#define CHECK(v) { if(!(v)) return OkOrError(__FILE__ ":" TOSTRING(__LINE__) ": " #v); }
#define CHECK_ERR(v) { OkOrError res = (v); if(res.is_error_) return res; }


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
	
	OkOrError read(FILE* fp) {
		CHECK(fread(&header, sizeof(PageHeader), 1, fp) == 1);
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

struct Reader {
	char buffer_[255];
	Page buffer_page_;
	FILE* fp_;

	OkOrError full_read(const char* filename) {
		CHECK_ERR(_open_file(filename));
		CHECK_ERR(_read_page());
		return OkOrError();
	}
	
	OkOrError _open_file(const char* fn) {
		fp_ = fopen(fn, "r");
		CHECK(fp_ != nullptr);
		return OkOrError();
	}
	
	OkOrError _read_page() {
		return buffer_page_.read(fp_);
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
