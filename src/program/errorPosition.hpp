/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Implements referencing of an error position in a program source
/// \file errorPosition.hpp
#ifndef _STRUS_UTILITIES_PROGRAM_ERROR_POSITION_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_ERROR_POSITION_INCLUDED

#error DEPRECATED

/// \brief strus toplevel namespace
namespace strus {

class ErrorPosition
{
public:
	ErrorPosition( const char* base, const char* itr, bool binary=false);
	const char* c_str() const
	{
		return m_buf;
	}
private:
	char m_buf[ 128];
};

}//namespace
#endif


