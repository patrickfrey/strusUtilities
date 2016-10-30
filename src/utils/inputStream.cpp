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
	if (m_bufferidx > bufsize * 2)
	{
		m_buffer = std::string( m_buffer.c_str() + m_bufferidx, m_buffer.size() - m_bufferidx);
		m_bufferidx = 0;
	}
	if (!bufsize) return 0;
	unsigned int idx = 0;
	if (m_bufferidx < m_buffer.size())
	{
		std::size_t restsize = m_buffer.size() - m_bufferidx;
		if (restsize >= bufsize)
		{
			std::memcpy( buf, m_buffer.c_str()+m_bufferidx, bufsize);
			m_bufferidx += bufsize;
			return bufsize;
		}
		else
		{
			std::memcpy( buf, m_buffer.c_str()+m_bufferidx, restsize);
			m_bufferidx += restsize;
			idx = restsize;
		}
		if (m_bufferidx == m_buffer.size())
		{
			m_buffer.clear();
			m_bufferidx = 0;
		}
	}
	std::size_t rt = ::fread( buf + idx, 1, bufsize - idx, m_fh);
	if (!rt)
	{
		if (!feof( m_fh))
		{
			unsigned int ec = ::ferror( m_fh);
			throw strus::runtime_error(_TXT("failed to read from file '%s' (errno %d)"), m_docpath.c_str(), ec);
		}
	}
	return idx + rt;
}

std::size_t InputStream::readAhead( char* buf, std::size_t bufsize)
{
	if (m_bufferidx > bufsize * 2)
	{
		m_buffer = std::string( m_buffer.c_str() + m_bufferidx, m_buffer.size() - m_bufferidx);
		m_bufferidx = 0;
	}
	if (!bufsize) return 0;
	std::size_t restsize = m_buffer.size() - m_bufferidx;
	if (restsize >= bufsize)
	{
		std::memcpy( buf, m_buffer.c_str() + m_bufferidx, bufsize);
		return bufsize;
	}
	else
	{
		std::size_t rt = ::fread( buf, 1, bufsize - restsize, m_fh);
		if (!rt)
		{
			if (!feof( m_fh))
			{
				unsigned int ec = ::ferror( m_fh);
				throw strus::runtime_error(_TXT("failed to read from file '%s' (errno %d)"), m_docpath.c_str(), ec);
			}
		}
		if (restsize)
		{
			m_buffer.append( buf, rt);
			std::memcpy( buf, m_buffer.c_str() + m_bufferidx, bufsize);
			return bufsize;
		}
		else
		{
			m_buffer.clear();
			m_buffer.append( buf, rt);
			return rt;
		}
	}
}

const char* InputStream::readLine( char* buf, std::size_t bufsize)
{
	if (!bufsize) return 0;
	char const* eolptr = ::strchr( m_buffer.c_str() + m_bufferidx, '\n');
	if (!eolptr)
	{
		(void)readAhead( buf, bufsize);
		eolptr = ::strchr( m_buffer.c_str() + m_bufferidx, '\n');
	}
	const char* ptr = m_buffer.c_str() + m_bufferidx;
	if (eolptr)
	{
		std::size_t len = eolptr - ptr;
		if (len >= bufsize)
		{
			std::memcpy( buf, ptr, bufsize-1);
			buf[ bufsize-1] = '\0';
			m_bufferidx += bufsize-1;
		}
		else
		{
			std::memcpy( buf, ptr, len);
			buf[ len] = '\0';
			m_bufferidx += len + 1/*\n*/;
		}
	}
	else
	{
		std::size_t restsize = m_buffer.size() - m_bufferidx;
		std::size_t nn = (restsize >= bufsize)?(bufsize-1):restsize;
		std::memcpy( buf, ptr, nn);
		buf[ nn] = '\0';
		m_bufferidx += nn;
	}
	return buf;
}




