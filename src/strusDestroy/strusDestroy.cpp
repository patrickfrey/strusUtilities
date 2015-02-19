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
#include "strus/lib/database_leveldb.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

int main( int argc, const char* argv[])
{
	if (argc > 1 && (std::strcmp( argv[1], "-v") == 0 || std::strcmp( argv[1], "--version") == 0))
	{
		std::cout << "Strus utilities " << STRUS_UTILITIES_VERSION_STRING << std::endl;
		return 0;
	}
	if (argc <= 1 || std::strcmp( argv[1], "-h") == 0 || std::strcmp( argv[1], "--help") == 0)
	{
		std::cerr << "usage: strusDestroy [options] <config>" << std::endl;
		std::cerr << "<config>  : configuration string of the storage" << std::endl;
		std::cerr << "            semicolon ';' separated list of assignments:" << std::endl;
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getDatabaseConfigDescription_leveldb(
						strus::CmdDestroyDatabase));
		std::cerr << "options:" << std::endl;
		std::cerr << "-h,--help     : Print this usage info and exit" << std::endl;
		std::cerr << "-v,--version  : Print the version info and exit" << std::endl;
		return 0;
	}
	try
	{
		if (argc < 2) throw std::runtime_error( "too few arguments (expected storage configuration string)");
		if (argc > 2) throw std::runtime_error( "too many arguments for strusDestroy");

		strus::destroyDatabase_leveldb( argv[1]);
	}
	catch (const std::runtime_error& e)
	{
		std::cerr << "ERROR " << e.what() << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cerr << "EXCEPTION " << e.what() << std::endl;
	}
	return -1;
}


