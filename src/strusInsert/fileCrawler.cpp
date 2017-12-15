/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "fileCrawler.hpp"
#include "private/internationalization.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/string_format.hpp"
#include <iostream>

using namespace strus;

FileCrawler::FileCrawler(
		const std::string& path_,
		std::size_t chunkSize_,
		const std::string& extension_)

	:m_chunkSize(chunkSize_)
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

FileCrawler::~FileCrawler()
{
}

void FileCrawler::collectFilesToProcess( const std::string& dir)
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
			m_chunkque.back().files.push_back( dir + strus::dirSeparator() + *fi);
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
				std::string subdir( dir + strus::dirSeparator() + *di);
				if (strus::isDir( subdir))
				{
					collectFilesToProcess( subdir);
				}
			}
		}
	}
}

std::vector<std::string> FileCrawler::fetch()
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


