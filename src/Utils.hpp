//
//  Utils.hpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 09.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#ifndef Utils_h
#define Utils_h

#include <stdint.h>
#include <string>
#ifdef __APPLE__
#include <machine/endian.h>
#endif
#ifndef BYTE_ORDER
#error BYTE_ORDER not defined
#endif


uint32_t update_crc(uint32_t crc, uint8_t* buffer, int size);


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

#endif /* Utils_h */
