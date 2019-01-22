//
//  Callbacks.cpp
//  ParseOggVorbis
//
//  Created by i6user on 21.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include "Callbacks.h"
#include <map>
#include <set>
#include <string>
#include <iostream>
#include <type_traits>
#include <iterator>

static int decoder_unique_idx = 1;

struct Info {
	int idx;
	std::string name;
	void* ref;
	std::set<void*> aliases;
	long sample_rate;
	int num_channels;
	Info() : idx(0), ref(nullptr), sample_rate(0), num_channels(0) {}
};

std::map<void*, Info> decoders;
std::map<void*, void*> decoder_alias_map;

static Info& get_decoder(void* ref) {
	auto alias_it = decoder_alias_map.find(ref);
	if(alias_it != decoder_alias_map.end())
		ref = alias_it->second;
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
	info.ref = ref;
	info.name = decoder_name;
	info.sample_rate = sample_rate;
	info.num_channels = num_channels;
}

extern "C" void register_decoder_alias(void* orig_ref, void* alias_ref) {
	Info& info = get_decoder(orig_ref);
	info.aliases.insert(alias_ref);
	decoder_alias_map[alias_ref] = info.ref;
}

extern "C" void unregister_decoder_ref(void* ref) {
	Info& info = get_decoder(ref);
	for(void* alias_ref : info.aliases)
		decoder_alias_map.erase(alias_ref);
	decoders.erase(info.ref); // warning: after this call, info becomes invalid
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
template<> struct TypeInfo<bool> : TypeInfoBase<bool> {
	static constexpr const char* name = "bool";
};

template<typename It, typename T=typename std::iterator_traits<It>::value_type>
struct IsNull {
	static bool check(const It& it) {
		return ((const T*) it) == nullptr;
	}
};
template<typename It>
struct IsNull<It> {
	static bool check(const It& it) {
		return false;
	}
};
template<typename It>
bool is_null(const It& it) { return IsNull<It>::check(it); }

template<typename It>
void push_data_short_stdout_T(void* ref, const char* name, int channel, It data, const It& end) {
	typedef typename std::iterator_traits<It>::value_type T;
	Info& info = get_decoder(ref);
	std::cout
	<< "decoder=" << info.idx << " '" << info.name << "' name='" << name << "'"
	<< " channel=" << channel;
	if(is_null(data)) {
		std::cout << " data=NULL";
	}
	else {
		std::cout << " data=" << TypeInfo<T>::name << "{";
		size_t len = end - data;
		for(size_t i = 0; data != end; ++i, ++data) {
			if(i == 10) {
				std::cout << " ...";
				break;
			}
			if(i > 0) std::cout << " ";
			std::cout << (typename TypeInfo<T>::num_type) *data;
		}
		std::cout << "} len=" << len;
	}
	std::cout << std::endl;
}

template<typename It>
void push_data_T(void* ref, const char* name, int channel, const It& data, const It& end) {
	//Info& info = get_decoder(ref);
	push_data_short_stdout_T(ref, name, channel, data, end);
}

extern "C" void push_data_float(void* ref, const char* name, int channel, const float* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}
extern "C" void push_data_u32(void* ref, const char* name, int channel, const uint32_t* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}
extern "C" void push_data_u8(void* ref, const char* name, int channel, const uint8_t* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}
extern "C" void push_data_i32(void* ref, const char* name, int channel, const int32_t* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}
extern "C" void push_data_int(void* ref, const char* name, int channel, const int* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}

void push_data_bool(void* ref, const char* name, int channel, const std::vector<bool>& data) {
	push_data_T(ref, name, channel, data.begin(), data.end());
}

extern "C" const char* generic_itoa(uint32_t val, int base, int len) {
	assert(base >= 2);
	if(len < 0)
		len = sizeof(val) * 8;  // TODO this is just for base=2...
	static char rep[] = "0123456789abcdef";
	static char buf[33];
	assert(len + 1 <= sizeof(buf));
	char *ptr = &buf[sizeof(buf) - 1];
	*ptr = '\0';
	if(val == 0)
		*--ptr = rep[val % base];
	while(val != 0) {
		*--ptr = rep[val % base];
		val /= base;
	}
	while(ptr >= buf + sizeof(buf) - len)
		*--ptr = '0';
	return ptr;
}
