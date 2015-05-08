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
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/arithmeticVariant.hpp"
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

static std::string::const_iterator skipSpaces( std::string::const_iterator si, const std::string::const_iterator& se)
{
	for (; si != se && (unsigned char)*si <= 32; ++si){}
	return si;
}

static std::pair<strus::Index,strus::ArithmeticVariant> readCmdLine( strus::InputStream& input)
{
	char buf[ 256];
	std::size_t bufsize = read( buf, sizeof(buf));
	
	std::pair<strus::Index,strus::ArithmeticVariant> rt;
	
}



int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 4,
				"h,help", "v,version", "m,module:", "M,moduledir:");
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
			std::cerr << "usage: strusAlterMetaData [options] <config> <cmds>" << std::endl;
			std::cerr << "<config>  : configuration string of the storage" << std::endl;
			std::cerr << "            semicolon';' separated list of assignments:" << std::endl;
			printStorageConfigOptions( std::cerr, moduleLoader.get(), (opt.nofargs()>=1?opt[0]:""));
			std::cerr << "<cmds>    : semicolon separated list of commands:" << std::endl;
			std::cerr << "            alter <name> <newname> <newtype>" << std::endl;
			std::cerr << "              <name>    :name of the element to change" << std::endl;
			std::cerr << "              <newname> :new name of the element" << std::endl;
			std::cerr << "              <newtype> :new type (*) of the element" << std::endl;
			std::cerr << "            add <name> <type>" << std::endl;
			std::cerr << "              <name>    :name of the element to add" << std::endl;
			std::cerr << "              <type>    :type (*) of the element to add" << std::endl;
			std::cerr << "            delete <name>" << std::endl;
			std::cerr << "              <name>    :name of the element to remove" << std::endl;
			std::cerr << "            rename <name> <newname>" << std::endl;
			std::cerr << "              <name>    :name of the element to rename" << std::endl;
			std::cerr << "              <newname> :new name of the element" << std::endl;
			std::cerr << "            clear <name>" << std::endl;
			std::cerr << "              <name>    :name of the element to clear all values" << std::endl;
			std::cerr << "(*)       :type of an element is one of the following:" << std::endl;
			std::cerr << "              INT8      :one byte signed integer value" << std::endl;
			std::cerr << "              UINT8     :one byte unsigned integer value" << std::endl;
			std::cerr << "              INT16     :two bytes signed integer value" << std::endl;
			std::cerr << "              UINT16    :two bytes unsigned integer value" << std::endl;
			std::cerr << "              INT32     :four bytes signed integer value" << std::endl;
			std::cerr << "              UINT32    :four bytes unsigned integer value" << std::endl;
			std::cerr << "              FLOAT16   :two bytes floating point value (IEEE 754 small)" << std::endl;
			std::cerr << "              FLOAT32   :four bytes floating point value (IEEE 754 single)" << std::endl;
			std::cerr << "description: Executes a list of alter the meta data table commands." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "-h|--help" << std::endl;
			std::cerr << "    Print this usage and do nothing else" << std::endl;
			std::cerr << "-v|--version" << std::endl;
			std::cerr << "    Print the program version and do nothing else" << std::endl;
			std::cerr << "-m|--module <MOD>" << std::endl;
			std::cerr << "    Load components from module <MOD>" << std::endl;
			std::cerr << "-M|--moduledir <DIR>" << std::endl;
			std::cerr << "    Search modules to load first in <DIR>" << std::endl;
			return rt;
		}
		std::string storagecfg = opt[0];
		std::vector<AlterMetaDataCommand> cmds = parseCommands( opt[1]);

		// Create objects for altering the meta data table:
		std::auto_ptr<strus::StorageObjectBuilderInterface> builder;
		std::auto_ptr<strus::StorageAlterMetaDataTableInterface> md;

		builder.reset( moduleLoader->createStorageObjectBuilder());
		md.reset( builder->createAlterMetaDataTable( storagecfg));

		// Execute alter meta data table commands:
		std::vector<AlterMetaDataCommand>::const_iterator ci = cmds.begin(), ce = cmds.end();
		for (; ci != ce; ++ci)
		{
			switch (ci->id())
			{
				case AlterMetaDataCommand::Alter:
					md->alterElement( ci->name(), ci->newname(), ci->type());
					break;
				case AlterMetaDataCommand::Add:
					md->addElement( ci->name(), ci->type());
					break;
				case AlterMetaDataCommand::Delete:
					md->deleteElement( ci->name());
					break;
				case AlterMetaDataCommand::Rename:
					md->renameElement( ci->name(), ci->newname());
					break;
				case AlterMetaDataCommand::Clear:
					md->clearElement( ci->name());
					break;
			}
		}
		std::cerr << "updating meta data table changes..." << std::endl;
		md->commit();
		std::cerr << "done" << std::endl;
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


