/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_UTILITIES_INPUT_STREAM_HPP_INCLUDED
#define _STRUS_UTILITIES_INPUT_STREAM_HPP_INCLUDED
#include "private/utils.hpp"
#include <string>
#include <fstream>
#include <cstdio>

namespace strus {

/// \class InputStream
/// \brief Abstraction of input stream
class InputStream
{
public:
	/// \brief Constructor
	/// \param[in] docpath path to file to read or "-" for stdin
	InputStream( const std::string& docpath);

	/// \brief Destructor
	~InputStream();

	/// \brief Read some data
	/// \param[in,out] buf where to write to
	/// \param[in] bufsize allocation size of 'buf' (capacity) 
	/// \return the number of bytes read
	std::size_t read( char* buf, std::size_t bufsize);

	/// \brief Read a line
	/// \param[in,out] buf where to write to
	/// \param[in] bufsize allocation size of 'buf' (capacity) 
	/// \return pointer to the line read
	const char* readline( char* buf, std::size_t bufsize);

	/// \brief Read some data and keep it in a buffer for the next read
	/// \param[in,out] buf where to write to
	/// \param[in] bufsize allocation size of 'buf' (capacity) 
	/// \return the number of bytes read
	std::size_t readAhead( char* buf, std::size_t bufsize);

private:
	FILE* m_fh;
	std::string m_docpath;
	std::ifstream m_stream;
	std::string m_buffer;
	std::size_t m_bufferidx;
};

}
#endif


