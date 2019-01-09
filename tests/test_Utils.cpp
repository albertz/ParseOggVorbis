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
	ConstDataReader reader(data, len);
	BitReader bitReader(&reader);
	T out = 0;
	CHECK_ERR(bitReader.readBits(out, numBits));
	if(out != value)
		std::cerr << "out: " << long(out) << ", value: " << long(value) << endl;
	CHECK(out == value);
	return OkOrError();
}

template<typename T1, typename T2>
OkOrError checkReadBits2(const char* data, size_t len, uint8_t n1, T1 v1, uint8_t n2, T2 v2) {
	ConstDataReader reader(data, len);
	BitReader bitReader(&reader);
	T1 out1 = 0;
	CHECK_ERR(bitReader.readBits(out1, n1));
	if(out1 != v1)
		std::cerr << "out1: " << long(out1) << ", v1: " << long(v1) << endl;
	CHECK(out1 == v1);
	T2 out2 = 0;
	CHECK_ERR(bitReader.readBits(out2, n2));
	if(out2 != v2)
		std::cerr << "out1: " << long(out1) << ", v1: " << long(v1) << endl;
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
	ASSERT_ERR(checkReadBits2("\x01\x02\x00\x00", 4, 8, 1, 8, 2));
}

int main() {
	test_all();
	return 0;
}
