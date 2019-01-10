//
//  test_Utils.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 09.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include "Utils.hpp"
#include <iostream>

using namespace std;

template<typename T>
OkOrError checkReadBits(const char* data, size_t len, uint8_t numBits, T value) {
	ConstDataReader reader((const uint8_t*) data, len);
	BitReader bitReader(&reader);
	T out = bitReader.readBits<T>(numBits);
	CHECK(!bitReader.reachedEnd());
	if(out != value)
		std::cerr << std::hex << "out: 0x" << long(out) << ", value: 0x" << long(value) << std::dec << endl;
	CHECK(out == value);
	return OkOrError();
}

template<typename T1, typename T2>
OkOrError checkReadBits2(const char* data, size_t len, uint8_t n1, T1 v1, uint8_t n2, T2 v2) {
	ConstDataReader reader((const uint8_t*) data, len);
	BitReader bitReader(&reader);
	T1 out1 = bitReader.readBits<T1>(n1);
	CHECK(!bitReader.reachedEnd());
	if(out1 != v1)
		std::cerr << std::hex << "out1: " << "0x" << long(out1) << ", v1: 0x" << long(v1) << std::dec << endl;
	CHECK(out1 == v1);
	T2 out2 = bitReader.readBits<T2>(n2);
	CHECK(!bitReader.reachedEnd());
	if(out2 != v2)
		std::cerr << std::hex << "out2: " << "0x" << long(out2) << ", v2: 0x" << long(v2) << std::dec << endl;
	CHECK(out2 == v2);
	return OkOrError();
}

void test_all() {
	ASSERT_ERR(checkReadBits("\x00\x00\x00\x01", 4, 1, 0));
	ASSERT_ERR(checkReadBits("\x01\x00\x00\x00", 4, 1, 1));
	ASSERT_ERR(checkReadBits("\xff\x00\x00\x00", 4, 1, 1));
	ASSERT_ERR(checkReadBits("\x02\x00\x00\x00", 4, 1, 0));
	ASSERT_ERR(checkReadBits("\x02\x00\x00\x00", 4, 2, 2));
	ASSERT_ERR(checkReadBits("\x02\x00\x00\x00", 4, 3, 2));
	ASSERT_ERR(checkReadBits("\x02\x00\x00\x00", 4, 8, 2));
	ASSERT_ERR(checkReadBits("\x02\x00\x00\x00", 4, 9, 2));
	ASSERT_ERR(checkReadBits("\xff\x00\x00\x00", 4, 8, 255));
	ASSERT_ERR(checkReadBits("\xff\xff\x00\x00", 4, 16, 0xffff));
	ASSERT_ERR(checkReadBits("\x01\x02\x00\x00", 4, 16, 0x0201));
	ASSERT_ERR(checkReadBits("\x01\x02\x03\x04", 4, 32, 0x04030201));
	ASSERT_ERR(checkReadBits2("\x01\x02\x00\x00", 4, 8, 1, 8, 2));
	ASSERT_ERR(checkReadBits2("\x01\x01\x00\x00", 4, 7, 1, 8, 2));
}

int main() {
	test_all();
	return 0;
}
