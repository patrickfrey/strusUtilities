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
#include "private/utils.hpp"

using namespace strus;

FileCrawler::FileCrawler(
		const std::string& path_,
		std::size_t transactionSize_,
		std::size_t nofChunksReadAhead_)

	:m_transactionSize(transactionSize_)
	,m_nofChunksReadAhead(nofChunksReadAhead_)
	,m_chunkquesize(0)
	,m_terminated(false)
{
	if (strus::isDir( path_))
	{
		std::size_t psize = path_.size();
		while (psize && path_[ psize-1] == strus::dirSeparator()) --psize;
		m_directories.push_back( std::string( path_.c_str(), psize));
	}
	else
	{
		m_openchunk.push_back( path_);
	}
	m_diritr = m_directories.begin();
}

FileCrawler::~FileCrawler()
{
}

void FileCrawler::run()
{
	findFilesToProcess();

	while (!m_terminated)
	{
		utils::UniqueLock lock( m_worker_mutex);
		m_worker_cond.wait( lock);
		if (m_terminated) break;

		findFilesToProcess();
	}
	m_chunkque_cond.notify_all();
}

void FileCrawler::findFilesToProcess()
{
	while (m_diritr != m_directories.end())
	{
		std::vector<std::string> files;
		std::string path = *m_diritr;
		unsigned int ec = strus::readDir( path, ".xml", files);
		if (ec)
		{
			std::cerr << "could not read directory to process '" << path << "' (file system error '" << ec << ")" << std::endl;
		}
		else
		{
			std::vector<std::string>::const_iterator
				fi = files.begin(), fe = files.end();
			for (; fi != fe; ++fi)
			{
				m_openchunk.push_back(
					path + strus::dirSeparator() + *fi);
				if (m_openchunk.size() == m_transactionSize)
				{
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
			std::cerr << "could not read subdirectories to process '" << path << "' (file system error " << ec << ")" << std::endl;
			continue;
		}
		std::vector<std::string>::const_iterator
			di = subdirs.begin(), de = subdirs.end();
		for (; di != de; ++di)
		{
			std::string subdir( path + strus::dirSeparator() + *di);
			if (strus::isDir( subdir))
			{
				m_diritr = m_directories.insert( m_diritr, subdir);
			}
		}

		if (haveEnough()) return;
	}
	m_terminated = true;
	if (m_openchunk.empty())
	{
		return;
	}
	pushChunk( m_openchunk);
	m_openchunk.clear();
}


bool FileCrawler::fetch( std::vector<std::string>& files)
{
	utils::UniqueLock lock(m_chunkque_mutex);
	if (!needMore())
	{
		m_worker_cond.notify_one();
	}

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
	files = m_chunkque.front();
	m_chunkque.pop_front();
	return true;
}

void FileCrawler::sigStop()
{
	m_terminated = true;
	m_worker_cond.notify_one();
}

void FileCrawler::pushChunk( const std::vector<std::string>& chunk)
{
	utils::UniqueLock lock( m_chunkque_mutex);
	m_chunkquesize += 1;
	m_chunkque.push_back( chunk);
	m_chunkque_cond.notify_one();
}

