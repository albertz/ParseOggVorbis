//
//  Callbacks.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 21.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include "Callbacks.h"
#include <stdio.h>
#include <map>
#include <set>
#include <string>
#include <iostream>
#include <type_traits>
#include <iterator>
#include <assert.h>


typedef float float32_t;

template<typename T>
struct TypeInfoBase {
	typedef T type;
	typedef T num_type; // for std::cout, visual repr
	typedef T raw_type; // when writing to file
};
template<typename T> struct TypeInfo {};
template<> struct TypeInfo<float32_t> : TypeInfoBase<float32_t> {
	static constexpr const char* name = "f32";
	static constexpr uint8_t type_id = DT_Float32;
};
template<> struct TypeInfo<int32_t> : TypeInfoBase<int32_t> {
	static constexpr const char* name = "i32";
	static constexpr uint8_t type_id = DT_Int32;
};
template<> struct TypeInfo<uint32_t> : TypeInfoBase<uint32_t> {
	static constexpr const char* name = "u32";
	static constexpr uint8_t type_id = DT_UInt32;
};
template<> struct TypeInfo<uint8_t> : TypeInfoBase<uint8_t> {
	static constexpr const char* name = "u8";
	typedef int num_type;
	static constexpr uint8_t type_id = DT_Uint8;
};
template<> struct TypeInfo<bool> : TypeInfoBase<bool> {
	static constexpr const char* name = "bool";
	typedef uint8_t raw_type;
	static constexpr uint8_t type_id = DT_Bool;
};

template<typename It, typename T=typename std::iterator_traits<It>::value_type>
struct ItToPtrTraits {
	typedef T value_type;
	static bool is_null(const It& it) {
		return ((const value_type*) it) == nullptr;
	}
	static const value_type* const_cast_to_ptr_or_null(const It& it) {
		return (const value_type*) it;
	}
};
template<typename It>
struct ItToPtrTraits<It> {
	typedef typename std::iterator_traits<It>::value_type value_type;
	static bool is_null(const It&) {
		return false;
	}
	static const value_type* const_cast_to_ptr_or_null(const It&) {
		return nullptr;
	}
};
template<typename It>
bool is_null(const It& it) { return ItToPtrTraits<It>::is_null(it); }
template<typename It>
const typename std::iterator_traits<It>::value_type* const_cast_to_ptr_or_null(const It& it) {
	return ItToPtrTraits<It>::const_cast_to_ptr_or_null(it);
}

// Global settings, set in advance, used for the next registered decoder.
enum OutputType {
	OT_null,
	OT_short_stdout,
	OT_file,
} output_type = OT_null;
std::string output_filename;

bool use_data_filter_names = false;
std::set<std::string> data_filter_names;


static int decoder_unique_idx = 1;

struct Info {
	int idx;
	std::string name;
	const void* ref;
	std::set<const void*> aliases;
	long sample_rate;
	int num_channels;
	OutputType output_type;
	FILE* output_file;
	bool use_data_filter_names;
	std::set<std::string> data_name_filters;

	Info() :
	idx(0), ref(nullptr), sample_rate(0), num_channels(0),
	output_type(OT_null), output_file(nullptr), use_data_filter_names(false) {}
	~Info() { reset_output_type(); }

	void reset_output_type() {
		if(output_file) {
			fclose(output_file);
			output_file = nullptr;
		}
		output_type = OT_null;
	}

	void set_output_type(OutputType ot, const std::string& fn="") {
		reset_output_type();
		output_type = ot;
		if(ot == OutputType::OT_file) {
			output_file = fopen(fn.c_str(), "wbx");
			assert(output_file);
			raw_write_to_file("ParseOggVorbis-header-v1");
			write_to_file("decoder-name", name);
			write_to_file("decoder-sample-rate", (uint32_t) sample_rate);
			write_to_file("decoder-num-channels", (uint8_t) num_channels);
		}
	}

	void raw_write_to_file(const std::string& data) {
		raw_write_to_file((const uint8_t*)data.data(), (uint32_t)data.size());
	}

	void raw_write_to_file(const uint8_t* data, uint32_t len) {
		assert(output_file);
		// No error checking, and don't care about endian. Just keep it simple.
		fwrite(&len, sizeof(len), 1, output_file);
		fwrite(data, 1, len, output_file);
	}

	template<typename It>
	void write_to_file(const std::string& key, It begin, It end) {
		raw_write_to_file(key);
		typedef typename std::iterator_traits<It>::value_type T;
		uint8_t raw_type_id = TypeInfo<T>::type_id;
		raw_write_to_file(&raw_type_id, 1);
		typedef typename TypeInfo<T>::raw_type raw_type;
		uint8_t raw_type_size = sizeof(raw_type);
		raw_write_to_file(&raw_type_size, 1);
		uint32_t byte_size = uint32_t(end - begin) * sizeof(raw_type);
		const T* begin_ptr = const_cast_to_ptr_or_null(begin);
		if(begin_ptr) { // works for most iterators, fails e.g. for std::vector<bool>
			assert(sizeof(raw_type) == sizeof(T));
			raw_write_to_file((const uint8_t*)begin_ptr, byte_size);
		} else {
			fwrite(&byte_size, sizeof(byte_size), 1, output_file);
			for(It data = begin; data != end; ++data) {
				raw_type value = (raw_type) *data;
				fwrite(&value, sizeof(raw_type), 1, output_file);
			}
		}
	}

	template<typename T> // e.g. int, bool, etc
	void write_to_file(const std::string& key, const T& value) {
		typedef typename TypeInfo<T>::raw_type raw_type; // make sure that T is a valid type
		write_to_file(key, (const raw_type*) &value, &value + 1);
	}

	void write_to_file(const std::string& key, const std::string& value_str) {
		const uint8_t* data = (const uint8_t*) value_str.data();
		write_to_file(key, data, data + value_str.size());
	}

	void write_to_file(const std::string& key, const char* value_cstr) {
		write_to_file(key, std::string(value_cstr));
	}
};

std::map<const void*, Info> decoders;
std::map<const void*, const void*> decoder_alias_map;

static Info& get_decoder(const void* ref) {
	auto alias_it = decoder_alias_map.find(ref);
	if(alias_it != decoder_alias_map.end())
		ref = alias_it->second;
	auto it = decoders.find(ref);
	assert(it != decoders.end());
	return it->second;
}

extern "C" void register_decoder_ref(const void* ref, const char* decoder_name, long sample_rate, int num_channels) {
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
	info.set_output_type(output_type, output_filename);
	info.use_data_filter_names = use_data_filter_names;
	info.data_name_filters.swap(data_filter_names);
	// reset
	use_data_filter_names = false;
	output_type = OT_null;
}

extern "C" void register_decoder_alias(const void* orig_ref, const void* alias_ref) {
	Info& info = get_decoder(orig_ref);
	info.aliases.insert(alias_ref);
	decoder_alias_map[alias_ref] = info.ref;
}

extern "C" void unregister_decoder_ref(const void* ref) {
	Info& info = get_decoder(ref);
	for(const void* alias_ref : info.aliases)
		decoder_alias_map.erase(alias_ref);
	decoders.erase(info.ref); // warning: after this call, info becomes invalid
}

extern "C" void set_data_output_null(void) {
	output_type = OT_null;
}

extern "C" void set_data_output_short_stdout(void) {
	output_type = OutputType::OT_short_stdout;
}

extern "C" void set_data_output_file(const char* fn) {
	output_type = OutputType::OT_file;
	output_filename = fn;
}

extern "C" void set_data_filter(const char** allowed_names) {
	data_filter_names.clear();
	if(!allowed_names) {
		use_data_filter_names = false;
		return;
	}
	use_data_filter_names = true;
	for(const char** ptr = allowed_names; *ptr; ++ptr) {
		const char* allowed_name = *ptr;
		data_filter_names.insert(allowed_name);
	}
}

template<typename It>
void push_data_short_stdout_T(Info& info, const char* name, int channel, It data, const It& end) {
	typedef typename std::iterator_traits<It>::value_type T;
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
void push_data_file_T(Info& info, const char* name, int channel, const It& data, const It& end) {
	assert(info.output_file);
	info.write_to_file("entry-name", name);
	if(channel >= 0)
		info.write_to_file("entry-channel", (uint8_t) channel);
	info.write_to_file("entry-data", data, end);
}

template<typename It>
void push_data_T(const void* ref, const char* name, int channel, const It& data, const It& end) {
	Info& info = get_decoder(ref);
	if(info.use_data_filter_names) {
		auto it = info.data_name_filters.find(name);
		if(it == info.data_name_filters.end())
			return;
	}
	switch(info.output_type) {
		case OutputType::OT_null:
			break;
		case OutputType::OT_short_stdout:
			push_data_short_stdout_T(info, name, channel, data, end);
			break;
		case OutputType::OT_file:
			push_data_file_T(info, name, channel, data, end);
			break;
	}
}

extern "C" void push_data_float(const void* ref, const char* name, int channel, const float* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}
extern "C" void push_data_u32(const void* ref, const char* name, int channel, const uint32_t* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}
extern "C" void push_data_u8(const void* ref, const char* name, int channel, const uint8_t* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}
extern "C" void push_data_i32(const void* ref, const char* name, int channel, const int32_t* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}
extern "C" void push_data_int(const void* ref, const char* name, int channel, const int* data, size_t len) {
	push_data_T(ref, name, channel, data, data + len);
}

void push_data_bool(const void* ref, const char* name, int channel, const std::vector<bool>& data) {
	push_data_T(ref, name, channel, data.begin(), data.end());
}

extern "C" const char* generic_itoa(uint32_t val, int base, int len) {
	assert(base >= 2);
	if(len < 0)
		len = sizeof(val) * 8;  // TODO this is just for base=2...
	static char rep[] = "0123456789abcdef";
	static char buf[33];
	assert(unsigned(len + 1) <= sizeof(buf));
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

void ArgParser::print_usage(const char* argv0) {
	std::cout << argv0 << " --in ogg_filename [--help] [--debug_out filename] [--debug_stdout]" << std::endl;
}

bool ArgParser::parse_args(int argc, const char **argv) {
	for(int i = 1; i < argc; ++i) {
		if(strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return false;
		}
		else if(strcmp(argv[i], "--in") == 0) {
			++i;
			if(i >= argc) {
				std::cerr << "missing arg after --in" << std::endl;
				print_usage(argv[0]);
				return false;
			}
			ogg_filename = argv[i];
			if(ogg_filename.empty()) {
				std::cerr << "invalid empty filename" << std::endl;
				print_usage(argv[0]);
				return false;
			}
		}
		else if(strcmp(argv[i], "--debug_out") == 0) {
			++i;
			if(i >= argc) {
				std::cerr << "missing arg after --debug_out" << std::endl;
				print_usage(argv[0]);
				return false;
			}
			set_data_output_file(argv[i]);
		}
		else if(strcmp(argv[i], "--debug_stdout") == 0) {
			set_data_output_short_stdout();
		}
		else {
			std::cerr << "unexpected arg " << i << " \"" << argv[i] << "\"" << std::endl;
			print_usage(argv[0]);
			return false;
		}
	}
	if(ogg_filename.empty()) {
		std::cerr << "need to provide --in ogg_filename" << std::endl;
		print_usage(argv[0]);
		return false;
	}
	return true;
}
