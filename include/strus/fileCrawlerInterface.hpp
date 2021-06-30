/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_INSERTER_FILE_CRAWLER_INTERFACE_HPP_INCLUDED
#define _STRUS_INSERTER_FILE_CRAWLER_INTERFACE_HPP_INCLUDED
#include "strus/storage/index.hpp"
#include <vector>
#include <string>

namespace strus {

class FileCrawlerInterface
{
public:
	virtual ~FileCrawlerInterface(){}
	virtual std::vector<std::string> fetch()=0;
};

}//namespace
#endif

