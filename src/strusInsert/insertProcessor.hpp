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
#ifndef _STRUS_INSERTER_PROCESSOR_HPP_INCLUDED
#define _STRUS_INSERTER_PROCESSOR_HPP_INCLUDED
#include "strus/index.hpp"
#include "strus/analyzerInterface.hpp"
#include "strus/storageInterface.hpp"
#include "docnoAllocatorInterface.hpp"
#include "fileCrawlerInterface.hpp"
#include "commitQueue.hpp"
#include <vector>
#include <string>
#include <boost/thread.hpp>
#include <boost/atomic.hpp>

namespace strus {

class InsertProcessor
{
public:
	InsertProcessor(
			StorageInterface* storage_,
			AnalyzerInterface* analyzer_,
			CommitQueue* commitque_,
			FileCrawlerInterface* crawler_);

	~InsertProcessor();

	void sigStop();
	void run();

private:
	StorageInterface* m_storage;
	AnalyzerInterface* m_analyzer;
	CommitQueue* m_commitque;
	FileCrawlerInterface* m_crawler;
	boost::atomic<bool> m_terminated;
};

}//namespace
#endif
