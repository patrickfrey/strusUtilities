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
#include "strus/lib/error.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/errorBufferInterface.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/configParser.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>


static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& dbcfg)
{
	std::auto_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());
	if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbcfg);
	if (dbi) throw strus::runtime_error(_TXT("failed to get database interface"));
	const strus::StorageInterface* sti = storageBuilder->getStorage();
	if (sti) throw strus::runtime_error(_TXT("failed to get storage interface"));

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreate));
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::StorageInterface::CmdCreate));
}


int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::ErrorBufferInterface* errorBuffer = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		errorBuffer = strus::createErrorBuffer_standard( stderr, 2);
		if (!errorBuffer) throw strus::runtime_error( _TXT("failed to create error buffer"));

		opt = strus::ProgramOptions(
				argc, argv, 6,
				"h,help", "v,version", "m,module:", "M,moduledir:",
				"s,storage:", "S,configfile:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 1)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("failed to create module loader"));
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
			std::string configfile = opt[ "configfile"];
			int ec = strus::readFile( configfile, databasecfg);
			if (ec) throw strus::runtime_error(_TXT("failed to read configuration file %s (errno %u)"), configfile.c_str(), ec);

			std::string::iterator di = databasecfg.begin(), de = databasecfg.end();
			for (; di != de; ++di)
			{
				if ((unsigned char)*di < 32) *di = ' ';
			}
		}
		if (opt("storage"))
		{
			nof_databasecfg += 1;
			databasecfg = opt[ "storage"];
		}
		if (opt.nofargs() == 1)
		{
			std::cerr << _TXT("warning: passing storage as first parameter instead of option -s (deprecated)") << std::endl;
			nof_databasecfg += 1;
			databasecfg = opt[0];
		}
		if (nof_databasecfg > 1)
		{
			std::cerr << _TXT("conflicting configuration options specified: --storage and --configfile") << std::endl;
			rt = 10003;
			printUsageAndExit = true;
		}
		else if (!printUsageAndExit && nof_databasecfg == 0)
		{
			std::cerr << _TXT("missing configuration option: --storage or --configfile has to be defined") << std::endl;
			rt = 10004;
			printUsageAndExit = true;
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusCreate [options]" << std::endl;
			std::cout << _TXT("description: Creates a storage with its key value store database.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
			printStorageConfigOptions( std::cout, moduleLoader.get(), databasecfg);
			std::cout << "-S|--configfile <FILENAME>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration file as <FILENAME>") << std::endl;
			std::cout << "    " << _TXT("<FILENAME> is a file containing the configuration string") << std::endl;
			return rt;
		}
		std::auto_ptr<strus::StorageObjectBuilderInterface>
			storageBuilder( moduleLoader->createStorageObjectBuilder());
		if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));

		const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( databasecfg);
		if (dbi) throw strus::runtime_error(_TXT("failed to get database interface"));
		const strus::StorageInterface* sti = storageBuilder->getStorage();
		if (sti) throw strus::runtime_error(_TXT("failed to get storage interface"));

		std::string dbname;
		(void)strus::extractStringFromConfigString( dbname, databasecfg, "database");
		std::string storagecfg( databasecfg);

		strus::removeKeysFromConfigString(
				databasecfg,
				sti->getConfigParameters(
					strus::StorageInterface::CmdCreateClient));
		//... In database_cfg is now the pure database configuration without the storage settings

		strus::removeKeysFromConfigString(
				storagecfg,
				dbi->getConfigParameters(
					strus::DatabaseInterface::CmdCreateClient));
		//... In storage_cfg is now the pure storage configuration without the database settings

		if (!dbi->createDatabase( databasecfg))
		{
			throw strus::runtime_error(_TXT("failed to create key/value store database files"));
		}

		strus::utils::ScopedPtr<strus::DatabaseClientInterface>
			database( dbi->createClient( databasecfg));
		if (!database.get()) throw strus::runtime_error(_TXT("failed to create database client"));

		if (!sti->createStorage( storagecfg, database.get()))
		{
			throw strus::runtime_error(_TXT("failed to create storage"));
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("unhandled error in create storage"));
		}
		std::cerr << _TXT("storage successfully created.") << std::endl;
		delete errorBuffer;
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
	}
	catch (const std::runtime_error& e)
	{
		const char* errormsg = errorBuffer?errorBuffer->fetchError():0;
		if (errormsg)
		{
			std::cerr << _TXT("ERROR ") << errormsg << ": " << e.what() << std::endl;
		}
		else
		{
			std::cerr << _TXT("ERROR ") << e.what() << std::endl;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << _TXT("EXCEPTION ") << e.what() << std::endl;
	}
	delete errorBuffer;
	return -1;
}


