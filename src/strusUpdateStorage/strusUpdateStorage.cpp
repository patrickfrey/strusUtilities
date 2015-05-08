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
#include "strus/programLoader.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/versionStorage.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "private/version.hpp"
#include "private/inputStream.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

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


int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 10,
				"h,help", "v,version", "m,module:", "M,moduledir:",
				"r,rpc:", "s,storage:", "c,commit:",
				"a,attribute:","m,metadata:","u,useraccess");
		if (opt( "help"))
		{
			printUsageAndExit = true;
		}
		if (opt( "version"))
		{
			std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << "Strus storage version " << STRUS_STORAGE_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else
		{
			if (opt.nofargs() < 2)
			{
				std::cerr << "ERROR too few arguments" << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() > 2)
			{
				std::cerr << "ERROR too many arguments" << std::endl;
				printUsageAndExit = true;
				rt = 2;
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
		if (printUsageAndExit)
		{
			std::cerr << "usage: strusUpdateStorage [options] <updatefile>" << std::endl;
			std::cerr << "<updatefile>  = file with the batch of updates ('-' for stdin)" << std::endl;
			std::cerr << "description: Executes a batch of storage updates." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "-h|--help" << std::endl;
			std::cerr << "    Print this usage and do nothing else" << std::endl;
			std::cerr << "-v|--version" << std::endl;
			std::cerr << "    Print the program version and do nothing else" << std::endl;
			std::cerr << "-m|--module <MOD>" << std::endl;
			std::cerr << "    Load components from module <MOD>" << std::endl;
			std::cerr << "-M|--moduledir <DIR>" << std::endl;
			std::cerr << "    Search modules to load first in <DIR>" << std::endl;
			std::cerr << "-r|--rpc <ADDR>" << std::endl;
			std::cerr << "    Execute the command on the RPC server specified by <ADDR>" << std::endl;
			std::cerr << "-s|--storage <CONFIG>" << std::endl;
			std::cerr << "    Define the storage configuration string as <CONFIG>" << std::endl;
			if (!opt("rpc"))
			{
				std::cerr << "    <CONFIG> is a semicolon ';' separated list of assignments:" << std::endl;
				printStorageConfigOptions( std::cerr, moduleLoader.get(), (opt("storage")?opt["storage"]:""));
			}
			std::cerr << "-a|--attribute <NAME>" << std::endl;
			std::cerr << "    The update batch is a list of attributes assignments" << std::endl;
			std::cerr << "    The name of the updated attribute is <NAME>." << std::endl;
			std::cerr << "-m|--metadata <NAME>" << std::endl;
			std::cerr << "    The update batch is a list of meta data assignments." << std::endl;
			std::cerr << "    The name of the updated meta data element is <NAME>." << std::endl;
			std::cerr << "-u|--useraccess" << std::endl;
			std::cerr << "    The update batch is a list of user right assignments." << std::endl;
			std::cerr << "-c|--commit <N>" << std::endl;
			std::cerr << "    Set <N> as number of updates per transaction (default 10000)" << std::endl;
			std::cerr << "    If <N> is set to 0 then only one commit is done at the end" << std::endl;
			return rt;
		}
		std::string storagecfg;
		if (opt("storage"))
		{
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --storage and --rpc");
			storagecfg = opt["storage"];
		}
		
		// Create objects for storage document update:
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
		strus::utils::ScopedPtr<strus::StorageClientInterface>
			storage( storageBuilder->createStorageClient( storagecfg));

		enum UpdateOperation
		{
			UpdateOpAttribute,
			UpdateOpMetadata,
			UpdateOpUserAccess
		};
		UpdateOperation updateOperation;
		std::string elemname;
		std::string updateBatchPath( opt[0]);

		if (opt("metadata"))
		{
			if (opt("attribute")) throw std::runtime_error("specified mutual exclusive options --attribute and --metadata");
			if (opt("useraccess")) throw std::runtime_error("specified mutual exclusive options --useraccess and --metadata");
			elemname = opt["metadata"];
			updateOperation = UpdateOpMetadata;
		}
		else if (opt("attribute"))
		{
			if (opt("useraccess")) throw std::runtime_error("specified mutual exclusive options --useraccess and --attribute");
			elemname = opt["attribute"];
			updateOperation = UpdateOpAttribute;
		}
		else if (opt("useraccess"))
		{
			updateOperation = UpdateOpUserAccess;
		}
		else
		{
			throw std::runtime_error("no update operation type specified as option (one of --attribute,--metadata,--useraccess is mandatory)");
		}
		strus::InputStream input( updateBatchPath);
		unsigned int nofUpdates = 0;
		unsigned int transactionSize = 10000;
		if (opt("commit"))
		{
			transactionSize = opt.asUint( "commit");
		}
		switch (updateOperation)
		{
			case UpdateOpMetadata:
				nofUpdates = strus::loadDocumentMetaDataAssignments(
						*storage, elemname, input.stream(), transactionSize);
				break;
			case UpdateOpAttribute:
				nofUpdates = strus::loadDocumentAttributeAssignments(
						*storage, elemname, input.stream(), transactionSize);
				break;
			case UpdateOpUserAccess:
				nofUpdates = strus::loadDocumentUserRightsAssignments(
						*storage, input.stream(), transactionSize);
				break;
		}
		std::cerr << "done " << nofUpdates << " update operations" << std::endl;
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


