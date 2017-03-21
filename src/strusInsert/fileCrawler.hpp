/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_INSERTER_FILE_CRAWLER_HPP_INCLUDED
#define _STRUS_INSERTER_FILE_CRAWLER_HPP_INCLUDED
#include "strus/base/fileio.hpp"
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
			std::size_t chunkSize_,
			const std::string& extension_);

	virtual ~FileCrawler();

	virtual std::vector<std::string> fetch();

private:
	struct Chunk
	{
		Chunk( const Chunk& o)
			:files(o.files){}
		Chunk()
			:files(){}

		void clear()
		{
			files.clear();
		}

		static Chunk singleFile( const std::string& filename)
		{
			Chunk rt;
			rt.files.push_back( filename);
			return rt;
		}

		std::vector<std::string> files;
	};

	void collectFilesToProcess( const std::string& dir);

private:
	std::size_t m_chunkSize;
	std::string m_extension;

	std::list<Chunk> m_chunkque;
	utils::Mutex m_chunkque_mutex;
};

}//namespace
#endif

