/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Implements a symbol table with implicit symbol definition with lookup in both directions
/// \file symbolTable.hpp
#ifndef _STRUS_UTILITIES_PROGRAM_SYMBOL_TABLE_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_SYMBOL_TABLE_INCLUDED
#include <string>
#include <vector>
#include <map>

/// \brief strus toplevel namespace
namespace strus {

class SymbolTable
{
public:
	/// \brief Default constructor
	SymbolTable(){}
	/// \brief Copy constructor
	SymbolTable( const SymbolTable& o)
		:m_map(o.m_map),m_inv(o.m_inv){}

	/// \brief Lookup of id of a key with implicit definition if not defined
	unsigned int getOrCreate( const std::string& key);

	/// \brief Lookup of id
	unsigned int get( const std::string& key) const;

	/// \brief Inverse lookup of key
	const char* key( unsigned int idx) const;

	const std::vector<std::size_t> invmap() const		{return m_inv;}
	const std::string strings() const			{return m_strings;}

private:
	typedef std::map<std::string,unsigned int> Map;
	typedef std::vector<std::size_t> Inv;

	Map m_map;
	Inv m_inv;
	std::string m_strings;
};
}//namespace
#endif


