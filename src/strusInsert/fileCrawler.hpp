/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_INSERTER_FILE_CRAWLER_HPP_INCLUDED
#define _STRUS_INSERTER_FILE_CRAWLER_HPP_INCLUDED
#include "strus/private/fileio.hpp"
#include "strus/index.hpp"
#include "fileCrawlerInterface.hpp"
#include "private/utils.hpp"
#include <vector>
#include <string>
#include <list>
#include <deque>

namespace strus {

class FileCrawler
	:public FileCrawlerInterface
{
public:
	FileCrawler(
			const std::string& path_,
			std::size_t transactionSize_,
			std::size_t nofChunksReadAhead_,
			const std::string& extension_);

	virtual ~FileCrawler();

	virtual bool fetch( std::vector<std::string>& files);

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
		return (m_chunkquesize < m_nofChunksReadAhead);
	}

private:
	std::size_t m_transactionSize;
	std::size_t m_nofChunksReadAhead;
	std::string m_extension;
	std::list<std::string> m_directories;
	std::list<std::string>::iterator m_diritr;

	std::vector<std::string> m_openchunk;
	std::deque<std::vector<std::string> > m_chunkque;
	std::size_t m_chunkquesize;
	utils::Mutex m_chunkque_mutex;
	utils::ConditionVariable m_chunkque_cond;

	utils::ConditionVariable m_worker_cond;
	utils::Mutex m_worker_mutex;
	utils::AtomicBool m_terminated;
};

}//namespace
#endif
