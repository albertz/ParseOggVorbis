//
//  ParseOggVorbis.hpp
//  ParseOggVorbis
//
//  Created by i6user on 24.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#ifndef ParseOggVorbis_h
#define ParseOggVorbis_h


#include <bitset>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <algorithm>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "Utils.hpp"
#include "inverse_db_table.h"
#include "mdct.h"
#include "Callbacks.h"


// Documentation:
// main page: https://xiph.org/vorbis/doc/
// detailed spec page: https://xiph.org/vorbis/doc/Vorbis_I_spec.html
// Go Vorbis code: https://github.com/runningwild/gorbis/tree/master/vorbis
// C# Vorbis code: https://github.com/ioctlLR/NVorbis/tree/master/NVorbis
// ref C Vorbis code: https://github.com/xiph/vorbis/tree/master/lib
// Python Vorbis code: https://github.com/susimus/ogg_vorbis/
// D Vorbis code: https://github.com/Samulus/hz/blob/master/src/lib/vorbis.d
// C++ Vorbis code: https://github.com/latelee/my_live555/blob/master/liveMedia/OggFileParser.cpp
// Java Vorbis code: https://github.com/kazutomi/xiphqt/blob/master/rhea/src/com/meviatronic/zeus/castor/VorbisDecoder.java


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
	int64_t absolute_granule_pos; // end PCM sample position of the last packet completed on that page
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
		header.page_crc_checksum = calculated_crc;
		return OkOrError();
	}
};

struct __attribute__((packed)) VorbisIdHeader { // used in VorbisStream
	// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.2. Identification header
	uint32_t vorbis_version;
	uint8_t audio_channels;
	uint32_t audio_sample_rate;
	uint32_t bitrate_maximum;
	uint32_t bitrate_nominal;
	uint32_t bitrate_minimum;
	uint8_t blocksizes_exp;
	// range of blocksize_0|1 is [1,...,2^15] (both inclusive)
	uint16_t get_blocksize_0() const { return uint16_t(1) << (blocksizes_exp & 0x0f); }
	uint16_t get_blocksize_1() const { return uint16_t(1) << ((blocksizes_exp & 0xf0) >> 4); }
	uint8_t framing_flag;
};


struct VorbisCodebook { // used in VorbisStreamSetup
	uint16_t dimensions_;
	uint32_t num_entries_;
	bool ordered_;
	bool sparse_;
	struct Entry {
		uint32_t idx_; // set in _assignCodewords()
		uint32_t num_; // number of used entry
		uint8_t len_; // bitlen of codeword. if used, 1 <= len_ <= 32.
		uint32_t codeword_; // calculated via _assignCodewords()
		Entry() : idx_(0), num_(0), len_(0), codeword_(0) {}
		void init(uint32_t idx, uint32_t num, uint8_t len) {
			idx_ = idx; num_ = num; len_ = len;
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
	std::vector<float> lookup_table_; // calculated via _buildVQ(). size: flat num_entries_, dimensions_

	OkOrError _assignCodewords() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 3.2.1 Huffman decision tree repr
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codebook.go
		uint32_t marker[32]; // for each bitlen (len - 1)
		memset(marker, 0, sizeof(marker));
		for(Entry& entry : entries_) {
			if(entry.unused()) continue;
			CHECK(entry.len_ >= 1 && entry.len_ <= 32);
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

	void _buildVQ() {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 3.2.1 VQ lookup table vector representation
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codebook.go
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisCodebook.cs
		if(lookup_type_ == 0) return;
		lookup_table_.resize(num_entries_ * dimensions_);
		if(lookup_type_ == 1) {
			for(uint32_t entry_idx = 0; entry_idx < num_entries_; ++entry_idx) {
				double last = 0;
				uint32_t index_divisor = 1;
				for(uint16_t dim = 0; dim < dimensions_; ++dim) {
					uint32_t mult_offset = (entry_idx / index_divisor) % multiplicands_.size();
					lookup_table_[entry_idx * dimensions_ + dim] = multiplicands_[mult_offset] * delta_value_ + minimum_value_ + last;
					if(sequence_p_)
						last = lookup_table_[entry_idx * dimensions_ + dim];
					index_divisor *= multiplicands_.size();
				}
			}
		}
		else if(lookup_type_ == 2) {
			assert(lookup_table_.size() == multiplicands_.size());
			uint32_t offset = 0; // idx both for lookup_table_ and multiplicands_
			for(uint32_t entry_idx = 0; entry_idx < num_entries_; ++entry_idx) {
				double last = 0;
				for(uint16_t dim = 0; dim < dimensions_; ++dim) {
					lookup_table_[offset] = multiplicands_[offset] * delta_value_ + minimum_value_ + last;
					if(sequence_p_)
						last = lookup_table_[offset];
					++offset;
				}
			}
		}
		else assert(false); // invalid lookup_type_
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
						entries_[cur_entry_num].init(cur_entry_num, i, reader.readBitsT<5>() + 1);
						++cur_entry_num;
					}
				}
				entries_.resize(cur_entry_num);
			}
			else { // not sparse
				for(uint32_t i = 0; i < num_entries_; ++i)
					entries_[i].init(i, i, reader.readBitsT<5>() + 1);
			}
		}
		else { // ordered flag is set
			sparse_ = false; // not used
			uint8_t cur_len = reader.readBitsT<5>() + 1;
			uint32_t cur_entry_num = 0;
			for(; cur_entry_num < num_entries_;) {
				uint32_t number = reader.readBits<uint32_t>(highest_bit(num_entries_ - cur_entry_num));
				for(uint32_t i = cur_entry_num; i < cur_entry_num + number; ++i)
					entries_[i].init(i, i, cur_len);
				cur_entry_num += number;
				CHECK(cur_entry_num <= num_entries_);
				++cur_len;
			}
			CHECK(cur_entry_num == num_entries_);
		}
		CHECK_ERR(_assignCodewords());

		// VQ lookup table vector representation
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
		else CHECK(false); // invalid type
		multiplicands_.resize(num_lookup_values_);
		for(uint32_t i = 0; i < num_lookup_values_; ++i)
			multiplicands_[i] = reader.readBits<uint32_t>(value_bits_);
		_buildVQ();

		CHECK(!reader.reachedEnd());
		return OkOrError();
	}

	uint32_t decodeScalar(BitReader& reader) const {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 3.2.1.
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codebook.go
		// TODO could be optimized by codeword lookup tree
		uint32_t word = 0;
		for(uint8_t len = 0; len < 32; ++len) {
			if(len > 0)
				for(const Entry& entry : entries_) {
					assert(!entry.unused());
					if(entry.len_ == len && entry.codeword_ == word)
						return entry.num_;
				}
			word = (word << 1) | reader.readBitsT<1>();
		}
		assert(false); // should not be possible, because we checked in _assignCodewords that we are fully specified
		return uint32_t(-1);
	}

	// Returns vector of size dimensions_, or empty if invalid.
	DataRange<const float> decodeVector(BitReader& reader) const {
		uint32_t idx = decodeScalar(reader);
		if(lookup_type_ == 0) return DataRange<const float>(); // actually this is invalid
		if(idx >= num_entries_) return DataRange<const float>(); // invalid idx
		uint32_t offset = idx * dimensions_;
		assert(offset + dimensions_ <= lookup_table_.size());
		return DataRange<const float>(&lookup_table_[offset], dimensions_);
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

	OkOrError decode(BitReader& reader, const std::vector<VorbisCodebook>& codebooks, DataRange<float>& out, bool& use_output) const {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 6.2.2
		CHECK(false); // not implemented. but rarely used anyway?
		(void) reader; (void) codebooks; (void) out; (void) use_output; // remove warnings
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
	typedef uint32_t x_t;
	std::vector<x_t> xs;
	std::vector<size_t> xs_sorted_idx;
	std::vector<x_t> xs_sorted;

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
				xs.push_back(reader.readBits<x_t>(rangebits));
		}

		// need sorted xs later in decode
		xs_sorted_idx.resize(xs.size());
		for(size_t i = 0; i < xs.size(); ++i)
			xs_sorted_idx[i] = i;
		std::sort(
				  xs_sorted_idx.begin(), xs_sorted_idx.end(),
				  [&](const size_t& a, const size_t& b) {
					  return xs[a] < xs[b];
				  });
		xs_sorted.resize(xs.size());
		for(size_t i = 0; i < xs.size(); ++i)
			xs_sorted[i] = xs[xs_sorted_idx[i]];
		return OkOrError();
	}

	OkOrError decode(BitReader& reader, const std::vector<VorbisCodebook>& codebooks, DataRange<float>& out, bool& use_output) const {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 7.2.3
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codec.go
		// https://github.com/runningwild/gorbis/blob/master/vorbis/floor.go
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisFloor.cs
		if(reader.readBitsT<1>() == 0) { // 7.2.3
			use_output = false; // nothing, no audio. this is valid
			return OkOrError();
		}
		use_output = true;

		typedef uint32_t y_t;
		y_t range;
		switch(multiplier) {
			case 1: range = 256; break;
			case 2: range = 128; break;
			case 3: range = 86; break;
			case 4: range = 64; break;
			default: CHECK(false); // invalid multiplier
		}

		// Decode Y values (7.2.3)
		std::vector<y_t> ys;
		{
			ys.push_back(reader.readBits<y_t>(highest_bit(range - 1)));
			ys.push_back(reader.readBits<y_t>(highest_bit(range - 1)));
			for(uint8_t class_idx : partition_classes) {
				const VorbisFloorClass& cl = classes[class_idx];
				uint8_t class_dim = cl.dimensions;
				uint8_t class_bits = cl.subclass;
				uint32_t csub = (uint32_t(1) << class_bits) - 1;
				uint32_t cval = 0;
				if(class_bits > 0)
					cval = codebooks[cl.masterbook].decodeScalar(reader);
				for(int i = 0; i < class_dim; ++i) {
					CHECK((cval & csub) < cl.subclass_books.size());
					int book = cl.subclass_books[cval & csub];
					cval = cval >> class_bits;
					ys.push_back((book >= 0) ? codebooks[book].decodeScalar(reader) : 0);
				}
			}
		}
		push_data_u32(this, "floor1 ys", -1, &ys[0], ys.size());
		CHECK(ys.size() == xs.size());

		// Compute curves (7.2.4).
		// Step 1: Amplitude value synthesis (7.2.4).
		std::vector<bool> step2_flag;
		step2_flag.resize(xs.size());
		step2_flag[0] = true;
		step2_flag[1] = true;
		std::vector<y_t> final_ys;
		final_ys.resize(xs.size());
		final_ys[0] = ys[0];
		final_ys[1] = ys[1];
		for(size_t i = 2; i < xs.size(); ++i) {
			size_t low_idx = low_neighbor(xs, i);
			size_t high_idx = high_neighbor(xs, i);
			y_t predicted = render_point(xs[low_idx], final_ys[low_idx], xs[high_idx], final_ys[high_idx], xs[i]);
			y_t val = ys[i];
			CHECK(predicted <= range);
			y_t high_room = range - predicted;
			y_t low_room = predicted;
			y_t room = std::min(high_room, low_room) * 2;
			if(val == 0) {
				step2_flag[i] = false;
				final_ys[i] = predicted;
			} else {
				step2_flag[low_idx] = true;
				step2_flag[high_idx] = true;
				step2_flag[i] = true;
				if(val >= room) {
					if(high_room > low_room)
						final_ys[i] = val - low_room + predicted;
					else
						final_ys[i] = predicted - val + high_room - 1;
				} else {
					if(val % 2 == 1)
						final_ys[i] = predicted - (val + 1) / 2;
					else
						final_ys[i] = predicted + val / 2;
				}
			}
		}
		push_data_u32(this, "floor1 final_ys", -1, &final_ys[0], final_ys.size());
		push_data_bool(this, "floor1 step2_flag", -1, step2_flag);

		// Step 2: curve synthesis (7.2.4)
		// Need sorted xs, final_ys, step2_flag, ascending by the values in xs.
		// We have prepared xs_sorted_idx for that.
		std::vector<y_t> final_ys_sorted(xs.size());
		std::vector<bool> step2_flag_sorted(xs.size());
		for(size_t i = 0; i < xs.size(); ++i)
			final_ys_sorted[i] = final_ys[xs_sorted_idx[i]];
		for(size_t i = 0; i < xs.size(); ++i)
			step2_flag_sorted[i] = step2_flag[xs_sorted_idx[i]];
		x_t lx = 0, hx = 0;
		y_t ly = final_ys_sorted[0] * multiplier, hy = 0;
		std::vector<y_t> floor(out.size());
		for(size_t i = 1; i < xs.size(); ++i) {
			if(step2_flag_sorted[i]) {
				hx = xs_sorted[i];
				hy = final_ys_sorted[i] * multiplier;
				render_line(lx, ly, hx, hy, floor);
				lx = hx; ly = hy;
			}
		}
		if(hx < out.size())
			render_line(hx, hy, out.size(), hy, floor);
		push_data_u32(this, "floor1 floor", -1, &floor[0], floor.size());
		for(uint16_t i = 0; i < out.size(); ++i) {
			CHECK(floor[i] < 256); // inverse_db_table len
			out[i] = inverse_db_table[floor[i]];
		}
		return OkOrError();
	}
};

struct VorbisFloor {
	uint16_t floor_type;
	VorbisFloor0 floor0;
	VorbisFloor1 floor1;

	OkOrError parse(BitReader& reader, int num_codebooks) {
		floor_type = reader.readBitsT<16>();
		if(floor_type == 0)
			CHECK_ERR(floor0.parse(reader, num_codebooks));
		else if(floor_type == 1)
			CHECK_ERR(floor1.parse(reader));
		else
			CHECK(false); // invalid floor type
		return OkOrError();
	}

	OkOrError decode(BitReader& reader, const std::vector<VorbisCodebook>& codebooks, DataRange<float>& out, bool& use_output) const {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.3.2
		if(floor_type == 0)
			CHECK_ERR(floor0.decode(reader, codebooks, out, use_output));
		else if(floor_type == 1)
			CHECK_ERR(floor1.decode(reader, codebooks, out, use_output));
		else
			CHECK(false); // invalid floor type
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
	typedef uint8_t book_t;
	std::vector<book_t> books;

	OkOrError parse(BitReader& reader) {
		type = reader.readBitsT<16>();
		CHECK(type <= 2);
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 8.6.1. header decode
		begin = reader.readBitsT<24>();
		end = reader.readBitsT<24>();
		CHECK(begin <= end);
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
					books[i * 8 + j] = book_t(-1);
			}
		}

		return OkOrError();
	}

	uint32_t getDecodeLen(uint32_t window_len) const {
		uint32_t decode_len = window_len / 2;
		return decode_len;
	}

	OkOrError decode(BitReader& reader, const std::vector<VorbisCodebook>& codebooks, uint8_t num_channel, const std::vector<bool>& channel_used, uint32_t decode_len, std::vector<std::vector<float>>& out, int type=-1) const {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.3.4. residue decode
		// 8.6.2. packet decode
		// https://github.com/runningwild/gorbis/blob/master/vorbis/residue.go
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisResidue.cs
		// actual_size = decode_len = window_len / 2 = blocksize / 2.
		// Allow to overwrite type, because type==2 reduces to type=1.
		if(type < 0)
			type = this->type;
		CHECK(type >= 0 && type <= 2);
		CHECK(num_channel > 0);
		CHECK(channel_used.size() == num_channel);
		CHECK(out.size() == num_channel);
		for(uint8_t i = 0; i < num_channel; ++i)
			CHECK(out[i].size() == decode_len);
		if(type == 2) {
			std::vector<std::vector<float>> tmp_out(1);
			tmp_out[0].resize(num_channel * decode_len, 0);
			std::vector<bool> tmp_channel_used{true};
			CHECK_ERR(decode(reader, codebooks, 1, tmp_channel_used, num_channel * decode_len, tmp_out, 1));
			for(uint8_t j = 0; j < num_channel; ++j)
				for(uint32_t i = 0; i < decode_len; ++i)
					out[j][i] = tmp_out[0][j + num_channel * i];
			return OkOrError();
		}
		CHECK(type == 0 || type == 1);
		// incorrect in documentation, we want to limit by decode_len, thus min
		uint32_t limit_begin = std::min(begin, decode_len);
		uint32_t limit_end = std::min(end, decode_len);
		CHECK(limit_begin <= limit_end);
		CHECK(classbook < codebooks.size());
		const VorbisCodebook& class_codebook = codebooks[classbook];
		uint16_t classwords_per_codeword = class_codebook.dimensions_;
		uint32_t n_to_read = limit_end - limit_begin;
		if(n_to_read == 0)
			return OkOrError();
		uint32_t partitions_to_read = n_to_read / partition_size;

		uint32_t classification_count_per_channel = partitions_to_read + classwords_per_codeword;
		typedef uint8_t classification_t;
		std::vector<classification_t> classifications(num_channel * classification_count_per_channel);
		for(uint8_t pass = 0; pass < 8; ++pass) {
			uint32_t partition_count = 0;
			while(partition_count < partitions_to_read) {
				if(pass == 0) {
					for(uint8_t j = 0; j < num_channel; ++j) {
						if(channel_used[j]) {
							uint32_t temp = class_codebook.decodeScalar(reader);
							for(uint16_t i = classwords_per_codeword; i > 0; --i) {
								classifications[j * classification_count_per_channel + i - 1 + partition_count] = temp % num_classifications;
								temp /= num_classifications;
							}
						}
					}
				}
				for(uint16_t i = 0; i < classwords_per_codeword && partition_count < partitions_to_read; ++i) {
					for(uint8_t j = 0; j < num_channel; ++j) {
						if(channel_used[j]) {
							classification_t vq_class = classifications[j * classification_count_per_channel + partition_count];
							uint8_t vq_book = books[uint16_t(vq_class) * 8 + pass];
							if(vq_book != book_t(-1)) {
								const VorbisCodebook& vq_codebook = codebooks[vq_book];
								std::vector<float>& v = out[j];
								uint32_t offset = limit_begin + partition_count * partition_size;
								if(type == 0) {
									// 8.6.3. format 0 specifics
									uint32_t step = partition_size / vq_codebook.dimensions_;
									for(uint32_t k = 0; k < step; ++k) {
										DataRange<const float> temp = vq_codebook.decodeVector(reader);
										CHECK(temp.size() > 0); CHECK(temp.size() == vq_codebook.dimensions_);
										for(uint16_t l = 0; l < vq_codebook.dimensions_; ++l)
											v[offset + k + l * step] += temp[l];
									}
								}
								else if(type == 1) {
									// 8.6.4. format 1 specifics
									for(uint32_t k = 0; k < partition_size;) {
										DataRange<const float> temp = vq_codebook.decodeVector(reader);
										CHECK(temp.size() > 0); CHECK(temp.size() == vq_codebook.dimensions_);
										for(uint32_t l = 0; l < vq_codebook.dimensions_; ++l, ++k)
											v[offset + k] += temp[l];
									}
								}
								else CHECK(false); // invalid type
							}
						}
						++partition_count;
					}
				}
			}
		}
		return OkOrError();
	}
};

struct VorbisMapping {
	uint16_t type;
	struct Coupling { int magintude, angle; };
	std::vector<Coupling> couplings;
	std::vector<uint8_t> muxs; // channel -> submap idx
	struct Submap { uint8_t floor, residue; };
	std::vector<Submap> submaps;

	OkOrError parse(BitReader& reader, int num_channels, int num_floors, int num_residues) {
		CHECK(num_channels > 0);
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
	bool block_flag; // == long window
	uint16_t window_type;
	uint16_t transform_type;
	uint8_t mapping;
	// precalculated
	uint16_t blocksize;
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
		uint16_t blocksize0 = header.get_blocksize_0();
		uint16_t blocksize1 = header.get_blocksize_1();
		blocksize = block_flag ? blocksize1 : blocksize0;
		windows.resize(block_flag ? (blocksize * 4) : blocksize); // either 4 windows, or a single one
		for(int win_idx = 0; win_idx < (block_flag ? 4 : 1); ++win_idx) {
			DataRange<float> window = _getWindow(win_idx);
			bool prev = win_idx & 1;
			bool next = win_idx & 2;
			uint16_t left = (prev ? blocksize1 : blocksize0) / 2;
			uint16_t right = (next ? blocksize1 : blocksize0) / 2;
			uint16_t left_begin = blocksize / 4 - left / 2;
			uint16_t right_begin = blocksize - blocksize / 4 - right / 2;
			for(int i = 0; i < left; ++i) {
				float x = sinf(M_PI_2 * (i + 0.5) / left);
				window[left_begin + i] = sinf(M_PI_2 * x * x);
			}
			for(int i = left_begin + left; i < right_begin; ++i)
				window[i] = 1;
			for(int i = 0; i < right; ++i) {
				float x = sinf(M_PI_2 * (right - i - .5) / right);
				window[right_begin + i] = sinf(M_PI_2 * x * x);
			}
		}
		return OkOrError();
	}

	DataRange<float> _getWindow(int idx) {
		assert(idx >= 0 && idx < (block_flag ? 4 : 1));
		return DataRange<float>(&windows[idx * blocksize], blocksize);
	}

	DataRange<const float> _getWindow(int idx) const {
		assert(idx >= 0 && idx < (block_flag ? 4 : 1));
		return DataRange<const float>(&windows[idx * blocksize], blocksize);
	}

	DataRange<const float> getWindow(bool prev, bool next) const {
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

struct VorbisStreamSetup { // used in VorbisStream
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

struct ParseCallbacks {
	// Returning false means to stop.
	virtual bool gotHeader(const VorbisIdHeader& header) { return true; }
	// TODO gotComments ...
	virtual bool gotSetup(const VorbisStreamSetup& setup) { return true; }
	virtual bool gotPcmData(const std::vector<DataRange<const float>>& channelPcms) { return true; }
	virtual bool gotEof() { return true; }
};

struct VorbisStreamDecodeState {
	std::vector<std::vector<float>> pcm_buffer; // for each channel
	uint32_t pcm_offset; // where to start adding next
	uint16_t prev_second_half_window_offset; // offset to pcm_offset

	VorbisStreamDecodeState() : pcm_offset(0), prev_second_half_window_offset(0) {}
	void init(uint8_t num_channels, uint32_t pcm_buffer_size) {
		pcm_buffer.resize(num_channels);
		size_t capacity = 0;
		for(uint8_t i = 0; i < num_channels; ++i) {
			pcm_buffer[i].resize(pcm_buffer_size);
			if(pcm_buffer[i].capacity() > capacity)
				capacity = pcm_buffer[i].capacity();
		}
		// Just use the full capacity. It does not make much sense to not use it.
		for(uint8_t i = 0; i < num_channels; ++i)
			pcm_buffer[i].resize(capacity);
	}
	OkOrError addPcmFrame(uint8_t channel, DataRange<const float> new_pcm, DataRange<const float> window) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		// 1.3.2. Decode Procedure
		CHECK(channel < pcm_buffer.size());
		CHECK(new_pcm.size() == window.size());
		CHECK(pcm_offset + window.size() <= pcm_buffer[channel].size());
		for(uint32_t i = 0; i < new_pcm.size(); ++i)
			pcm_buffer[channel][pcm_offset + i] += new_pcm[i] * window[i];
		return OkOrError();
	}
	OkOrError advancePcmOffset(ParseCallbacks& callbacks, uint32_t prev_win_size, uint32_t cur_win_size, uint32_t next_win_size) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		// 1.3.2. Decode Procedure
		// finished: audio between center of the previous frame and the center of the current frame
		// amount: window_blocksize(previous_window)/4+window_blocksize(current_window)/4 (doc wrong?)
		// Data is not returned from the first frame
		uint8_t num_channels = pcm_buffer.size();
		uint32_t pcm_cur_second_half_window_offset = pcm_offset + cur_win_size / 2;
		if(prev_win_size > 0) {
			uint32_t pcm_prev_second_half_window_offset = pcm_offset + prev_second_half_window_offset;
			CHECK(pcm_prev_second_half_window_offset < pcm_cur_second_half_window_offset);
			uint32_t num_frames = pcm_cur_second_half_window_offset - pcm_prev_second_half_window_offset;
			std::vector<DataRange<const float>> channelPcms(num_channels);
			for(uint8_t channel = 0; channel < num_channels; ++channel) {
				channelPcms[channel] = DataRange<const float>(
					&pcm_buffer[channel][pcm_offset + prev_second_half_window_offset], num_frames);
				push_data_float(this, "pcm", channel, channelPcms[channel].begin(), channelPcms[channel].size());
			}
			CHECK(callbacks.gotPcmData(channelPcms));
		}

		int32_t next_pcm_offset = int32_t(pcm_offset) + (int32_t(cur_win_size) / 4) * 3 - (int32_t(next_win_size) / 4) * 1;
		// Check whether we need to move the data to have enough room for the next window.
		// We need to keep cur_win_size / 2 of the recent frames.
		if(next_pcm_offset + next_win_size >= pcm_buffer[0].size()) {
			// Move to the left.
			// Keep half of the current window.
			int32_t needed_offset = int32_t(pcm_offset) + cur_win_size / 2 - next_pcm_offset;
			if(needed_offset < 0)
				pcm_cur_second_half_window_offset = 0; // No extra space needed.
			else
				pcm_cur_second_half_window_offset = needed_offset; // Need extra space.
			uint32_t delete_start_offset = pcm_cur_second_half_window_offset + cur_win_size / 2;
			for(uint8_t channel = 0; channel < num_channels; ++channel) {
				memmove(&pcm_buffer[channel][pcm_cur_second_half_window_offset], &pcm_buffer[channel][pcm_offset + cur_win_size / 2], (cur_win_size / 2) * sizeof(float));
				memset(&pcm_buffer[channel][delete_start_offset], 0, (pcm_buffer[channel].size() - delete_start_offset) * sizeof(float));
			}
			next_pcm_offset = 0;
		}
		else if(next_pcm_offset < 0) { // possible if short window and next is long window
			uint32_t extra_room_needed = uint32_t(-next_pcm_offset);
			CHECK(extra_room_needed > pcm_offset); // expect to move to the right
			pcm_cur_second_half_window_offset += extra_room_needed;
			for(uint8_t channel = 0; channel < num_channels; ++channel) {
				memmove(&pcm_buffer[channel][extra_room_needed], &pcm_buffer[channel][pcm_offset], cur_win_size * sizeof(float));
				memset(&pcm_buffer[channel][0], 0, extra_room_needed * sizeof(float));
			}
			next_pcm_offset = 0;
		}
		CHECK(pcm_cur_second_half_window_offset >= next_pcm_offset);
		prev_second_half_window_offset = pcm_cur_second_half_window_offset - next_pcm_offset;
		pcm_offset = next_pcm_offset;
		return OkOrError();
	}
};

struct VorbisStream {
	VorbisIdHeader header;
	VorbisStreamSetup setup;
	uint32_t packet_counts_;
	uint32_t audio_packet_counts_;
	VorbisStreamDecodeState decode_state;
	Mdct mdct[2];

	VorbisStream() : packet_counts_(0), audio_packet_counts_(0) {}

	OkOrError parse_audio(BitReader& reader, VorbisStreamDecodeState& state, ParseCallbacks& callbacks) const {
		// By design, this is a const function, because we will not modify any of the header or the setup.
		// However, we will modify the decode state, which remembers things like the PCM position,
		// and recent decoded PCM, which we need for the windowing.
		// That is why we pass in the decode state as a writeable ref.

		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		// 1.3.2. Decode Procedure (very high level)
		// 4.3 Audio packet decode and synthesis
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codec.go
		// https://github.com/ioctlLR/NVorbis/blob/master/NVorbis/VorbisStreamDecoder.cs
		push_data_u8(this, "start_audio_packet", -1, nullptr, 0);
		CHECK(reader.readBitsT<1>() == 0);
		CHECK(setup.modes.size() > 0);

		// 4.3.1. packet type, mode and window decode
		int mode_idx = reader.readBits<uint16_t>(highest_bit(setup.modes.size() - 1));
		const VorbisModeNumber& mode = setup.modes[mode_idx];
		const VorbisMapping& mapping = setup.mappings[mode.mapping];
		bool prev_window_flag = false, next_window_flag = false;
		if(mode.block_flag) {
			prev_window_flag = reader.readBitsT<1>();
			next_window_flag = reader.readBitsT<1>();
		}
		DataRange<const float> window = mode.getWindow(prev_window_flag, next_window_flag);
		CHECK((window.size() >> 16) == 0); // window size should fit in uint16_t
		std::vector<float> floor_outputs(window.size() * header.audio_channels);
		std::vector<bool> floor_output_used(header.audio_channels);

		// 4.3.2. floor curve decode
		for(uint8_t channel = 0; channel < header.audio_channels; ++channel) {
			uint8_t submap_number = mapping.muxs[channel];
			uint8_t floor_number = mapping.submaps[submap_number].floor;
			push_data_u8(this, "floor_number", channel, &floor_number, 1);
			const VorbisFloor& floor = setup.floors[floor_number];
			DataRange<float> out(&floor_outputs[window.size() * channel], window.size());
			bool use_output = false;
			CHECK_ERR(floor.decode(reader, setup.codebooks, out, use_output));
			floor_output_used[channel] = use_output;
			if(use_output)
				push_data_float(this, "floor_outputs", channel, out.begin(), out.size());
		}

		// 4.3.3. nonzero vector propagate
		for(const VorbisMapping::Coupling& coupling : mapping.couplings) {
			if(floor_output_used[coupling.angle] || floor_output_used[coupling.magintude]) {
				floor_output_used[coupling.angle] = true;
				floor_output_used[coupling.magintude] = true;
			}
		}

		// 4.3.4. residue decode
		std::vector<std::vector<float>> residue_outputs(header.audio_channels);
		for(size_t i = 0; i < mapping.submaps.size(); ++i) {
			const VorbisMapping::Submap& submap = mapping.submaps[i];
			uint8_t num_channel_per_submap = 0;
			std::vector<bool> channel_used(header.audio_channels);
			for(uint8_t j = 0; j < header.audio_channels; ++j) {
				if(mapping.muxs[j] == i) {
					channel_used[num_channel_per_submap] = floor_output_used[j];
					++num_channel_per_submap;
				}
			}
			channel_used.resize(num_channel_per_submap);
			const VorbisResidue& residue = setup.residues[submap.residue];
			uint32_t decode_len = residue.getDecodeLen((uint32_t) window.size());
			std::vector<std::vector<float>> out(num_channel_per_submap);
			for(uint8_t j = 0; j < num_channel_per_submap; ++j)
				out[j].resize(decode_len, 0);
			CHECK_ERR(residue.decode(reader, setup.codebooks, num_channel_per_submap, channel_used, decode_len, out));
			num_channel_per_submap = 0;
			for(uint8_t j = 0; j < header.audio_channels; ++j) {
				if(mapping.muxs[j] == i) {
					CHECK(out[num_channel_per_submap].size() == decode_len);
					residue_outputs[j].swap(out[num_channel_per_submap]);
					++num_channel_per_submap;
				}
			}
		}
		for(uint8_t channel = 0; channel < header.audio_channels; ++channel)
			push_data_float(this, "after_residue", channel, &residue_outputs[channel][0], residue_outputs[channel].size());

		// 4.3.5. inverse coupling
		for(size_t i = mapping.couplings.size(); i > 0; --i) {
			const VorbisMapping::Coupling& coupling = mapping.couplings[i - 1];
			std::vector<float>& magnitude_vector = residue_outputs[coupling.magintude];
			std::vector<float>& angle_vector = residue_outputs[coupling.angle];
			CHECK(magnitude_vector.size() == angle_vector.size());
			for(size_t j = 0; j < magnitude_vector.size(); ++j) {
				float mag_val = magnitude_vector[j];
				float ang_val = angle_vector[j];
				float mag_val_new = mag_val, ang_val_new = ang_val;
				if(mag_val > 0) {
					if(ang_val > 0) {
						ang_val_new = mag_val - ang_val;
					} else {
						ang_val_new = mag_val;
						mag_val_new = mag_val + ang_val;
					}
				} else {
					if(ang_val > 0) {
						ang_val_new = mag_val + ang_val;
					} else {
						ang_val_new = mag_val;
						mag_val_new = mag_val - ang_val;
					}
				}
				magnitude_vector[j] = mag_val_new;
				angle_vector[j] = ang_val_new;
			}
		}

		// 4.3.6. dot product
		// operate inplace on the residue_data.
		for(uint8_t channel = 0; channel < header.audio_channels; ++channel) {
			DataRange<float> residue_data(residue_outputs[channel]);
			if(floor_output_used[channel]) {
				DataRange<float> floor_data(&floor_outputs[window.size() * channel], window.size());
				CHECK(floor_data.size() >= window.size() / 2);
				CHECK(residue_data.size() >= window.size() / 2);
				for(size_t i = 0; i < window.size() / 2; ++i)
					residue_data[i] *= floor_data[i];
			}
			push_data_float(this, "after_envelope", channel, residue_data.begin(), residue_data.size());
		}

		// 4.3.7. inverse MDCT
		const Mdct& mdct = this->mdct[mode.block_flag ? 1 : 0];
		std::vector<float> pcm(mdct.n);
		for(uint8_t channel = 0; channel < header.audio_channels; ++channel) {
			DataRange<float> residue_data(residue_outputs[channel]);
			CHECK(mdct.n == residue_data.size() * 2);
			CHECK(mdct.n == mode.blocksize); // cur window size
			mdct.backward(residue_data.begin(), pcm.data());
			push_data_float(this, "pcm_after_mdct", channel, pcm.data(), pcm.size());
			// overlap/add data
			CHECK_ERR(state.addPcmFrame(channel, DataRange<const float>(pcm), window));
		}

		push_data_u8(this, "finish_audio_packet", -1, nullptr, 0);

		// cache right hand data & return finished audio data
		{
			// TODO: blocksize, number of frames?
			uint16_t blocksize0 = header.get_blocksize_0();
			uint16_t blocksize1 = header.get_blocksize_1();
			CHECK_ERR(state.advancePcmOffset(
				callbacks,
				(audio_packet_counts_ > 0) ? (prev_window_flag ? blocksize1 : blocksize0) : 0,
				mode.blocksize,
				next_window_flag ? blocksize1 : blocksize0));
		}

		return OkOrError();
	}
};


struct VorbisPacket {
	VorbisStream* stream;
	uint8_t* data;
	uint32_t data_len; // never more than 256*256

	OkOrError parse_id(ParseCallbacks& callbacks) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.2
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 1);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		CHECK(data_len - 7 == sizeof(VorbisIdHeader));
		VorbisIdHeader& header = stream->header;
		memcpy(&header, &data[7], sizeof(VorbisIdHeader));
		CHECK(header.framing_flag == 1);
		CHECK(callbacks.gotHeader(header));
		return OkOrError();
	}

	OkOrError parse_comment(ParseCallbacks& callbacks) {
		// https://xiph.org/vorbis/doc/v-comment.html
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.3
		// Meta tags, etc.
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 3);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		// ignore for now...
		// TODO...
		return OkOrError();
	}

	OkOrError parse_setup(ParseCallbacks& callbacks) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html 4.2.4
		CHECK(data_len >= 16);
		uint8_t type = data[0];
		CHECK(type == 5);
		CHECK(memcmp(&data[1], "vorbis", 6) == 0);
		ConstDataReader reader(data + 7, data_len - 7);
		BitReader bitReader(&reader);
		CHECK_ERR(stream->setup.parse(bitReader, stream->header));
		CHECK(reader.reachedEnd());
		stream->mdct[0].init(stream->header.get_blocksize_0());
		stream->mdct[1].init(stream->header.get_blocksize_1());
		stream->decode_state.init(
			stream->header.audio_channels,
			// Min buffer would be sth like min(blocksize0,blocksize1) * 2 or even a bit less.
			// However, doesn't matter if we have the buffer too large.
			// Actually that should be faster.
			uint32_t(stream->header.get_blocksize_0()) * 5 + uint32_t(stream->header.get_blocksize_1()) * 5);
		register_decoder_ref(stream, "ParseOggVorbis", stream->header.audio_sample_rate, stream->header.audio_channels);
		register_decoder_alias(stream, &stream->decode_state);
		for(VorbisFloor& floor : stream->setup.floors) {
			if(floor.floor_type == 1) {
				VorbisFloor1& floor1 = floor.floor1;
				register_decoder_alias(stream, &floor1);
				push_data_u8(stream, "floor1_unpack multiplier", -1, &floor1.multiplier, 1);
				push_data_u32(stream, "floor1_unpack xs", -1, &floor1.xs[0], floor1.xs.size());
			}
		}
		push_data_u8(stream, "finish_setup", -1, nullptr, 0);
		CHECK(callbacks.gotSetup(stream->setup));
		return OkOrError();
	}

	OkOrError parse_audio(ParseCallbacks& callbacks) {
		// https://xiph.org/vorbis/doc/Vorbis_I_spec.html
		// https://github.com/runningwild/gorbis/blob/master/vorbis/codec.go
		ConstDataReader reader(data, data_len);
		BitReader bitReader(&reader);
		return stream->parse_audio(bitReader, stream->decode_state, callbacks);
	}
};


struct OggReader {
	Page buffer_page_;
	std::map<uint32_t, VorbisStream> streams_;
	size_t packet_counts_;
	std::shared_ptr<IReader> reader_;
	ParseCallbacks& callbacks_;

	OggReader(ParseCallbacks& callbacks) : packet_counts_(0), callbacks_(callbacks) {}

	OkOrError open_file(const std::string& filename) {
		reader_ = std::make_shared<FileReader>(filename);
		CHECK_ERR(reader_->isValid());
		return OkOrError();
	}

	OkOrError read_next_page(bool& reached_eof) {
		Page::ReadHeaderResult res = buffer_page_.read_header(reader_.get());
		if(res == Page::ReadHeaderResult::Ok)
			CHECK_ERR(_read_page());
		else if(res == Page::ReadHeaderResult::Eof)
			reached_eof = true;
		else
			return OkOrError("read error");
		return OkOrError();
	}

	OkOrError full_read(const std::string& filename) {
		CHECK_ERR(open_file(filename));
		bool reached_eof = false;
		while(!reached_eof)
			CHECK_ERR(read_next_page(reached_eof));
		return OkOrError();
	}

	OkOrError _read_page() { // Called after buffer_page_.read_header().
		CHECK_ERR(buffer_page_.read(reader_.get()));
		if(buffer_page_.header.header_type_flag & HeaderFlag_First) {
			CHECK(streams_.find(buffer_page_.header.stream_serial_num) == streams_.end());
			streams_[buffer_page_.header.stream_serial_num] = VorbisStream();
		}
		CHECK(streams_.find(buffer_page_.header.stream_serial_num) != streams_.end());
		VorbisStream& stream = streams_[buffer_page_.header.stream_serial_num];

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
					CHECK_ERR(packet.parse_id(callbacks_));
				else if(stream.packet_counts_ == 1)
					CHECK_ERR(packet.parse_comment(callbacks_));
				else if(stream.packet_counts_ == 2)
					CHECK_ERR(packet.parse_setup(callbacks_));
				else {
					CHECK_ERR(packet.parse_audio(callbacks_));
					++stream.audio_packet_counts_;
				}
				++stream.packet_counts_;
				++packet_counts_;
				offset += len;
				len = 0;
			}
		}
		CHECK(len == 0 && offset == buffer_page_.data_len);

		if(buffer_page_.header.header_type_flag & HeaderFlag_Last) {
			CHECK(callbacks_.gotEof());
			streams_.erase(buffer_page_.header.stream_serial_num);
		}

		return OkOrError();
	}
};


#endif /* ParseOggVorbis_h */
