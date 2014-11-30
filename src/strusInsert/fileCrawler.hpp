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
#ifndef _STRUS_INSERTER_FILE_CRAWLER_HPP_INCLUDED
#define _STRUS_INSERTER_FILE_CRAWLER_HPP_INCLUDED
#include "strus/utils/fileio.hpp"
#include "strus/index.hpp"
#include <cstring>
#include <stdexcept>
#include <vector>
#include <string>
#include <deque>
#include <stdexcept>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>

namespace strus {

class DocnoAllocator
{
public:
	virtual Index allocDocnoRange( const Index& size)=0;
};

class FileCrawler
{
public:
	FileCrawler(
			const std::string& directory_,
			std::size_t transactionSize_,
			std::size_t nofConsumers_)
		:m_transactionSize(transactionSize_)
		,m_nofConsumers(nofConsumers_)
		,m_chunkquesize(0)
		,m_terminated(false)
	{
		m_directories.push_back( directory_);
		m_diritr = m_directories.begin();
	}

	bool fetch( std::vector<std::string>& files);

	void start()
	{
		boost::thread this_thread( boost::bind( run, this));
	}

private:
	typedef std::map<std::string,std::string> Parent;

	void run()
	{
		while (!m_terminated)
		{
			boost::unique_lock<boost::mutex> lock( m_worker_mutex);
			m_worker_cond.wait( lock);

			findFilesToProcess();
		}
	}

	void findFilesToProcess();

	void pushChunk( const std::vector<std::string>& chunk);

	bool haveEnough()
	{
		return (m_chunkquesize > nofConsumers_*2);
	}

private:
	std::size_t m_transactionSize;
	std::size_t m_nofConsumers;
	std::list<std::string> m_directories;
	std::list<std::string>::const_iterator m_diritr;

	std::vector<std::string> m_openchunk;
	std::deque<std::vector<std::string> > m_chunkque;
	std::size_t m_chunkquesize;
	boost::mutex m_chunkque_mutex;
	boost::condition_variable m_chunkque_cond;

	boost::condition_variable m_worker_cond;
	boost::mutex m_worker_mutex;
	boost::atomic<bool> m_terminated;
};

}//namespace
#endif
