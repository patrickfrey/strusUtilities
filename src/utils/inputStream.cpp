/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "private/inputStream.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include <stdexcept>
#include <cstring>

using namespace strus;

InputStream::InputStream( const std::string& docpath)
	:m_fh(0),m_docpath(docpath),m_bufferidx(0)
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
			throw strus::runtime_error(_TXT("failed to open file '%s' for reading (errno %d)"), docpath.c_str(), errno);
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
			m_bufferidx = 0;
		}
		idx = nn;
	}
	std::size_t rt = ::fread( buf + idx, 1, bufsize - idx, m_fh) + idx;
	if (!rt)
	{
		if (!feof( m_fh))
		{
			unsigned int ec = ::ferror( m_fh);
			throw strus::runtime_error(_TXT("failed to read from file '%s' (errno %d)"), m_docpath.c_str(), ec);
		}
	}
	return rt;
}

std::size_t InputStream::readAhead( char* buf, std::size_t bufsize)
{
	std::size_t rt = read( buf, bufsize);
	if (m_bufferidx != m_buffer.size())
	{
		throw strus::runtime_error( _TXT("subsequent calls of readAhead not allowed"));
	}
	m_buffer.clear();
	m_buffer.append( buf, rt);
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
			throw strus::runtime_error(_TXT("failed to read from file '%s' (errno %d)"), m_docpath.c_str(), ec);
		}
	}
	return rt;
}




