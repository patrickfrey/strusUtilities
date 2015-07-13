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
#include "private/inputStream.hpp"
#include <stdexcept>
#include <cstring>

using namespace strus;

InputStream::InputStream( const std::string& docpath)
	:m_docpath(docpath)
{
	if (docpath == "-")
	{
		m_fh = stdin;
	}
	else
	{
		m_fh = ::fopen( docpath.c_str(), "rb");
		if (!m_fh)
		{
			char buf[ 256];
			snprintf( buf, sizeof(buf), "failed to open file %s for reading (errno %d)", docpath.c_str(), errno);
			throw std::runtime_error( buf);
		}
	}
}

InputStream::~InputStream()
{
	if (m_fh != stdin)
	{
		::fclose( m_fh);
	}
}

std::size_t InputStream::read( char* buf, std::size_t bufsize)
{
	unsigned int idx = 0;
	if (m_bufferidx < m_buffer.size())
	{
		std::size_t nn = m_buffer.size() - m_bufferidx;
		if (nn > bufsize) nn = bufsize;
		std::memcpy( buf, m_buffer.c_str()+m_bufferidx, nn);
		m_bufferidx += nn;
		if (m_bufferidx == m_buffer.size())
		{
			m_buffer.clear();
		}
		idx = nn;
	}
	std::size_t rt = ::fread( buf + idx, 1, bufsize - idx, m_fh) + idx;
	if (!rt)
	{
		if (!feof( m_fh))
		{
			unsigned int ec = ::ferror( m_fh);
			char errbuf[ 256];
			snprintf( errbuf, sizeof( errbuf), "failed to read from file %s (errno %d)", m_docpath.c_str(), ec);
			throw std::runtime_error( errbuf);
		}
	}
	return rt;
}

std::size_t InputStream::readAhead( char* buf, std::size_t bufsize)
{
	std::size_t rt = read( buf, bufsize);
	if (m_bufferidx != m_buffer.size())
	{
		throw std::runtime_error( "subsequent calls of readAhead not allowed");
	}
	m_buffer.clear();
	m_buffer.append( buf, bufsize);
	return rt;
}

const char* InputStream::readline( char* buf, std::size_t bufsize)
{
	char* rt = ::fgets( buf, bufsize, m_fh);
	if (!rt)
	{
		if (!feof( m_fh))
		{
			unsigned int ec = ::ferror( m_fh);
			char msgbuf[ 256];
			snprintf( msgbuf, sizeof(msgbuf), "failed to read from file %s (errno %d)", m_docpath.c_str(), ec);
			throw std::runtime_error( msgbuf);
		}
	}
	return rt;
}




