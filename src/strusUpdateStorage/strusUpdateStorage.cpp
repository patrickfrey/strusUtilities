/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2015 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 3 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public
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
#include "strus/programLoader.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/versionStorage.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "private/version.hpp"
#include "private/inputStream.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

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
	FILE* logfile = 0;
	std::auto_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
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
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() < 1)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() > 1)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
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
				if (!moduleLoader->loadModule( *mi))
				{
					throw strus::runtime_error(_TXT("error failed to load module %s"), mi->c_str());
				}
			}
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusUpdateStorage [options] <updatefile>" << std::endl;
			std::cout << "<updatefile>  = " << _TXT("file with the batch of updates ('-' for stdin)") << std::endl;
			std::cout << _TXT("description: Executes a batch of updates of attributes, meta data") << std::endl;
			std::cout << "             " << _TXT("or user rights in a storage.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-s|--storage <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Define the storage configuration string as <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""), errorBuffer.get());
			}
			std::cout << "-a|--attribute <NAME>" << std::endl;
			std::cout << "    " << _TXT("The update batch is a list of attributes assignments") << std::endl;
			std::cout << "    " << _TXT("The name of the updated attribute is <NAME>.") << std::endl;
			std::cout << "-m|--metadata <NAME>" << std::endl;
			std::cout << "    " << _TXT("The update batch is a list of meta data assignments.") << std::endl;
			std::cout << "    " << _TXT("The name of the updated meta data element is <NAME>.") << std::endl;
			std::cout << "-u|--useraccess" << std::endl;
			std::cout << "    " << _TXT("The update batch is a list of user right assignments.") << std::endl;
			std::cout << "-c|--commit <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of updates per transaction (default 10000)") << std::endl;
			std::cout << "    " << _TXT("If <N> is set to 0 then only one commit is done at the end") << std::endl;
			std::cout << "-L|--logerror <FILE>" << std::endl;
			std::cout << "    " << _TXT("Write the last error occurred to <FILE> in case of an exception")  << std::endl;
			return rt;
		}
		if (opt("logerror"))
		{
			std::string filename( opt["logerror"]);
			logfile = fopen( filename.c_str(), "a+");
			if (!logfile) throw strus::runtime_error(_TXT("error loading log file '%s' for appending (errno %u)"), filename.c_str(), errno);
			errorBuffer->setLogFile( logfile);
		}
		std::string storagecfg;
		if (opt("storage"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--storage", "--rpc");
			storagecfg = opt["storage"];
		}
		
		// Create objects for storage document update:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error( _TXT("error creating rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
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
		strus::utils::ScopedPtr<strus::StorageClientInterface>
			storage( storageBuilder->createStorageClient( storagecfg));
		if (!storage.get()) throw strus::runtime_error(_TXT("could not create storage client"));

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
			if (opt("attribute")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s") ,"--attribute", "--metadata");
			if (opt("useraccess")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--useraccess", "--metadata");
			elemname = opt["metadata"];
			updateOperation = UpdateOpMetadata;
		}
		else if (opt("attribute"))
		{
			if (opt("useraccess")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--useraccess", "--attribute");
			elemname = opt["attribute"];
			updateOperation = UpdateOpAttribute;
		}
		else if (opt("useraccess"))
		{
			updateOperation = UpdateOpUserAccess;
		}
		else
		{
			throw strus::runtime_error(_TXT("no update operation type specified as option (one of %s is mandatory)"), "--attribute,--metadata,--useraccess");
		}
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
						*storage, elemname, updateBatchPath, transactionSize, errorBuffer.get());
				break;
			case UpdateOpAttribute:
				nofUpdates = strus::loadDocumentAttributeAssignments(
						*storage, elemname, updateBatchPath, transactionSize, errorBuffer.get());
				break;
			case UpdateOpUserAccess:
				nofUpdates = strus::loadDocumentUserRightsAssignments(
						*storage, updateBatchPath, transactionSize, errorBuffer.get());
				break;
		}
		if (!nofUpdates && errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("error in update storage"));
		}
		std::cerr << strus::utils::string_sprintf( _TXT("done %u update operations"), nofUpdates) << std::endl;
		if (logfile) fclose( logfile);
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
	}
	catch (const std::runtime_error& e)
	{
		const char* errormsg = errorBuffer->fetchError();
		if (errormsg)
		{
			std::cerr << _TXT("ERROR ") << e.what() << ": " << errormsg << std::endl;
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
	if (logfile) fclose( logfile);
	return -1;
}


