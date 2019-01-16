//
//  main.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 07.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "Utils.hpp"


// Documentation:
// main page: https://xiph.org/vorbis/doc/
// detailed spec page: https://xiph.org/vorbis/doc/Vorbis_I_spec.html
// Go Vorbis code: https://github.com/runningwild/gorbis/tree/master/vorbis
// C# Vorbis code: https://github.com/ioctlLR/NVorbis/tree/master/NVorbis
// ref C Vorbis code: https://github.com/xiph/vorbis/tree/master/lib
// Python Vorbis code: https://github.com/susimus/ogg_vorbis/
// D Vorbis code: https://github.com/Samulus/hz/blob/master/src/lib/vorbis.d
// C++ Vorbis code: https://github.com/latelee/my_live555/blob/master/liveMedia/OggFileParser.cpp



// Page + page header is described here:
// https://xiph.org/vorbis/doc/framing.html

enum {
	HeaderFlag_Continued = 0x1,
	HeaderFlag_First = 0x2,
	HeaderFlag_Last = 0x4
};

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
	
	enum ReadHeaderResult { Ok, Eof, Error };
	ReadHeaderResult read_header(IReader* reader) {
		size_t c = reader->read(&header, sizeof(PageHeader), 1);
		if(c == 1) return ReadHeaderResult::Ok;
		if(reader->reachedEnd()) return ReadHeaderResult::Eof;
		return ReadHeaderResult::Error;
	}
	
	OkOrError read(IReader* reader) {
		CHECK(memcmp(header.capture_pattern, "OggS", 4) == 0);
		CHECK(header.stream_structure_version == 0);
		endian_swap_to_little_endian(header.absolute_granule_pos);
		endian_swap_to_little_endian(header.stream_serial_num);
		endian_swap_to_little_endian(header.page_sequence_num);
		endian_swap_to_little_endian(header.page_crc_checksum);
		
		CHECK(reader->read(segment_table, header.page_segments_num, 1) == 1);
		data_len = 0;
		for(uint8_t i = 0; i < header.page_segments_num; ++i)
			data_len += segment_table[i];
		if(header.page_segments_num > 0)
			CHECK(segment_table[header.page_segments_num - 1] != 255); // packets spanning pages not supported currently...
		CHECK(reader->read(data, data_len, 1) == 1);
		
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

struct __attribute__((packed)) VorbisIdHeader {
	// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.2. Identification header
	uint32_t vorbis_version;
	uint8_t audio_channels;
	uint32_t audio_sample_rate;
	uint32_t bitrate_maximum;
	uint32_t bitrate_nominal;
	uint32_t bitrate_minimum;
	uint8_t blocksizes_exp;
	int get_blocksize_0() const { return int(1) << (blocksizes_exp & 0x0f); }
	int get_blocksize_1() const { return int(1) << ((blocksizes_exp & 0xf0) >> 4); }
	uint8_t framing_flag;
};


struct VorbisCodebook { // used in VorbisStreamSetup
	uint16_t dimensions_;
	uint32_t num_entries_;
	bool ordered_;
	bool sparse_;
	struct Entry {
		uint32_t num_; // number of used entry
		uint8_t len_; // bitlen of codeword
		uint8_t codeword_;
		Entry() : num_(0), len_(0), codeword_(0) {}
		void init(uint32_t num, uint8_t len) {
			num_ = num; len_ = len;
			assert(len >= 1 && len <= 32); // reader.readBitsT<5>() + 1
		}
		bool unused() const { return len_ == 0; }
	};
	std::vector<Entry> entries_;
	uint8_t lookup_type_;
	double minimum_value_;
	double delta_value_;
	uint8_t value_bits_;
	bool sequence_p_;
	uint32_t num_lookup_values_;
	std::vector<uint32_t> multiplicands_;
	
	OkOrError _assignCodewords() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 3.2.1 Huffman decision tree repr
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codebook.go
		uint32_t marker[32]; // for each bitlen (len - 1)
		memset(marker, 0, sizeof(marker));
		for(Entry& entry : entries_) {
			if(entry.unused()) continue;
			assert(entry.len_ >= 1 && entry.len_ <= 32);
			uint32_t codeword = marker[entry.len_ - 1];
			CHECK((codeword >> entry.len_) == 0); // overspecified
			entry.codeword_ = codeword;
			for(uint8_t j = entry.len_; j > 0; --j) {
				if(marker[j - 1] & 1) {
					if(j == 1) {
						++marker[0];
					} else {
						marker[j - 1] = marker[j - 2] << 1;
					}
					CHECK(marker[j - 1] <= (uint32_t(1) << j)); // overspecified
					break;
				}
				++marker[j - 1];
			}
			for(uint8_t j = entry.len_ + 1; j <= 32; ++j) {
				if((marker[j - 1] >> 1) == codeword) {
					codeword = marker[j - 1];
					marker[j - 1] = marker[j - 2] << 1;
				}
				else
					break;
			}
		}
		for(uint8_t i = 0; i < 31; ++i)
			CHECK(marker[i] == (uint32_t(1) << (i + 1))); // underspecified
		CHECK(marker[31] == 0); // underspecified
		return OkOrError();
	}

	OkOrError parse(BitReader& reader) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 3.2.1
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codebook.go
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisCodebook.cs
		// https://github.com/xiph/vorbis/blob/master/lib/codebook.c
		// https://github.com/susimus/ogg_vorbis/
		// https://github.com/Samulus/hz/blob/master/src/lib/vorbis.d
		// https://github.com/latelee/my_live555/blob/master/liveMedia/OggFileParser.cpp
		CHECK(reader.readBitsT<24>() == 0x564342); // sync pattern
		dimensions_ = reader.readBitsT<16>();
		CHECK(dimensions_ > 0);
		num_entries_ = reader.readBitsT<24>();
		CHECK(num_entries_ > 0);
		entries_.resize(num_entries_);
		ordered_ = reader.readBitsT<1>();
		
		if(!ordered_) {
			sparse_ = reader.readBitsT<1>();
			if(sparse_) {
				uint32_t cur_entry_num = 0;
				for(uint32_t i = 0; i < num_entries_; ++i) {
					bool flag = reader.readBitsT<1>();
					if(flag) {
						entries_[i].init(cur_entry_num, reader.readBitsT<5>() + 1);
						++cur_entry_num;
					}
				}
			}
			else { // not sparse
				for(uint32_t i = 0; i < num_entries_; ++i)
					entries_[i].init(i, reader.readBitsT<5>() + 1);
			}
		}
		else { // ordered flag is set
			sparse_ = false; // not used
			for(uint32_t cur_entry_num = 0; cur_entry_num < num_entries_;) {
				uint8_t cur_len = reader.readBitsT<5>() + 1;
				uint32_t number = reader.readBits<uint32_t>(highest_bit(num_entries_ - cur_entry_num));
				for(uint32_t i = cur_entry_num; i < cur_entry_num + number; ++i)
					entries_[i].init(i, cur_len);
				cur_entry_num += number;
				++cur_len;
				CHECK(cur_entry_num <= num_entries_);
			}
		}
		CHECK_ERR(_assignCodewords());

		lookup_type_ = reader.readBitsT<4>();
		CHECK(lookup_type_ == 0 || lookup_type_ == 1 || lookup_type_ == 2);
		if(lookup_type_ == 0) {
			// not used
			minimum_value_ = 0; delta_value_ = 0;
			value_bits_ = 0; sequence_p_ = false;
			num_lookup_values_ = 0;
		}
		else if(lookup_type_ == 1 || lookup_type_ == 2) {
			minimum_value_ = float32_unpack(reader.readBitsT<32>());
			delta_value_ = float32_unpack(reader.readBitsT<32>());
			value_bits_ = reader.readBitsT<4>() + 1;
			sequence_p_ = reader.readBitsT<1>();
			if(lookup_type_ == 1) {
				// lookup1_values:
				// the greatest integer value for which [num_lookup_values] to the power of [codebook_dimensions] is less than or equal to [codebook_entries]’.
				num_lookup_values_ = 0;
				while(powIntExp(num_lookup_values_ + 1, dimensions_) <= num_entries_)
					++num_lookup_values_;
			}
			else
				num_lookup_values_ = num_entries_ * dimensions_;
		}
		else
			assert(false);
		multiplicands_.resize(num_lookup_values_);
		for(uint32_t i = 0; i < num_lookup_values_; ++i)
			multiplicands_[i] = reader.readBits<uint32_t>(value_bits_);
		
		CHECK(!reader.reachedEnd());
		return OkOrError();
	}
	
	int decodeScalar(BitReader& reader) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 3.2.1.
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codebook.go
		// TODO could be optimized by codeword lookup tree
		uint32_t word = 0;
		for(uint8_t len = 0; len < 32; ++len) {
			if(len > 0)
				for(Entry& entry : entries_) {
					if(entry.unused()) continue;
					if(entry.len_ == len && entry.codeword_ == word)
						return entry.num_;
				}
			word = (word << 1) | reader.readBitsT<1>();
		}
		return -1;
	}
};

struct VorbisFloor0 {
	uint8_t order;
	uint16_t rate;
	uint16_t bark_map_size;
	uint8_t amplitude_bits;
	uint8_t amplitude_offset;
	std::vector<uint8_t> books;
	
	OkOrError parse(BitReader& reader, int max_books) {
		order = reader.readBitsT<8>();
		rate = reader.readBitsT<16>();
		bark_map_size = reader.readBitsT<16>();
		amplitude_bits = reader.readBitsT<6>();
		amplitude_offset = reader.readBitsT<8>();
		int num_books = reader.readBitsT<4>() + 1;
		books.resize(num_books);
		for(int i = 0; i < num_books; ++i) {
			books[i] = reader.readBitsT<8>();
			CHECK(books[i] < max_books);
		}
		return OkOrError();
	}

	OkOrError decode(BitReader& reader, std::vector<VorbisCodebook>& codebooks, int window_len, DataRange<float>& out) {
		CHECK(false); // not implemented. but rarely used anyway?
		(void) reader; (void) codebooks; (void) window_len; (void) out; // remove warnings
		return OkOrError();
	}
};

struct VorbisFloorClass {
	uint8_t dimensions;
	uint8_t subclass;
	uint8_t masterbook;
	std::vector<int> subclass_books;
	VorbisFloorClass() : dimensions(0), subclass(0), masterbook(0) {}
};

struct VorbisFloor1 {
	std::vector<uint8_t> partition_classes;
	std::vector<VorbisFloorClass> classes;
	uint8_t multiplier;
	std::vector<uint32_t> xs;
	
	OkOrError parse(BitReader& reader) {
		int num_partitions = reader.readBitsT<5>();
		int max_class = -1;
		partition_classes.resize(num_partitions);
		for(int i = 0; i < num_partitions; ++i) {
			partition_classes[i] = reader.readBitsT<4>();
			if(partition_classes[i] > max_class)
				max_class = partition_classes[i];
		}
		
		classes.resize(max_class + 1);
		for(VorbisFloorClass& cl : classes) {
			cl.dimensions = reader.readBitsT<3>() + 1;
			cl.subclass = reader.readBitsT<2>();
			if(cl.subclass > 0)
				cl.masterbook = reader.readBitsT<8>();
			cl.subclass_books.resize(1 << cl.subclass);
			for(auto& x : cl.subclass_books)
				x = int(reader.readBitsT<8>()) - 1;
		}
		
		multiplier = reader.readBitsT<2>() + 1;
		uint8_t rangebits = reader.readBitsT<4>();
		xs.resize(2);
		xs[0] = 0;
		xs[1] = 1 << rangebits;
		for(uint8_t class_idx : partition_classes) {
			CHECK(class_idx < classes.size());
			VorbisFloorClass& cl = classes[class_idx];
			for(int j = 0; j < cl.dimensions; ++j)
				xs.push_back(reader.readBits<uint32_t>(rangebits));
		}
		return OkOrError();
	}

	OkOrError decode(BitReader& reader, std::vector<VorbisCodebook>& codebooks, int window_len, DataRange<float>& out) {
		(void) window_len; // not used
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codec.go
		// https://github.com/runningwild/gorbis/blob/master/vorbis/floor.go
		if(reader.readBitsT<1>() == 0) {
			out = DataRange<float>(); // nothing
			return OkOrError();
		}
		// Decode Y values
		typedef uint32_t y_t; // best type?
		std::vector<y_t> ys;
		{
			int range;
			switch(multiplier) {
				case 1: range = 256; break;
				case 2: range = 128; break;
				case 3: range = 86; break;
				case 4: range = 64; break;
				default: assert(false);
			}
			ys.push_back(reader.readBits<y_t>(highest_bit(range - 1)));
			ys.push_back(reader.readBits<y_t>(highest_bit(range - 1)));
			for(uint8_t class_idx : partition_classes) {
				VorbisFloorClass& cl = classes[class_idx];
				uint8_t class_dim = cl.dimensions;
				uint8_t class_bits = cl.subclass;
				uint32_t csub = (uint32_t(1) << class_bits) - 1;
				uint32_t cval = 0;
				if(class_bits > 0)
					cval = codebooks[cl.masterbook].decodeScalar(reader);
				for(int i = 0; i < class_dim; ++i) {
					int book = cl.subclass_books[cval & csub];
					cval = cval >> class_bits;
					ys.push_back((book >= 0) ? codebooks[book].decodeScalar(reader) : 0);
				}
			}
		}
		// TODO amplitude value synthesis, compute curve
		assert(false);
		return OkOrError();
	}
};

struct VorbisFloor {
	uint16_t floor_type;
	VorbisFloor0 floor0;
	VorbisFloor1 floor1;
	
	OkOrError parse(BitReader& reader, int num_codebooks) {
		floor_type = reader.readBitsT<16>();
		CHECK(floor_type == 0 || floor_type == 1);
		if(floor_type == 0)
			CHECK_ERR(floor0.parse(reader, num_codebooks));
		else if(floor_type == 1)
			CHECK_ERR(floor1.parse(reader));
		else
			assert(false);
		return OkOrError();
	}
	
	OkOrError decode(BitReader& reader, std::vector<VorbisCodebook>& codebooks, int window_len, DataRange<float>& out) {
		if(floor_type == 0)
			return floor0.decode(reader, codebooks, window_len, out);
		if(floor_type == 1)
			return floor1.decode(reader, codebooks, window_len, out);
		assert(false);
		return OkOrError();
	}
};

struct VorbisResidue {
	uint16_t type;
	uint32_t begin, end;
	uint32_t partition_size;
	uint8_t num_classifications;
	uint8_t classbook;
	std::vector<uint32_t> cascades;
	std::vector<uint8_t> books;
	
	OkOrError parse(BitReader& reader) {
		type = reader.readBitsT<16>();
		CHECK(type <= 2);
		begin = reader.readBitsT<24>();
		end = reader.readBitsT<24>();
		partition_size = reader.readBitsT<24>() + 1;
		num_classifications = reader.readBitsT<6>() + 1;
		classbook = reader.readBitsT<8>();
		
		cascades.resize(num_classifications);
		for(uint32_t& x : cascades) {
			uint32_t high_bits = 0;
			uint32_t low_bits = reader.readBitsT<3>();
			bool bit_flag = reader.readBitsT<1>();
			if(bit_flag) high_bits = reader.readBitsT<5>();
			x = high_bits * 8 + low_bits;
		}
		
		books.resize(num_classifications * 8);
		for(int i = 0; i < num_classifications; ++i) {
			for(int j = 0; j < 8; ++j) {
				if(cascades[i] & (uint32_t(1) << j))
					books[i * 8 + j] = reader.readBitsT<8>();
				else
					books[i * 8 + j] = -1; // TODO: good placeholder?
			}
		}
		
		return OkOrError();
	}
};

struct VorbisMapping {
	uint16_t type;
	struct Coupling { int magintude, angle; };
	std::vector<Coupling> couplings;
	std::vector<uint8_t> muxs;
	struct Submap { uint8_t floor, residue; };
	std::vector<Submap> submaps;
	
	OkOrError parse(BitReader& reader, int num_channels, int num_floors, int num_residues) {
		assert(num_channels > 0);
		int bits = highest_bit(num_channels - 1);
		type = reader.readBitsT<16>();
		CHECK(type == 0);
		bool flag = reader.readBitsT<1>();
		int num_submaps = 1;
		if(flag)
			num_submaps = reader.readBitsT<4>() + 1;
		if(reader.readBitsT<1>()) {
			int coupling_steps = int(reader.readBitsT<8>()) + 1;
			couplings.resize(coupling_steps);
			for(Coupling& coupling : couplings) {
				coupling.magintude = reader.readBits<uint16_t>(bits);
				coupling.angle = reader.readBits<uint16_t>(bits);
				CHECK(coupling.magintude != coupling.angle);
				CHECK(coupling.magintude < num_channels);
				CHECK(coupling.angle < num_channels);
			}
		}
		CHECK(reader.readBitsT<2>() == 0);
		
		muxs.resize(num_channels);
		if(num_submaps > 1) {
			for(uint8_t& x : muxs) {
				x = reader.readBitsT<4>();
				CHECK(x < num_submaps);
			}
		}
		
		submaps.resize(num_submaps);
		for(Submap& submap : submaps) {
			reader.readBitsT<8>(); // explicitly discarded
			submap.floor = reader.readBitsT<8>();
			CHECK(submap.floor < num_floors);
			submap.residue = reader.readBitsT<8>();
			CHECK(submap.residue < num_residues);
		}
		
		return OkOrError();
	}
};

struct VorbisModeNumber { // used in VorbisStreamSetup
	bool block_flag;
	uint16_t window_type;
	uint16_t transform_type;
	uint8_t mapping;
	// precalculated
	int blocksize;
	std::vector<float> windows;
	
	OkOrError parse(BitReader& reader, int num_mappings, VorbisIdHeader& header) {
		block_flag = reader.readBitsT<1>();
		window_type = reader.readBitsT<16>();
		CHECK(window_type == 0);
		transform_type = reader.readBitsT<16>();
		CHECK(transform_type == 0);
		mapping = reader.readBitsT<8>();
		CHECK(mapping < num_mappings);
		CHECK_ERR(precalc(header));
		return OkOrError();
	}
	
	OkOrError precalc(VorbisIdHeader& header) {
		int blocksize0 = header.get_blocksize_0();
		int blocksize1 = header.get_blocksize_1();
		blocksize = block_flag ? blocksize1 : blocksize0;
		windows.resize(block_flag ? (blocksize * 4) : blocksize);
		for(int win_idx = 0; win_idx < (block_flag ? 4 : 1); ++win_idx) {
			DataRange<float> window = _getWindow(win_idx);
			bool prev = win_idx & 1;
			bool next = win_idx & 2;
			int left = (prev ? blocksize1 : blocksize0) / 2;
			int right = (next ? blocksize1 : blocksize0) / 2;
			int left_begin = blocksize / 4 - left / 2;
			int right_begin = blocksize - blocksize / 4 - right / 2;
			for(int i = 0; i < left; ++i) {
				float x = sinf(M_PI_2 * (i + 0.5) / left);
				x *= x;
				window[left_begin + i] = sinf(M_PI_2 * x);
			}
			for(int i = left_begin + left; i < right_begin; ++i)
				window[i] = 1;
			for(int i = 0; i < right; ++i) {
				float x = sinf(M_PI_2 * (right - i - .5) / right);
				x *= x;
				window[right_begin + i] = sinf(M_PI_2 * x);
			}
		}
		return OkOrError();
	}
	
	DataRange<float> _getWindow(int idx) {
		assert(idx >= 0 && idx < (block_flag ? 4 : 1));
		return DataRange<float>(&windows[idx * blocksize], blocksize);
	}
	
	DataRange<float> getWindow(bool prev, bool next) {
		int win_idx = 0;
		if(block_flag) {
			if(next) {
				if(prev) win_idx = 3;
				else win_idx = 2;
			}
			else if(prev) { // and not next
				win_idx = 1;
			}
		}
		return _getWindow(win_idx);
	}
};

struct VorbisStreamSetup { // used in VorbisStreamInfo
	// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
	std::vector<VorbisCodebook> codebooks;
	std::vector<VorbisFloor> floors;
	std::vector<VorbisResidue> residues;
	std::vector<VorbisMapping> mappings;
	std::vector<VorbisModeNumber> modes;
	
	OkOrError parse(BitReader& reader, VorbisIdHeader& header) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisStreamDecoder.cs LoadBooks
		// https://github.com/runningwild/gorbis/blob/master/vorbis/setup_header.go
		int num_channels = header.audio_channels;
		
		// Codebooks
		{
			int count = int(reader.readBitsT<8>()) + 1;
			codebooks.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(codebooks[i].parse(reader));
			CHECK(!reader.reachedEnd());
		}

		// Time domain transforms
		{
			int count = reader.readBitsT<6>() + 1;
			for(int i = 0; i < count; ++i)
				// Just placeholders, but we expect them to be 0.
				CHECK(reader.readBitsT<16>() == 0);
			CHECK(!reader.reachedEnd());
		}

		// Floors
		{
			int count = reader.readBitsT<6>() + 1;
			floors.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(floors[i].parse(reader, (int)codebooks.size()));
			CHECK(!reader.reachedEnd());
		}

		// Residues
		{
			int count = reader.readBitsT<6>() + 1;
			residues.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(residues[i].parse(reader));
			CHECK(!reader.reachedEnd());
		}

		// Mappings
		{
			int count = reader.readBitsT<6>() + 1;
			mappings.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(mappings[i].parse(reader, num_channels, (int)floors.size(), (int)residues.size()));
			CHECK(!reader.reachedEnd());
		}

		// Modes
		{
			int count = reader.readBitsT<6>() + 1;
			modes.resize(count);
			for(int i = 0; i < count; ++i)
				CHECK_ERR(modes[i].parse(reader, (int)mappings.size(), header));
			CHECK(!reader.reachedEnd());
		}

		CHECK(reader.readBitsT<1>() == 1); // framing
		CHECK(!reader.reachedEnd()); // not yet...
		// Check that we are at the end now.
		CHECK(reader.readBitsT<8>() == 0);
		CHECK(reader.reachedEnd());
		return OkOrError();
	}
};

struct VorbisStreamInfo {
	VorbisIdHeader header;
	VorbisStreamSetup setup;
	uint32_t packet_counts_;

	VorbisStreamInfo() : packet_counts_(0) {}
	
	OkOrError parse_audio(BitReader& reader) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codec.go
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisStreamDecoder.cs
		CHECK(reader.readBitsT<1>() == 0);
		assert(setup.modes.size() > 0);
		int mode_idx = reader.readBits<uint16_t>(highest_bit(setup.modes.size() - 1));
		VorbisModeNumber& mode = setup.modes[mode_idx];
		VorbisMapping& mapping = setup.mappings[mode.mapping];
		
		// Get window.
		bool prev_window_flag = false, next_window_flag = false;
		if(mode.block_flag) {
			prev_window_flag = reader.readBitsT<1>();
			next_window_flag = reader.readBitsT<1>();
		}
		DataRange<float> window = mode.getWindow(prev_window_flag, next_window_flag);
		
		// Floor curves.
		for(int channel = 0; channel < header.audio_channels; ++channel) {
			uint8_t submap_number = mapping.muxs[channel];
			uint8_t floor_number = mapping.submaps[submap_number].floor;
			VorbisFloor& floor = setup.floors[floor_number];
			DataRange<float> out;
			CHECK_ERR(floor.decode(reader, setup.codebooks, (int)window.size(), out));
		}
		
		// Residues.
		// TODO residue decode
		assert(false);
		
		return OkOrError();
	}
};


struct VorbisPacket {
	VorbisStreamInfo* stream;
	uint8_t* data;
	uint32_t data_len; // never more than 256*256
	
	OkOrError parse_id() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.2
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 1);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		CHECK(data_len - 7 == sizeof(VorbisIdHeader));
		VorbisIdHeader& header = stream->header;
		memcpy(&header, &data[7], sizeof(VorbisIdHeader));
		std::cout << "vorbis version: " << header.vorbis_version
		<< ", channels: " << (int) header.audio_channels
		<< ", sample rate: " << header.audio_sample_rate << std::endl;
		CHECK(header.framing_flag == 1);
		return OkOrError();
	}
	
	OkOrError parse_comment() {
		// https://xiph.org/vorbis/doc/v-comment.html
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.3
		// Meta tags, etc.
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 3);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		// ignore for now...
		return OkOrError();
	}

	OkOrError parse_setup() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 5);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		ConstDataReader reader(data + 7, data_len - 7);
		BitReader bitReader(&reader);
		CHECK_ERR(stream->setup.parse(bitReader, stream->header));
		CHECK(reader.reachedEnd());
		return OkOrError();
	}
	
	OkOrError parse_audio() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codec.go
		ConstDataReader reader(data, data_len);
		BitReader bitReader(&reader);
		return stream->parse_audio(bitReader);
	}
};


struct OggReader {
	char buffer_[255];
	Page buffer_page_;
	std::map<uint32_t, VorbisStreamInfo> streams_;
	size_t packet_counts_;
	std::shared_ptr<IReader> reader_;

	OggReader() : packet_counts_(0) {}
	
	OkOrError full_read(const char* filename) {
		CHECK_ERR(_open_file(filename));
		size_t page_count = 0;
		while(true) {
			Page::ReadHeaderResult res = buffer_page_.read_header(reader_.get());
			if(res == Page::ReadHeaderResult::Ok)
				CHECK_ERR(_read_page());
			else if(res == Page::ReadHeaderResult::Eof)
				break;
			else
				return OkOrError("read error");
			++page_count;
		}
		std::cout << "page count: " << page_count << std::endl;
		std::cout << "packets count: " << packet_counts_ << std::endl;
		return OkOrError();
	}
	
	OkOrError _open_file(const char* fn) {
		reader_ = std::make_shared<FileReader>(fn);
		CHECK_ERR(reader_->isValid());
		return OkOrError();
	}
	
	OkOrError _read_page() {
		CHECK_ERR(buffer_page_.read(reader_.get()));
		if(buffer_page_.header.header_type_flag & HeaderFlag_First) {
			CHECK(streams_.find(buffer_page_.header.stream_serial_num) == streams_.end());
			streams_[buffer_page_.header.stream_serial_num] = VorbisStreamInfo();
			std::cout << "new stream: " << buffer_page_.header.stream_serial_num << std::endl;
		}
		CHECK(streams_.find(buffer_page_.header.stream_serial_num) != streams_.end());
		VorbisStreamInfo& stream = streams_[buffer_page_.header.stream_serial_num];
		
		// pack packets: join seg table with size 255 and first with <255, each is one packet
		size_t offset = 0;
		uint32_t len = 0;
		for(uint8_t segment_i = 0; segment_i < buffer_page_.header.page_segments_num; ++segment_i) {
			len += buffer_page_.segment_table[segment_i];
			if(buffer_page_.segment_table[segment_i] < 255) {
				// new packet
				// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
				// https://github.com/runningwild/gorbis/blob/master/vorbis/codec.go
				// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisStreamDecoder.cs
				VorbisPacket packet;
				packet.stream = &stream;
				packet.data = buffer_page_.data + offset;
				packet.data_len = len;
				if(stream.packet_counts_ == 0)
					CHECK_ERR(packet.parse_id());
				else if(stream.packet_counts_ == 1)
					CHECK_ERR(packet.parse_comment());
				else if(stream.packet_counts_ == 2)
					CHECK_ERR(packet.parse_setup());
				else
					CHECK_ERR(packet.parse_audio());
				++stream.packet_counts_;
				++packet_counts_;
				offset += len;
				len = 0;
			}
		}
		assert(len == 0 && offset == buffer_page_.data_len);
		
		if(buffer_page_.header.header_type_flag & HeaderFlag_Last) {
			streams_.erase(buffer_page_.header.stream_serial_num);
		}
		
		return OkOrError();
	}
};


int main(int argc, const char* argv[]) {
	OggReader reader;
	OkOrError result = reader.full_read((argc >= 2) ? argv[1] : "");
	if(result.is_error_)
		std::cerr << "error: " << result.err_msg_ << std::endl;
	else
		std::cout << "ok" << std::endl;
	return (int) result.is_error_;
}
