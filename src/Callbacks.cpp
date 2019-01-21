//
//  Callbacks.cpp
//  ParseOggVorbis
//
//  Created by i6user on 21.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include "Callbacks.h"
#include <map>
#include <string>
#include <iostream>

static int decoder_unique_idx = 1;

struct Info {
	int idx;
	std::string name;
	long sample_rate;
	int num_channels;
	Info() : idx(0), sample_rate(0), num_channels(0) {}
};

std::map<void*, Info> decoders;

static Info& get_decoder(void* ref) {
	auto it = decoders.find(ref);
	assert(it != decoders.end());
	return it->second;
}

extern "C" void register_decoder_ref(void* ref, const char* decoder_name, long sample_rate, int num_channels) {
	Info& info = decoders[ref];
	if(!info.idx) {
		assert(decoder_unique_idx);
		info.idx = decoder_unique_idx;
		++decoder_unique_idx;
	}
	info.name = decoder_name;
	info.sample_rate = sample_rate;
	info.num_channels = num_channels;
}

extern "C" void unregister_decoder_ref(void* ref) {
	decoders.erase(ref);
}

typedef float float32_t;

template<typename T>
struct TypeInfoBase {
	typedef T type;
	typedef T num_type;
};
template<typename T> struct TypeInfo {};
template<> struct TypeInfo<float32_t> : TypeInfoBase<float32_t> {
	static constexpr const char* name = "f32";
};
template<> struct TypeInfo<int32_t> : TypeInfoBase<int32_t> {
	static constexpr const char* name = "i32";
};
template<> struct TypeInfo<uint32_t> : TypeInfoBase<uint32_t> {
	static constexpr const char* name = "u32";
};
template<> struct TypeInfo<uint8_t> : TypeInfoBase<uint8_t> {
	static constexpr const char* name = "u8";
	typedef int num_type;
};

template<typename T>
void push_data_short_stdout_T(void* ref, const char* name, int channel, const T* data, size_t len) {
	Info& info = get_decoder(ref);
	std::cout
	<< "decoder=" << info.idx << " '" << info.name << "' name='" << name << "'"
	<< " channel=" << channel;
	if(!data) {
		std::cout << " data=NULL";
	}
	else {
		std::cout << " data=" << TypeInfo<T>::name << "{";
		for(size_t i = 0; i < len; ++i) {
			if(i == 10) {
				std::cout << " ...";
				break;
			}
			if(i > 0) std::cout << " ";
			std::cout << (typename TypeInfo<T>::num_type) data[i];
		}
		std::cout << "} len=" << len;
	}
	std::cout << std::endl;
}

template<typename T>
void push_data_T(void* ref, const char* name, int channel, const T* data, size_t len) {
	//Info& info = get_decoder(ref);
	push_data_short_stdout_T(ref, name, channel, data, len);
}

extern "C" void push_data_float(void* ref, const char* name, int channel, const float* data, size_t len) {
	push_data_T(ref, name, channel, data, len);
}
extern "C" void push_data_u32(void* ref, const char* name, int channel, const uint32_t* data, size_t len) {
	push_data_T(ref, name, channel, data, len);
}
extern "C" void push_data_u8(void* ref, const char* name, int channel, const uint8_t* data, size_t len) {
	push_data_T(ref, name, channel, data, len);
}
extern "C" void push_data_i32(void* ref, const char* name, int channel, const int32_t* data, size_t len) {
	push_data_T(ref, name, channel, data, len);
}
extern "C" void push_data_int(void* ref, const char* name, int channel, const int* data, size_t len) {
	push_data_T(ref, name, channel, data, len);
}

