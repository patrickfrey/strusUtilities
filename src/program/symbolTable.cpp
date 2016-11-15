/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Implements a symbol table with implicit symbol definition with lookup in both directions
/// \file symbolTable.cpp
#include "symbolTable.hpp"
#include "private/utils.hpp"
#include <string>
#include <vector>
#include <map>

/// \brief strus toplevel namespace
using namespace strus;

unsigned int SymbolTable::getOrCreate( const std::string& key)
{
	std::string lokey = utils::tolower( key);
	Map::const_iterator fi = m_map.find( lokey);
	if (fi == m_map.end())
	{
		m_strings.push_back('\0');
		m_inv.push_back( m_strings.size());
		m_strings.append( key);
		return m_map[ lokey] = m_inv.size();
	}
	else
	{
		return fi->second;
	}
}

unsigned int SymbolTable::get( const std::string& key) const
{
	std::string lokey = utils::tolower( key);
	Map::const_iterator fi = m_map.find( lokey);
	if (fi == m_map.end())
	{
		return 0;
	}
	return fi->second;
}

/// \brief Inverse lookup of key
const char* SymbolTable::key( unsigned int idx) const
{
	if (idx >= m_inv.size()) return 0;
	return m_strings.c_str() + m_inv[ idx-1];
}




