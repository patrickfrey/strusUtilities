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
		std::memcpy( buf, m_buffer.c_str()+m_bufferidx, restsize);
		idx = restsize;

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
	if (m_bufferidx != m_buffer.size())
	{
		char const* eolptr = ::strchr( m_buffer.c_str() + m_bufferidx, '\n');
		if (eolptr)
		{
			const char* ptr = m_buffer.c_str() + m_bufferidx;
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
			std::size_t restbufsize = m_buffer.size() - m_bufferidx;
			if (restbufsize >= bufsize)
			{
				std::memcpy( buf, m_buffer.c_str() + m_bufferidx, bufsize-1);
				buf[ bufsize-1] = '\0';
				m_bufferidx += bufsize-1;
			}
			else
			{
				m_buffer = std::string( m_buffer.c_str() + m_bufferidx, restbufsize);
				m_bufferidx = 0;
				char* restline = ::fgets( buf, bufsize-restbufsize-1, m_fh);
				if (restline)
				{
					m_buffer.append( restline);
					std::memcpy( buf, m_buffer.c_str(), m_buffer.size());
					buf[ m_buffer.size()] = '\0';
					m_buffer.clear();
				}
				else if (feof( m_fh))
				{
					std::memcpy( buf, m_buffer.c_str() + m_bufferidx, restbufsize);
					buf[ restbufsize] = '\0';
					m_bufferidx = 0;
					m_buffer.clear();
					if (!restbufsize) return 0;
				}
				else
				{
					unsigned int ec = ::ferror( m_fh);
					throw strus::runtime_error(_TXT("failed to read from file '%s' (errno %d)"), m_docpath.c_str(), ec);
				}
			}
		}
		return buf;
	}
	else
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
}




