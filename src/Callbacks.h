//
//  Callbacks.h
//  ParseOggVorbis
//
//  Created by i6user on 21.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#ifndef Callbacks_h
#define Callbacks_h

#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} // to fool the Xcode indent
#endif

// ref could be eg vorbis_info in libvorbis.
// It is not an error to call this multiple times for the same ref.
// However, it is an error to call any of the other functions without calling this first.
void register_decoder_ref(void* ref, const char* decoder_name, long sample_rate, int num_channels);
void unregister_decoder_ref(void* ref);

// Name is any descriptive name.
// Channel can be -1, if it does not apply.
// data can be NULL, if no data. In that case, len is ignored.
void push_data_float(void* ref, const char* name, int channel, const float* data, size_t len);
void push_data_u32(void* ref, const char* name, int channel, const uint32_t* data, size_t len);
void push_data_i32(void* ref, const char* name, int channel, const int32_t* data, size_t len);
void push_data_int(void* ref, const char* name, int channel, const int* data, size_t len);

#if 0
{ // to keep Xcode happy
#endif
#ifdef __cplusplus
}
#endif

#endif /* Callbacks_h */
