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
#include "fileCrawler.hpp"

using namespace strus;

void FileCrawler::findFilesToProcess()
{
	std::vector<std::string> files;
	while (m_diritr != m_directories.end())
	{
		std::string path = *m_diritr;
		unsigned int ec = strus::readDir( path, ".xml", files);
		if (ec)
		{
			std::cerr << "ERROR could not read directory to process '" << path << "' (file system error '" << ec << ")" << std::endl;
		}
		else
		{
			std::vector<std::string>::const_iterator
				fi = files.begin(), fe = files.end();
			for (std::size_t fidx=0; fi != fe; ++fi,++fidx)
			{
				m_openchunk.push_back( *fi);
				if (fidx == m_transactionSize)
				{
					fidx = 0;
					pushChunk( m_openchunk);
					m_openchunk.clear();
				}
			}
		}
		std::vector<std::string> subdirs;
		ec = strus::readDir( path, "", subdirs);
		m_diritr = m_directories.erase( m_diritr);
		if (ec)
		{
			std::cerr << "ERROR could not read subdirectories to process '" << path << "' (file system error " << ec << ")" << std::endl;
			continue;
		}
		std::vector<std::string>::const_iterator
			di = subdirs.begin(), de = subdirs.end();
		for (; di != de; ++di)
		{
			std::string subdir( path + strus::dirSeparator() + *di);
			if (strus::isDir( subdir))
			{
				m_directories.insert( m_diritr, subdir);
			}
		}

		if (haveEnough()) return;
	}
	pushChunk( m_openchunk);
	m_openchunk.clear();

	m_terminated = true;
}


bool FileCrawler::fetch( std::vector<std::string>& files)
{
	boost::unique_lock<boost::mutex> lock(m_chunkque_mutex);

	while (m_chunkque.empty() && !m_terminated)
	{
		m_worker_cond.notify_one();
		m_chunkque_cond.wait(lock);
	}
	if (m_chunkque.empty())
	{
		return false;
	}
	m_chunkquesize -= 1;
	if (!haveEnough())
	{
		m_worker_cond.notify_one();
	}
	files = m_chunkque.pop_front();
	return true;
}

void FileCrawler::pushChunk( const std::vector<std::string>& chunk)
{
	boost::unique_lock<boost::mutex> lock( m_chunkque_mutex);
	m_chunkquesize += 1;
	m_chunkque.push_back( chunk);
	m_chunkque_cond.notify_one();
}

