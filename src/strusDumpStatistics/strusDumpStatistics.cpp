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
#include "strus/lib/storage.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storagePeerInterface.hpp"
#include "strus/storagePeerTransactionInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/constants.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include "strus/private/protocol.hpp"
#include "private/version.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <boost/scoped_ptr.hpp>

namespace strus
{
	typedef boost::scoped_ptr<StorageInterface> StorageReference;
	typedef boost::scoped_ptr<StoragePeerInterface> StoragePeerReference;
	typedef boost::scoped_ptr<StoragePeerTransactionInterface> StoragePeerTransactionReference;
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
	if (argc > 1 && (std::strcmp( argv[1], "-v") == 0 || std::strcmp( argv[1], "--version") == 0))
	{
		std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
		std::cout << "Strus storage version " << STRUS_STORAGE_VERSION_STRING << std::endl;
		return 0;
	}
	if (argc <= 1 || std::strcmp( argv[1], "-h") == 0 || std::strcmp( argv[1], "--help") == 0)
	{
		std::cerr << "usage: strusDumpStatistics [options] <config>" << std::endl;
		std::cerr << "<config>  : configuration string of the storage" << std::endl;
		std::cerr << "            semicolon';' separated list of assignments:" << std::endl;
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getDatabaseConfigDescription_leveldb(
						strus::CmdCreateClient));
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getStorageConfigDescription(
						strus::CmdCreateClient));
		std::cerr << "options:" << std::endl;
		std::cerr << "-h,--help     : Print this usage info and exit" << std::endl;
		std::cerr << "-v,--version  : Print the version info and exit" << std::endl;
		return 0;
	}
	try
	{
		if (argc < 2) throw std::runtime_error( "too few arguments (expected storage configuration string)");

		std::string database_cfg( argv[1]);
		strus::removeKeysFromConfigString(
				database_cfg,
				strus::getStorageConfigParameters( strus::CmdCreateClient));
		//... In database_cfg is now the pure database configuration without the storage settings

		std::string storage_cfg( argv[1]);
		strus::removeKeysFromConfigString(
				storage_cfg,
				strus::getDatabaseConfigParameters_leveldb( strus::CmdCreateClient));
		//... In storage_cfg is now the pure storage configuration without the database settings

		boost::scoped_ptr<strus::DatabaseInterface>
			database( strus::createDatabaseClient_leveldb( database_cfg));

		boost::scoped_ptr<strus::StorageInterface>
			storage( strus::createStorageClient( storage_cfg, database.get()));

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


