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
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/constants.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "private/version.hpp"
#include "private/programOptions.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <memory>

static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& dbcfg, strus::ErrorBufferInterface* errorhnd)
{
	std::auto_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());
	if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbcfg);
	if (!dbi) throw strus::runtime_error(_TXT("failed to get database interface"));
	const strus::StorageInterface* sti = storageBuilder->getStorage();
	if (!sti) throw strus::runtime_error(_TXT("failed to get storage interface"));

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient), errorhnd);
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::StorageInterface::CmdCreateClient), errorhnd);
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
				argc, argv, 7,
				"h,help", "v,version", "m,module:", "M,moduledir:",
				"r,rpc:", "s,storage:", "e,exists");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 0)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer));
		if (!moduleLoader.get()) throw strus::runtime_error( _TXT("error creating module loader"));

		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
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
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
			std::vector<std::string> modlist( opt.list("module"));
			std::vector<std::string>::const_iterator mi = modlist.begin(), me = modlist.end();
			for (; mi != me; ++mi)
			{
				if (!moduleLoader->loadModule( *mi))
				{
					throw strus::runtime_error(_TXT("error failed to load module %s"), mi->c_str());
				}
			}
		}

		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusCheckStorage [options]" << std::endl;
			std::cout << _TXT("description: Checks a storage for corrupt data.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << _TXT("    <CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""), errorBuffer);
			}
			std::cout << "-e|--exists" << std::endl;
			std::cout << "    " << _TXT("Checks if the database of the storage exists and return 'yes'/'no'") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the commands on the RPC server specified by <ADDR>") << std::endl;
			return rt;
		}
		std::string storagecfg;
		if (opt("storage"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
			storagecfg = opt["storage"];
		}

		// Create objects to check storage:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer));
			if (!messaging.get()) throw strus::runtime_error( _TXT("error creating rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer));
			if (!rpcClient.get()) throw strus::runtime_error( _TXT("error creating rpc client"));
			(void)messaging.release();
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error( _TXT("error creating rpc storage object builder"));
		}
		else
		{
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error( _TXT("error creating storage object builder"));
		}
		if (opt("exists"))
		{
			const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( storagecfg);
			if (!dbi) throw strus::runtime_error(_TXT("could not find key/value store database"));

			if (dbi->exists( storagecfg))
			{
				std::cout << "yes" << std::endl;
			}
			else
			{
				std::cout << "no" << std::endl;
			}
			if (errorBuffer->hasError())
			{
				throw strus::runtime_error(_TXT("unhandled error in analyze"));
			}
		}
		else
		{
			std::auto_ptr<strus::StorageClientInterface>
				storage( storageBuilder->createStorageClient( storagecfg));
			if (!storage.get()) throw strus::runtime_error(_TXT("could not create storage client"));

			if (!storage->checkStorage( std::cerr))
			{
				if (errorBuffer->hasError())
				{
					throw strus::runtime_error(_TXT("error in check storage"));
				}
				std::cerr << _TXT("check storage failed") << std::endl;
			}
			else
			{
				std::cerr << _TXT("done") << std::endl;
			}
		}
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


