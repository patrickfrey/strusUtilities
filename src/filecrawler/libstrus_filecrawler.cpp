/*
 * Copyright (c) 2018 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Exported functions of the file crawler
#include "strus/lib/filecrawler.hpp"
#include "strus/errorBufferInterface.hpp"
#include "fileCrawler.hpp"
#include "private/internationalization.hpp"
#include "strus/base/dll_tags.hpp"
#include "private/errorUtils.hpp"

static bool g_intl_initialized = false;

using namespace strus;

DLL_PUBLIC FileCrawlerInterface* strus::createFileCrawlerInterface( const std::string& path, int chunkSize, const std::string& extension, ErrorBufferInterface* errorhnd)
{
	try
	{
		if (!g_intl_initialized)
		{
			strus::initMessageTextDomain();
			g_intl_initialized = true;
		}
		return new FileCrawler( path, chunkSize, extension, errorhnd);
	}
	CATCH_ERROR_MAP_RETURN( _TXT("cannot create file crawler: %s"), *errorhnd, 0);
}

