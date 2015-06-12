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
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storagePeerInterface.hpp"
#include "strus/storagePeerTransactionInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/constants.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "strus/private/protocol.hpp"
#include "private/version.hpp"
#include "private/utils.hpp"
#include "private/programOptions.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <memory>


static void printStorageConfigOptions( std::ostream& out, const strus::ModuleLoaderInterface* moduleLoader, const std::string& dbcfg)
{
	std::auto_ptr<strus::StorageObjectBuilderInterface>
		storageBuilder( moduleLoader->createStorageObjectBuilder());

	const strus::DatabaseInterface* dbi = storageBuilder->getDatabase( dbcfg);
	const strus::StorageInterface* sti = storageBuilder->getStorage();

	strus::printIndentMultilineString(
				out, 12, dbi->getConfigDescription(
					strus::DatabaseInterface::CmdCreateClient));
	strus::printIndentMultilineString(
				out, 12, sti->getConfigDescription(
					strus::StorageInterface::CmdCreateClient));
}


class StorageStatsDumperInstance
	:public strus::StoragePeerTransactionInterface
{
public:
	StorageStatsDumperInstance(){}

	virtual ~StorageStatsDumperInstance(){}

	virtual void populateNofDocumentsInsertedChange(
			int increment)
	{
		std::cout << strus::Constants::storage_statistics_number_of_documents()
				<< ' ' << increment
				<< std::endl;
	}

	virtual void populateDocumentFrequencyChange(
			const char* termtype,
			const char* termvalue,
			int increment,
			bool isnew)
	{
		std::cout << strus::Constants::storage_statistics_document_frequency()
				<< ' ' << increment
				<< ' ' << termtype
				<< ' ' << strus::Protocol::encodeString( termvalue)
				<< std::endl;
	}

	virtual void try_commit(){}

	virtual void final_commit(){}

	virtual void rollback(){}
};

class StorageStatsDumper
	:public strus::StoragePeerInterface
{
public:
	StorageStatsDumper(){};
	virtual ~StorageStatsDumper(){}

	virtual strus::StoragePeerTransactionInterface* createTransaction() const
	{
		return new StorageStatsDumperInstance();
	}
};


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
				"r,rpc:", "s,storage:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << "Strus storage version " << STRUS_STORAGE_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 0)
			{
				std::cerr << "ERROR too many arguments" << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader());
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --moduledir and --rpc");
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
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --module and --rpc");
			std::vector<std::string> modlist( opt.list("module"));
			std::vector<std::string>::const_iterator mi = modlist.begin(), me = modlist.end();
			for (; mi != me; ++mi)
			{
				moduleLoader->loadModule( *mi);
			}
		}

		if (printUsageAndExit)
		{
			std::cout << "usage: strusDumpStatistics [options]" << std::endl;
			std::cout << "description: Dumps the statisics that would be populated to" << std::endl;
			std::cout << "    other peer storages in case of a distributed index to stout." << std::endl;
			std::cout << "options:" << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "   Print this usage and do nothing else" << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    Print the program version and do nothing else" << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    Define the storage configuration string as <CONFIG>" << std::endl;
			if (!opt("rpc"))
			{
				std::cout << "    <CONFIG> is a semicolon ';' separated list of assignments:" << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""));
			}
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    Load components from module <MOD>" << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    Search modules to load first in <DIR>" << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    Execute the command on the RPC server specified by <ADDR>" << std::endl;
			return rt;
		}
		std::string storagecfg;
		if (opt("storage"))
		{
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --moduledir and --rpc");
			storagecfg = opt["storage"];
		}

		// Create objects for dump:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"]));
			rpcClient.reset( strus::createRpcClient( messaging.get()));
			(void)messaging.release();
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
		}
		else
		{
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
		}
		std::auto_ptr<strus::StorageClientInterface>
			storage( storageBuilder->createStorageClient( storagecfg));

		StorageStatsDumper statsDumper;
		storage->defineStoragePeerInterface( &statsDumper, true/*do populate init state*/);
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


