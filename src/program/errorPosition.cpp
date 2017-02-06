/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Implements referencing of an error position in a program source
/// \file errorPosition.cpp
#include "strus/base/snprintf.h"
#include "private/internationalization.hpp"
#include "errorPosition.hpp"

using namespace strus;

ErrorPosition::ErrorPosition( const char* base, const char* itr, bool binary)
{
	if (binary)
	{
		strus_snprintf( m_buf, sizeof(m_buf), _TXT("at byte %u"), itr-base);
	}
	else
	{
		unsigned int line = 1;
		unsigned int col = 1;
	
		for (unsigned int ii=0,nn=itr-base; ii < nn; ++ii)
		{
			if (base[ii] == '\n')
			{
				col = 1;
				++line;
			}
			else
			{
				++col;
			}
		}
		strus_snprintf( m_buf, sizeof(m_buf), _TXT("at line %u column %u"), line, col);
	}
}

