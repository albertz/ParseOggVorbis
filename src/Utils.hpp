//
//  Utils.hpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 09.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#ifndef Utils_h
#define Utils_h

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <iostream>
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
#define CHECK(v) do { if(!(v)) return OkOrError(__FILE__ ":" TOSTRING(__LINE__) ": check failed: " #v); } while(0)
#define CHECK_ERR(v) do { OkOrError res = (v); if(res.is_error_) return res; } while(0)
#define ASSERT_ERR(v) do { OkOrError res = (v); if(res.is_error_) { std::cerr << "assertion failed, has error: " << res.err_msg_ << std::endl; } assert(!res.is_error_); } while(0)


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


struct IReader {
	virtual ~IReader() {}
	virtual OkOrError isValid() = 0;
	virtual bool reachedEnd() = 0;
	virtual size_t read(void* ptr, size_t size, size_t nitems) = 0;
};

struct FileReader : IReader {
	FILE* fp_;
	FileReader(const char* filename) { fp_ = fopen(filename, "r"); }
	virtual ~FileReader() { if(fp_) fclose(fp_); }
	virtual OkOrError isValid() override { CHECK(fp_ != NULL); return OkOrError(); }
	virtual bool reachedEnd() override { return feof(fp_); }
	virtual size_t read(void* ptr, size_t size, size_t nitems) override {
		return fread(ptr, size, nitems, fp_);
	}
};

struct ConstDataReader : IReader {
	const char* data_;
	size_t len_;
	bool reached_end_;
	ConstDataReader(const char* data, size_t len) : data_(data), len_(len), reached_end_(false) {}
	virtual OkOrError isValid() override { return OkOrError(); }
	virtual bool reachedEnd() override { return reached_end_; }
	virtual size_t read(void* ptr, size_t size, size_t nitems) override {
		if(len_ / size < nitems) {
			nitems = len_ / size;
			reached_end_ = true;
		}
		memcpy(ptr, data_, size * nitems);
		data_ += size * nitems;
		len_ -= size * nitems;
		return nitems;
	}
};

struct BitReader {
	// If a byte represents the number 1, the Vorbis documentation (https://xiph.org/vorbis/doc/Vorbis_I_spec.html)
	// says that the single set bit is the 'least significant' bit (LSb).
	// The 'most significant' bit (MSb) is thus represented by the byte 128.
	// LSbit of a binary integer comes first = bit position 0.
	// (3-2-1-0) 'big endian' = 'most significant byte first'.
	// (0-1-2-3) 'little endian' or 'least significant byte first'.
	// Little endian is what we support.
	// We allow to reach the end, i.e. this is not an error.
	IReader* reader_;
	int last_byte_remaining_bits_; // 0..7
	uint8_t last_byte_;
	bool reached_end_;
	BitReader(IReader* reader) : reader_(reader), last_byte_remaining_bits_(0), last_byte_(0), reached_end_(false) {}
	
	template<typename T>
	T readBits(int num) {
		assert(num > 0 && num <= sizeof(T) * 8);
		T out = 0;
		int i = 0;
		while(num > 0) {
			if(last_byte_remaining_bits_ == 0) {
				while(num >= 64) {
					uint64_t w;
					if(reader_->read(&w, 8, 1) == 0)
						break;
					endian_swap_to_little_endian(w);
					out += T(w) << i;
					num -= 64;
					i += 64;
				}
				while(num >= 32) {
					uint32_t w;
					if(reader_->read(&w, 4, 1) == 0)
						break;
					endian_swap_to_little_endian(w);
					out += T(w) << i;
					num -= 32;
					i += 32;
				}
				while(num >= 16) {
					uint16_t w;
					if(reader_->read(&w, 2, 1) == 0)
						break;
					endian_swap_to_little_endian(w);
					out += T(w) << i;
					num -= 16;
					i += 16;
				}
				while(num >= 8) {
					uint8_t b;
					if(reader_->read(&b, 1, 1) == 0)
						break; // handled below
					out += T(b) << i;
					num -= 8;
					i += 8;
				}
				if(num == 0)
					break;
				if(reader_->read(&last_byte_, 1, 1) == 0) {
					reached_end_ = true;
					break;
				}
				last_byte_remaining_bits_ = 8;
			}
			out += T(last_byte_ & 1) << i;
			last_byte_ >>= 1;
			--last_byte_remaining_bits_;
			--num;
			++i;
		}
		return out;
	}
	
	template<typename T, bool BigEndian=false>
	T readBytes(int num) {
		assert(num > 0 && num <= sizeof(T));
		static_assert(!BigEndian, "not implemented otherwise...");
		return readBits<T>(num * 8);
	}
	
	bool reachedEnd() const { return reached_end_; }
};

#endif /* Utils_h */
