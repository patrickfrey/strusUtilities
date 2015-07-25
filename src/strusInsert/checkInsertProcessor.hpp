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
#ifndef _STRUS_CHECK_INSERT_PROCESSOR_HPP_INCLUDED
#define _STRUS_CHECK_INSERT_PROCESSOR_HPP_INCLUDED
#include "private/utils.hpp"
#include "analyzerMap.hpp"
#include <string>

namespace strus {

/// \brief Forward declaration
class StorageClientInterface;
/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class DocumentAnalyzerInterface;
/// \brief Forward declaration
class FileCrawlerInterface;

class CheckInsertProcessor
{
public:
	CheckInsertProcessor(
			StorageClientInterface* storage_,
			const TextProcessorInterface* textproc_,
			const AnalyzerMap& analyzerMap_,
			FileCrawlerInterface* crawler_,
			const std::string& logfile_);

	~CheckInsertProcessor();

	void sigStop();
	void run();

private:
	StorageClientInterface* m_storage;
	const TextProcessorInterface* m_textproc;
	AnalyzerMap m_analyzerMap;
	FileCrawlerInterface* m_crawler;
	utils::AtomicBool m_terminated;
	std::string m_logfile;
};

}//namespace
#endif
