/*
	This file is part of the Util library.
	Copyright (C) 2012 Benjamin Eikel <benjamin@eikel.org>
	
	This library is subject to the terms of the Mozilla Public License, v. 2.0.
	You should have received a copy of the MPL along with this library; see the 
	file LICENSE. If not, you can obtain one at http://mozilla.org/MPL/2.0/.
*/
#ifndef GENERICATTRIBUTETEST_H_
#define GENERICATTRIBUTETEST_H_

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

class GenericAttributeTest : public CppUnit::TestFixture {
	CPPUNIT_TEST_SUITE(GenericAttributeTest);
	CPPUNIT_TEST(testBool);
	CPPUNIT_TEST(testNumber);
	CPPUNIT_TEST(testString);
	CPPUNIT_TEST_SUITE_END();

	public:
		void testBool();
		void testNumber();
		void testString();
};

#endif /* GENERICATTRIBUTETEST_H_ */
