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
#include "strus/postingIteratorInterface.hpp"
#include "strus/forwardIteratorInterface.hpp"
#include "strus/attributeReaderInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/index.hpp"
#include "strus/versionStorage.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/private/arithmeticVariantAsString.hpp"
#include "strus/private/configParser.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "private/utils.hpp"
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

namespace strus
{
	typedef strus::utils::ScopedPtr<PostingIteratorInterface> PostingIteratorReference;
	typedef strus::utils::ScopedPtr<ForwardIteratorInterface> ForwardIteratorReference;
	typedef strus::utils::ScopedPtr<MetaDataReaderInterface> MetaDataReaderReference;
}

static strus::Index stringToIndex( const char* value)
{
	std::ostringstream val;
	return strus::utils::toint( std::string( value));
}
static bool isIndex( char const* cc)
{
	while (*cc >= '0' && *cc <= '9') ++cc;
	return (*cc == '\0');
}

static void inspectPositions( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 3) throw std::runtime_error( "too many arguments");
	if (size < 3) throw std::runtime_error( "too few arguments");
	strus::PostingIteratorReference itr(
		storage.createTermPostingIterator(
			std::string(key[0]), std::string(key[1])));

	strus::Index docno = isIndex(key[2])
			?stringToIndex( key[2])
			:storage.documentNumber( key[2]);
	if (docno == itr->skipDoc( docno))
	{
		strus::Index pos=0;
		int cnt = 0;
		while (0!=(pos=itr->skipPos(pos+1)))
		{
			if (cnt++ != 0) std::cout << " ";
			std::cout << pos;
		}
		std::cout << std::endl;
	}
}

static void inspectDocumentFrequency( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 2) throw std::runtime_error( "too few arguments");

	strus::PostingIteratorReference itr(
		storage.createTermPostingIterator(
			std::string(key[0]), std::string(key[1])));
	std::cout << itr->documentFrequency() << std::endl;
}

static void inspectDocumentTermTypeStats( strus::StorageClientInterface& storage, strus::StorageClientInterface::DocumentStatisticsType stat, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 1) throw std::runtime_error( "too few arguments");

	if (size == 1)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 0;
		for (; docno <= maxDocno; ++docno)
		{
			std::cout << docno << ' ' << storage.documentStatistics( docno, stat, key[0]) << std::endl;
		}
	}
	else
	{
		strus::Index docno = isIndex( key[1])
				?stringToIndex( key[1])
				:storage.documentNumber( key[1]);
		std::cout << storage.documentStatistics( docno, stat, key[0]) << std::endl;
	}
}

static void inspectFeatureFrequency( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 3) throw std::runtime_error( "too many arguments");
	if (size < 3) throw std::runtime_error( "too few arguments");

	strus::PostingIteratorReference itr(
		storage.createTermPostingIterator(
			std::string(key[0]), std::string(key[1])));

	strus::Index docno = isIndex( key[2])
			?stringToIndex( key[2])
			:storage.documentNumber( key[2]);
	if (docno == itr->skipDoc( docno))
	{
		std::cout << (*itr).frequency() << std::endl;
	}
	else
	{
		std::cout << '0' << std::endl;
	}
}

static void inspectNofDocuments( const strus::StorageClientInterface& storage, const char**, int size)
{
	if (size > 0) throw std::runtime_error( "too many arguments");
	std::cout << storage.localNofDocumentsInserted() << std::endl;
}

static void inspectDocAttribute( const strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 2) throw std::runtime_error( "too few arguments");

	std::auto_ptr<strus::AttributeReaderInterface>
		attreader( storage.createAttributeReader());

	strus::Index docno = isIndex(key[1])
			?stringToIndex( key[1])
			:storage.documentNumber( key[1]);

	strus::Index hnd = attreader->elementHandle( key[0]);

	attreader->skipDoc( docno);
	std::string value = attreader->getValue( hnd);

	std::cout << value << std::endl;
}

static void inspectDocMetaData( const strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 2) throw std::runtime_error( "too few arguments");

	strus::MetaDataReaderReference metadata( storage.createMetaDataReader());

	strus::Index docno = isIndex(key[1])
			?stringToIndex( key[1])
			:storage.documentNumber( key[1]);

	strus::Index hnd = metadata->elementHandle( key[0]);

	metadata->skipDoc( docno);
	strus::ArithmeticVariant value = metadata->getValue( hnd);

	std::cout << value << std::endl;
}

static void inspectDocMetaTable( const strus::StorageClientInterface& storage, const char**, int size)
{
	if (size > 0) throw std::runtime_error( "too many arguments");

	strus::MetaDataReaderReference metadata( storage.createMetaDataReader());

	strus::Index ei = 0, ee = metadata->nofElements();
	for (; ei != ee; ++ei)
	{
		std::cout << metadata->getName( ei) << " " << metadata->getType( ei) << std::endl;
	}
	std::cout << std::endl;
}

static void inspectContent( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 2) throw std::runtime_error( "too few arguments");

	strus::Index docno = isIndex(key[1])
			?stringToIndex( key[1])
			:storage.documentNumber( key[1]);

	strus::ForwardIteratorReference viewer( storage.createForwardIterator( std::string(key[0])));
	viewer->skipDoc( docno);
	strus::Index pos=0;
	for (int idx=0; 0!=(pos=viewer->skipPos(pos+1)); ++idx)
	{
		if (idx) std::cout << ' ';
		std::cout << viewer->fetch();
	}
	std::cout << std::endl;
}

static void inspectToken( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 2) throw std::runtime_error( "too few arguments");

	strus::Index docno = isIndex(key[1])
			?stringToIndex( key[1])
			:storage.documentNumber( key[1]);

	strus::ForwardIteratorReference viewer( storage.createForwardIterator( std::string(key[0])));
	viewer->skipDoc( docno);
	strus::Index pos=0;
	while (0!=(pos=viewer->skipPos(pos+1)))
	{
		std::cout << "[" << pos << "] " << viewer->fetch() << std::endl;
	}
}

static void inspectDocid( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 1) throw std::runtime_error( "too many arguments");
	if (size < 1) throw std::runtime_error( "too few arguments");

	std::cout << storage.documentNumber( key[0]) << std::endl;
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
				"r,rpc:", "s,storage:");
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
			if (opt.nofargs() < 1)
			{
				std::cerr << "ERROR too few arguments" << std::endl;
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
			std::cerr << "usage: strusInspect [options] <what...>" << std::endl;
			std::cerr << "<what>    : what to inspect:" << std::endl;
			std::cerr << "            \"pos\" <type> <value> <doc-id/no>" << std::endl;
			std::cerr << "               = Get the list of positions for a search index feature" << std::endl;
			std::cerr << "            \"ff\" <type> <value> <doc-id/no>" << std::endl;
			std::cerr << "               = Get the feature frequency for a search index feature" << std::endl;
			std::cerr << "            \"df\" <type> <value>" << std::endl;
			std::cerr << "               = Get the document frequency for a search index feature" << std::endl;
			std::cerr << "            \"ttf\" <type> {<doc-id/no>}" << std::endl;
			std::cerr << "               = Get the term type frequency in a document" << std::endl;
			std::cerr << "                 If the document is not specified, then for all docs" << std::endl;
			std::cerr << "            \"ttc\" <type> {<doc-id/no>} (term type count in doc)" << std::endl;
			std::cerr << "               = Get the term type count (distinct) in a document" << std::endl;
			std::cerr << "                 If the document is not specified, then for all docs" << std::endl;
			std::cerr << "            \"nofdocs\"" << std::endl;
			std::cerr << "               = Get the local number of documents in the storage" << std::endl;
			std::cerr << "            \"metadata\" <name> <doc-id/no>" << std::endl;
			std::cerr << "               = Get the value of a meta data element" << std::endl;
			std::cerr << "            \"metatable\"" << std::endl;
			std::cerr << "               = Get the schema of the meta data table" << std::endl;
			std::cerr << "            \"attribute\" <name> <doc-id/no>" << std::endl;
			std::cerr << "               = Get the value of a document attribute" << std::endl;
			std::cerr << "            \"content\" <type> <doc-id/no>" << std::endl;
			std::cerr << "               = Get the content of the forward index for a type" << std::endl;
			std::cerr << "            \"token\" <type> <doc-id/no>" << std::endl;
			std::cerr << "               = Get the list of terms in the forward index for a type" << std::endl;
			std::cerr << "            \"docno\" <docid>" << std::endl;
			std::cerr << "               = Get the internal local document number for a document id" << std::endl;
			std::cerr << "description: Inspect some data in the storage." << std::endl;
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
			return rt;
		}

		std::string storagecfg;
		if (opt("storage"))
		{
			if (opt("rpc")) throw std::runtime_error("specified mutual exclusive options --storage and --rpc");
			storagecfg = opt["storage"];
		}
		std::string what = opt[0];
		const char** inpectarg = opt.argv() + 1;
		std::size_t inpectargsize = opt.nofargs() - 1;

		// Create objects for inspecting storage:
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

		// Do inspect what is requested:
		if (strus::utils::caseInsensitiveEquals( what, "pos"))
		{
			inspectPositions( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "ff"))
		{
			inspectFeatureFrequency( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "df"))
		{
			inspectDocumentFrequency( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "ttf"))
		{
			inspectDocumentTermTypeStats( *storage, strus::StorageClientInterface::StatNofTermOccurrencies, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "ttc"))
		{
			inspectDocumentTermTypeStats( *storage, strus::StorageClientInterface::StatNofTerms, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "nofdocs"))
		{
			inspectNofDocuments( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "metadata"))
		{
			inspectDocMetaData( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "metatable"))
		{
			inspectDocMetaTable( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "attribute"))
		{
			inspectDocAttribute( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "content"))
		{
			inspectContent( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "docno"))
		{
			inspectDocid( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "token"))
		{
			inspectToken( *storage, inpectarg, inpectargsize);
		}
		else
		{
			throw std::runtime_error( std::string( "unknown item to inspect '") + what + "'");
		}
		return 0;
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


