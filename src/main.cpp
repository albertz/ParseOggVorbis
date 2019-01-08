//
//  main.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 07.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include <iostream>
#include <string>
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


struct __attribute__((packed)) PageHeader {
	char capture_pattern[4];
	char stream_structure_version;
	char header_type_flag;
	uint64_t absolute_granule_pos;
	uint32_t stream_serial_num;
	uint32_t page_sequence_num;
	uint32_t page_checksum;
	uint8_t page_segments_num;
};

struct Reader {
	char buffer_[254];
	PageHeader buffer_page_header_;
	FILE* fp_;

	OkOrError full_read(const char* filename) {
		CHECK_ERR(_open_file(filename));
		CHECK_ERR(_read_page_header());
		return OkOrError();
	}
	
	OkOrError _open_file(const char* fn) {
		fp_ = fopen(fn, "r");
		CHECK(fp_ != nullptr);
		return OkOrError();
	}
	
	OkOrError _read_page_header() {
		CHECK(fread(&buffer_page_header_, sizeof(PageHeader), 1, fp_) == 1);
		CHECK(memcmp(buffer_page_header_.capture_pattern, "OggS", 4) == 0);
		CHECK(buffer_page_header_.stream_structure_version == 0);
		endian_swap_to_big_endian(buffer_page_header_.absolute_granule_pos);
		endian_swap_to_big_endian(buffer_page_header_.stream_serial_num);
		endian_swap_to_big_endian(buffer_page_header_.page_sequence_num);
		endian_swap_to_big_endian(buffer_page_header_.page_checksum);
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
