/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "fileCrawler.hpp"
#include "private/internationalization.hpp"
#include "private/errorUtils.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/string_format.hpp"
#include <iostream>

using namespace strus;

FileCrawler::FileCrawler(
		const std::string& path_,
		std::size_t chunkSize_,
		const std::string& extension_,
		ErrorBufferInterface* errorhnd_)

	:m_errorhnd(errorhnd_)
	,m_chunkSize(chunkSize_)
	,m_extension(extension_)
	,m_chunkque()
{
	if (strus::isDir( path_))
	{
		m_chunkque.push_back( Chunk());
		collectFilesToProcess( path_);
	}
	else
	{
		m_chunkque.push_back( Chunk::singleFile( path_));
	}
}

FileCrawler::FileCrawler(
		const std::vector<std::string>& path_,
		std::size_t chunkSize_,
		const std::string& extension_,
		ErrorBufferInterface* errorhnd_)

	:m_errorhnd(errorhnd_)
	,m_chunkSize(chunkSize_)
	,m_extension(extension_)
	,m_chunkque()
{
	m_chunkque.push_back( Chunk());
	std::vector<std::string>::const_iterator pi = path_.begin(), pe = path_.end();
	for (; pi != pe; ++pi)
	{
		if (strus::isDir( *pi))
		{
			collectFilesToProcess( *pi);
		}
		else
		{
			if (m_chunkque.back().files.size() >= m_chunkSize)
			{
				m_chunkque.push_back( Chunk());
			}
			m_chunkque.back().files.push_back( *pi);
		}
	}
}

FileCrawler::~FileCrawler()
{
}

void FileCrawler::collectFilesToProcess( const std::string& dir)
{
	try
	{
		std::vector<std::string> files;
		unsigned int ec = strus::readDirFiles( dir, m_extension, files);
		if (ec)
		{
			std::cerr << string_format( _TXT( "could not read directory to process '%s' (errno %u)"), dir.c_str(), ec) << std::endl;
			std::cerr.flush();
		}
		else
		{
			std::vector<std::string>::const_iterator fi = files.begin(), fe = files.end();
			for (; fi != fe; ++fi)
			{
				if (m_chunkque.back().files.size() >= m_chunkSize)
				{
					m_chunkque.push_back( Chunk());
				}
				std::string fullpath = strus::joinFilePath( dir, *fi);
				if (fullpath.empty()) throw std::bad_alloc();
				m_chunkque.back().files.push_back( fullpath);
			}
			std::vector<std::string> subdirs;
			ec = strus::readDirSubDirs( dir, subdirs);
			if (ec)
			{
				std::cerr << string_format( _TXT( "could not read subdirectories to process '%s' (errno %u)"), dir.c_str(), ec) << std::endl;
				std::cerr.flush();
			}
			else
			{
				std::vector<std::string>::const_iterator di = subdirs.begin(), de = subdirs.end();
				for (; di != de; ++di)
				{
					std::string subdir = strus::joinFilePath( dir, *di);
					if (strus::isDir( subdir))
					{
						collectFilesToProcess( subdir);
					}
				}
			}
		}
	}
	CATCH_ERROR_MAP( _TXT("error collecting files to process: %s"), *m_errorhnd);
}

std::vector<std::string> FileCrawler::fetch()
{
	try
	{
		strus::scoped_lock lock( m_chunkque_mutex);
		if (m_chunkque.empty())
		{
			return std::vector<std::string>();
		}
		std::vector<std::string> rt = m_chunkque.front().files;
		m_chunkque.pop_front();
		return rt;
	}
	CATCH_ERROR_MAP_RETURN( _TXT("error fetching files to process: %s"), *m_errorhnd, std::vector<std::string>());
}


