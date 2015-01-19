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
#include "strus/private/fileio.hpp"
#include "strus/index.hpp"
#include "docnoAllocatorInterface.hpp"
#include "fileCrawlerInterface.hpp"
#include <vector>
#include <string>
#include <list>
#include <deque>
#include <boost/thread.hpp>
#include <boost/atomic.hpp>

namespace strus {

class FileCrawler
	:public FileCrawlerInterface
{
public:
	FileCrawler(
			const std::string& path_,
			DocnoAllocatorInterface* docnoAllocator_,
			std::size_t transactionSize_,
			std::size_t nofChunksReadAhead_=10);

	virtual ~FileCrawler();

	virtual bool fetch( Index& docno, std::vector<std::string>& files);

	void sigStop();
	void run();

private:
	void findFilesToProcess();

	void pushChunk( const std::vector<std::string>& chunk);

	bool haveEnough()
	{
		return (m_chunkquesize > (m_nofChunksReadAhead*2));
	}
	bool needMore()
	{
		return (m_chunkquesize > m_nofChunksReadAhead);
	}

private:
	std::size_t m_transactionSize;
	std::size_t m_nofChunksReadAhead;
	std::list<std::string> m_directories;
	std::list<std::string>::iterator m_diritr;

	std::vector<std::string> m_openchunk;
	std::deque<std::vector<std::string> > m_chunkque;
	std::size_t m_chunkquesize;
	boost::mutex m_chunkque_mutex;
	boost::condition_variable m_chunkque_cond;

	boost::condition_variable m_worker_cond;
	boost::mutex m_worker_mutex;
	boost::atomic<bool> m_terminated;
	DocnoAllocatorInterface* m_docnoAllocator;
};

}//namespace
#endif
