//
//  Callbacks.h
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 21.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

/*
 This file was intended to provide simple hooks into existing code.
 We patched libvorbis with a few calls to these hook functions to extract certain
 intermedia decode information/state, see `tests/libvorbis-standalone`.
 We also added similar hooks to our own implementation, to allow for an easy comparison.
 This is being tested by `tests/compare-debug-out.py`.
 */

#ifndef Callbacks_h
#define Callbacks_h

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
#include <vector>
#include <string>

extern "C" {
#endif
#if 0
} // to fool the Xcode indent
#endif

// ref could be eg vorbis_info in libvorbis.
// It is not an error to call this multiple times for the same ref.
// However, it is an error to call any of the other functions without calling this first.
void register_decoder_ref(const void* ref, const char* decoder_name, long sample_rate, int num_channels);
void unregister_decoder_ref(const void* ref);
// Such that alias_ref is also a valid ref. orig_ref needs to be registered beforehand.
void register_decoder_alias(const void* orig_ref, const void* alias_ref);

// This setting will be used for the next registered decoder.
void set_data_output_null(void);
void set_data_output_short_stdout(void);
void set_data_output_file(const char* fn);

// Name is any descriptive name.
// Channel can be -1, if it does not apply.
// data can be NULL, if no data. In that case, len is ignored.
void push_data_float(const void* ref, const char* name, int channel, const float* data, size_t len);
void push_data_u32(const void* ref, const char* name, int channel, const uint32_t* data, size_t len);
void push_data_u8(const void* ref, const char* name, int channel, const uint8_t* data, size_t len);
void push_data_i32(const void* ref, const char* name, int channel, const int32_t* data, size_t len);
void push_data_int(const void* ref, const char* name, int channel, const int* data, size_t len);

// General utilities.
const char* generic_itoa(uint32_t val, int base, int len);

#if 0
{ // to keep Xcode happy
#endif
#ifdef __cplusplus
}

// C++ only
void push_data_bool(const void* ref, const char* name, int channel, const std::vector<bool>& data);

struct ArgParser {
	std::string ogg_filename;
	void print_usage(const char* argv0);
	bool parse_args(int argc, const char** argv);
};
#endif

#endif /* Callbacks_h */
