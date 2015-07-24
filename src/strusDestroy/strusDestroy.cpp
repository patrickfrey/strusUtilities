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
#include "strus/lib/module.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/versionStorage.hpp"
#include "strus/private/fileio.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>


static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& dbcfg)
{
	std::auto_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbcfg);

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient));
}


int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 6,
				"h,help", "v,version", "m,module:", "M,moduledir:",
				"s,storage:", "S,configfile:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << "Strus storage version " << STRUS_STORAGE_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 1)
			{
				std::cerr << "ERROR too many arguments" << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader());
		if (opt("moduledir"))
		{
			std::vector<std::string> modirlist( opt.list("moduledir"));
			std::vector<std::string>::const_iterator mi = modirlist.begin(), me = modirlist.end();
			for (; mi != me; ++mi)
			{
				moduleLoader->addModulePath( *mi);
			}
			moduleLoader->addSystemModulePath();
		}
		if (opt("module"))
		{
			std::vector<std::string> modlist( opt.list("module"));
			std::vector<std::string>::const_iterator mi = modlist.begin(), me = modlist.end();
			for (; mi != me; ++mi)
			{
				moduleLoader->loadModule( *mi);
			}
		}
		std::string databasecfg;
		int nof_databasecfg = 0;
		if (opt("configfile"))
		{
			nof_databasecfg += 1;
			int ec = strus::readFile( opt[ "configfile"], databasecfg);
			if (ec)
			{
				std::cerr << "ERROR failed to read configuration file " << opt[ "configfile"] << " (file system error " << ec << ")" << std::endl;
				rt = 2;
				printUsageAndExit = true;
			}
			std::string::iterator di = databasecfg.begin(), de = databasecfg.end();
			for (; di != de; ++di)
			{
				if ((unsigned char)*di < 32) *di = ' ';
			}
		}
		if (opt.nofargs() == 1)
		{
			std::cerr << "WARNING passing storage as first parameter instead of option -s (deprecated)" << std::endl;
			nof_databasecfg += 1;
			databasecfg = opt[0];
		}
		if (opt("storage"))
		{
			nof_databasecfg += 1;
			databasecfg = opt[ "storage"];
		}
		if (nof_databasecfg > 1)
		{
			std::cerr << "ERROR conflicting configuration options specified: --storage and --configfile" << std::endl;
			rt = 10003;
			printUsageAndExit = true;
		}
		else if (nof_databasecfg == 0)
		{
			std::cerr << "ERROR missing configuration option: --storage or --configfile has to be defined" << std::endl;
			rt = 10004;
			printUsageAndExit = true;
		}

		if (printUsageAndExit)
		{
			std::cout << "usage: strusDestroy [options] <config>" << std::endl;
			std::cout << "description: Removes an existing storage database." << std::endl;
			std::cout << "options:" << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "   Print this usage and do nothing else" << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    Print the program version and do nothing else" << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    Load components from module <MOD>" << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    Search modules to load first in <DIR>" << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    Define the storage configuration string as <CONFIG>" << std::endl;
			std::cout << "    <CONFIG> is a semicolon ';' separated list of assignments:" << std::endl;
			printStorageConfigOptions( std::cout, moduleLoader.get(), databasecfg);
			std::cout << "-S|--configfile <FILENAME>" << std::endl;
			std::cout << "    Define the storage configuration file as <FILENAME>" << std::endl;
			std::cout << "    <FILENAME> is a file containing the configuration string" << std::endl;
			return rt;
		}
		std::auto_ptr<strus::StorageObjectBuilderInterface>
			builder( moduleLoader->createStorageObjectBuilder());
		const strus::DatabaseInterface* dbi = builder->getDatabase( databasecfg);
		dbi->destroyDatabase( databasecfg);
		std::cerr << "storage successfully destroyed." << std::endl;
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


