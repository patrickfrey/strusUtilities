/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2013,2014 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
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


