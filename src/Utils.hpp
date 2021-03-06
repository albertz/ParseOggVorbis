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
#include <math.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <iostream>
#include <vector>
#ifdef __APPLE__
#include <machine/endian.h>
#endif
#ifndef BYTE_ORDER
#error BYTE_ORDER not defined
#endif

// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
// Some of the reference functions (9.2) of Vorbis are here.

uint32_t update_crc(uint32_t crc, uint8_t* buffer, int size);


struct OkOrError {
	bool is_error_;
	std::string err_msg_;
	explicit OkOrError(const std::string& err_msg) : is_error_(true), err_msg_(err_msg) {}
	explicit OkOrError() : is_error_(false) {}
};

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define CHECK(v) do { if(!(v)) { return OkOrError(__FILE__ ":" TOSTRING(__LINE__) ": check failed: " #v); } } while(0)
#define CHECK_ERR(v) do { OkOrError res = (v); if(res.is_error_) return res; } while(0)
#define ASSERT_ERR(v) do { OkOrError res = (v); if(res.is_error_) { std::cerr << "assertion failed, has error: " << res.err_msg_ << std::endl; } assert(!res.is_error_); } while(0)

// 9.2.1. ilog
template<typename T>
inline int highest_bit(T v) {
	assert(v >= 0);
	int ret = 0;
	while(v) {
		++ret;
		v >>= 1;
	}
	return ret;
}

// 9.2.4. low_neighbor
// low_neighbor(vec,idx) finds the position n in vector [vec] of the greatest value scalar element for which n is less than [idx] and vector [vec] element n is less than vector [vec] element [idx].
template<typename T>
inline size_t low_neighbor(const std::vector<T>& vec, size_t idx) {
	assert(idx >= 1); assert(!vec.empty()); assert(idx < vec.size());
	T val = vec[idx];
	size_t best_idx;
	T best_val;
	size_t cur_idx = 0;
	// find first valid, assign best_idx/best_val
	while(true) {
		if(cur_idx >= idx)
			return size_t(-1);
		if(vec[cur_idx] < val) {
			best_idx = cur_idx;
			best_val = vec[cur_idx];
			++cur_idx;
			break;
		}
		++cur_idx;
	}
	// now find really the best
	for(; cur_idx < idx; ++cur_idx) {
		if(vec[cur_idx] < val && vec[cur_idx] > best_val) {
			best_idx = cur_idx;
			best_val = vec[cur_idx];
		}
	}
	return best_idx;
}

// 9.2.5. high_neighbor
// high_neighbor(vec,idx) finds the position n in vector [vec] of the lowest value scalar element for which n is less than [idx] and vector [vec] element n is greater than vector [vec] element [idx].
template<typename T>
inline size_t high_neighbor(const std::vector<T>& vec, size_t idx) {
	assert(idx >= 1); assert(!vec.empty()); assert(idx < vec.size());
	T val = vec[idx];
	size_t best_idx;
	T best_val;
	size_t cur_idx = 0;
	// find first valid, assign best_idx/best_val
	while(true) {
		if(cur_idx >= idx)
			return size_t(-1);
		if(vec[cur_idx] > val) {
			best_idx = cur_idx;
			best_val = vec[cur_idx];
			++cur_idx;
			break;
		}
		++cur_idx;
	}
	// now find really the best
	for(; cur_idx < idx; ++cur_idx) {
		if(vec[cur_idx] > val && vec[cur_idx] < best_val) {
			best_idx = cur_idx;
			best_val = vec[cur_idx];
		}
	}
	return best_idx;
}

// 9.2.6. render_point
// render_point(x0,y0,x1,y1,X) is used to find the Y value at point X along the line specified by x0, x1, y0 and y1. This function uses an integer algorithm to solve for the point directly without calculating intervening values along the line.
template<typename T>
inline T render_point(T x0, T y0, T x1, T y1, T X) {
	assert(x0 < x1);
	assert(x0 <= X && X <= x1);
	T adx = x1 - x0;
	assert(adx > 0);
	// written in a way that T can be an unsigned type
	bool dy_positive = y1 >= y0;
	T ady = dy_positive ? (y1 - y0) : (y0 - y1);
	T err = ady * (X - x0);
	T off = err / adx;
	if(dy_positive)
		return y0 + off;
	else
		return y0 - off;
}

// 9.2.7. render_line
// Assigns vec[x] = y for x in [x0,x1-1], where y is interpolated.
// We expect vec to be resized beforehand.
// We allow some x to be outside of the valid range, and will just skip them.
template<typename T>
inline void render_line(size_t x0, T y0, size_t x1, T y1, std::vector<T>& vec) {
	assert(x0 < x1);
	if(x0 >= vec.size())
		return;
	size_t abs_dx = x1 - x0;
	// written in a way that T can be an unsigned type
	bool dy_positive = y1 >= y0;
	T abs_dy = dy_positive ? (y1 - y0) : (y0 - y1);
	T abs_base = abs_dy / abs_dx;
	T abs_err = 0;
	T abs_sy = abs_base + 1;
	assert(abs_dy >= abs_base * abs_dx);
	abs_dy -= abs_base * abs_dx;
	T y = y0;
	vec[x0] = y0;
	for(size_t x = x0 + 1; x < x1; ++x) {
		if(x >= vec.size())
			break;
		abs_err += abs_dy;
		if(abs_err >= abs_dx) {
			abs_err -= abs_dx;
			if(dy_positive)
				y += abs_sy;
			else
				y -= abs_sy;
		}
		else {
			if(dy_positive)
				y += abs_base;
			else
				y -= abs_base;
		}
		vec[x] = y;
	}
	if(dy_positive) {
		assert(y0 <= y); assert(y <= y1);
	} else {
		assert(y0 >= y); assert(y >= y1);
	}
}

/* 32 bit float (not IEEE; nonnormalized mantissa +
 biased exponent) : neeeeeee eeemmmmm mmmmmmmm mmmmmmmm
 Vorbis ref code: Why not IEEE?  It's just not that important here. */

static constexpr int VQ_FEXP = 10;
static constexpr int VQ_FMAN = 21;
static constexpr int VQ_FEXP_BIAS = 768;

// 9.2.2. float32_unpack
inline double float32_unpack(uint32_t v) {
	double mant = v & 0x1fffff;
	bool sign = v & 0x80000000;
	long exp = (v & 0x7fe00000L) >> VQ_FMAN;
	if(sign) mant = -mant;
	exp = exp - (VQ_FMAN - 1) - VQ_FEXP_BIAS;
	if(exp > 63) exp = 63;
	if(exp < -63) exp = -63;
	return ldexp(mant, int(exp));
}

template<typename BaseT>
BaseT powIntExp(BaseT base, int exponent) {
	if(exponent > 0) {
		if(exponent % 2 == 0) {
			BaseT x = powIntExp(base, exponent / 2);
			return x * x;
		}
		return powIntExp(base, exponent - 1) * base;
	}
	else if(exponent < 0)
		return powIntExp(1 / base, -exponent);
	return 1;
}

inline void endian_swap(uint16_t& x) {
	x = ((x>>8) & 0x00FF) | ((x<<8) & 0xFF00);
}

inline void endian_swap(uint32_t& x) {
	x =
	((x<<24) & 0xFF000000) |
	((x<<8)  & 0x00FF0000) |
	((x>>8)  & 0x0000FF00) |
	((x>>24) & 0x000000FF);
}

inline void endian_swap(uint64_t& x) {
	x =
	((x<<56) & 0xFF00000000000000) |
	((x<<40) & 0x00FF000000000000) |
	((x<<24) & 0x0000FF0000000000) |
	((x<<8)  & 0x000000FF00000000) |
	((x>>8)  & 0x00000000FF000000) |
	((x>>24) & 0x0000000000FF0000) |
	((x>>40) & 0x000000000000FF00) |
	((x>>56) & 0x00000000000000FF);
}

// Also works with packed fields.
#define endian_swap_generic(x) do { auto tmp = (x); endian_swap(tmp); (x) = tmp; } while(0)

#if BYTE_ORDER == LITTLE_ENDIAN
#define endian_swap_to_big_endian(x) endian_swap_generic(x)
#define endian_swap_to_little_endian(x)
#elif BYTE_ORDER == BIG_ENDIAN
#define endian_swap_to_big_endian(x)
#define endian_swap_to_little_endian(x) endian_swap_generic(x)
#else
#error unknown byte order
#endif


struct IReader {
	virtual ~IReader() {}
	virtual OkOrError isValid() = 0;
	virtual bool reachedEnd() = 0;
	virtual size_t read(void* ptr, size_t size, size_t nitems) = 0;
};

struct FileReader : IReader {
	FILE* fp_;
	FileReader(const std::string& filename) { fp_ = fopen(filename.c_str(), "rb"); }
	virtual ~FileReader() { if(fp_) fclose(fp_); }
	virtual OkOrError isValid() override { CHECK(fp_ != NULL); return OkOrError(); }
	virtual bool reachedEnd() override { return feof(fp_); }
	virtual size_t read(void* ptr, size_t size, size_t nitems) override {
		return fread(ptr, size, nitems, fp_);
	}
};

struct ConstDataReader : IReader {
	const uint8_t* data_;
	size_t len_;
	bool reached_end_;
	ConstDataReader(const uint8_t* data, size_t len) : data_(data), len_(len), reached_end_(false) {}
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

template<int N>
struct IntTypeByNumBytes {
	// Fallback to next biggest type, if not defined.
	static constexpr int NumBytes = IntTypeByNumBytes<N + 1>::NumBytes;
	typedef typename IntTypeByNumBytes<N + 1>::unsigned_t unsigned_t;
	typedef typename IntTypeByNumBytes<N + 1>::signed_t signed_t;
};
template<> struct IntTypeByNumBytes<1> {
	static constexpr int NumBytes = 1;
	typedef uint8_t unsigned_t;
	typedef int8_t signed_t;
};
template<> struct IntTypeByNumBytes<2> {
	static constexpr int NumBytes = 2;
	typedef uint16_t unsigned_t;
	typedef int16_t signed_t;
};
template<> struct IntTypeByNumBytes<4> {
	static constexpr int NumBytes = 4;
	typedef uint32_t unsigned_t;
	typedef int32_t signed_t;
};
template<> struct IntTypeByNumBytes<8> {
	static constexpr int NumBytes = 8;
	typedef uint64_t unsigned_t;
	typedef int64_t signed_t;
};
template<int N>
struct IntTypeByNumBits {
	static constexpr int _NumBytes = (N + 7) / 8;
	static constexpr int NumBytes = IntTypeByNumBytes<_NumBytes>::NumBytes;
	typedef typename IntTypeByNumBytes<_NumBytes>::unsigned_t unsigned_t;
	typedef typename IntTypeByNumBytes<_NumBytes>::signed_t signed_t;
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
		assert(num > 0 && (unsigned int)(num) <= sizeof(T) * 8);
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
	
	template<int N>
	typename IntTypeByNumBits<N>::unsigned_t readBitsT() {
		typedef typename IntTypeByNumBits<N>::unsigned_t T;
		return readBits<T>(N);
	}

	template<int N>
	typename IntTypeByNumBytes<N>::unsigned_t readBytesT() {
		return readBitsT<N * 8>();
	}

	bool reachedEnd() const { return reached_end_; }
	uint8_t bitOffset() const { return (8 - last_byte_remaining_bits_) % 8; }
};

template<typename T>
struct DataRange {
	T* data_; // not owned
	size_t size_;
	
	DataRange() : data_(nullptr), size_(0) {}
	DataRange(T* data, size_t size) : data_(data), size_(size) {}
	DataRange(std::vector<float>& vec) : data_(&vec[0]), size_(vec.size()) {}
	T& operator[](size_t i) {
		assert(data_);
		assert(i >= 0 && i < size_);
		return data_[i];
	}
	const T& operator[](size_t i) const {
		assert(data_);
		assert(i >= 0 && i < size_);
		return data_[i];
	}
	T* begin() { return data_; }
	T* end() { return data_ + size_; }
	const T* begin() const { return data_; }
	const T* end() const { return data_ + size_; }
	size_t size() const { return size_; }
};

#endif /* Utils_h */
