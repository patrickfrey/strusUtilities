/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/storage_objbuild.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageAlterMetaDataTableInterface.hpp"
#include "strus/versionBase.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "private/version.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/reference.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/configParser.hpp"
#include "private/traceUtils.hpp"
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

class AlterMetaDataCommand
{
public:
	enum Id {Alter, Add, Delete, Rename, Clear};

	AlterMetaDataCommand( Id id_, const std::string& name_, const std::string& newname_, const std::string& type_)
		:m_id(id_),m_name(name_),m_newname(newname_),m_type(type_){}
	AlterMetaDataCommand( const AlterMetaDataCommand& o)
		:m_id(o.m_id),m_name(o.m_name),m_newname(o.m_newname),m_type(o.m_type){}

	static AlterMetaDataCommand AlterElement( const std::string& name_, const std::string& newname_, const std::string& type_)
	{
		return AlterMetaDataCommand( Alter, name_, newname_, type_);
	}

	static AlterMetaDataCommand AddElement( const std::string& name_, const std::string& type_)
	{
		return AlterMetaDataCommand( Add, name_, "", type_);
	}

	static AlterMetaDataCommand RenameElement( const std::string& name_, const std::string& newname_)
	{
		return AlterMetaDataCommand( Rename, name_, newname_, "");
	}

	static AlterMetaDataCommand DeleteElement( const std::string& name_)
	{
		return AlterMetaDataCommand( Delete, name_, "", "");
	}

	static AlterMetaDataCommand ClearValue( const std::string& name_)
	{
		return AlterMetaDataCommand( Clear, name_, "", "");
	}

	Id id() const						{return m_id;}
	const std::string& name() const				{return m_name;}
	const std::string& newname() const			{return m_newname;}
	const std::string& type() const				{return m_type;}

private:
	Id m_id;
	std::string m_name;
	std::string m_newname;
	std::string m_type;
};

static std::string::const_iterator skipSpaces( std::string::const_iterator si, const std::string::const_iterator& se)
{
	for (; si != se && (unsigned char)*si <= 32; ++si){}
	return si;
}

bool isIdentifier( char ch)
{
	if ((ch|32) >= 'a' && (ch|32) <= 'z') return true;
	if (ch >= '0' && ch <= '9') return true;
	if (ch == '_') return true;
	return false;
}

static std::string parseIdentifier( std::string::const_iterator& si, const std::string::const_iterator& se, const char* idname)
{
	si = skipSpaces( si, se);
	if (!isIdentifier( *si))
	{
		std::string str( si, se);
		throw strus::runtime_error( _TXT( "identifier (%s) expected at '%s'"), idname, str.c_str());
	}
	std::string rt;
	for (; si != se && isIdentifier( *si); ++si)
	{
		rt.push_back( *si);
	}
	return rt;
}

static std::vector<AlterMetaDataCommand> parseCommands( const std::string& source)
{
	std::vector<AlterMetaDataCommand> rt;
	std::string::const_iterator si = source.begin(), se = source.end();

	for (si = skipSpaces( si, se); si != se; si = skipSpaces( si, se))
	{
		std::string cmd( parseIdentifier( si, se, _TXT("command name")));
		if (strus::utils::caseInsensitiveEquals( cmd, "Alter"))
		{
			std::string name( parseIdentifier( si, se, _TXT("old element name")));
			std::string newname( parseIdentifier( si, se, _TXT("new element name")));
			std::string type( parseIdentifier( si, se, _TXT("new element type")));

			rt.push_back( AlterMetaDataCommand::AlterElement( name, newname, type));
		}
		else if (strus::utils::caseInsensitiveEquals( cmd, "Add"))
		{
			std::string name( parseIdentifier( si, se, _TXT("element name")));
			std::string type( parseIdentifier( si, se, _TXT("element type name")));

			rt.push_back( AlterMetaDataCommand::AddElement( name, type));
		}
		else if (strus::utils::caseInsensitiveEquals( cmd, "Rename"))
		{
			std::string name( parseIdentifier( si, se, _TXT("old element name")));
			std::string newname( parseIdentifier( si, se, _TXT("new element name")));

			rt.push_back( AlterMetaDataCommand::RenameElement( name, newname));
		}
		else if (strus::utils::caseInsensitiveEquals( cmd, "Delete"))
		{
			std::string name( parseIdentifier( si, se, _TXT("element name")));

			rt.push_back( AlterMetaDataCommand::DeleteElement( name));
		}
		else if (strus::utils::caseInsensitiveEquals( cmd, "Clear"))
		{
			std::string name( parseIdentifier( si, se, _TXT("element name")));
			
			rt.push_back( AlterMetaDataCommand::ClearValue( name));
		}
		si = skipSpaces( si, se);
		if (si == se)
		{
			break;
		}
		else if (*si == ';')
		{
			++si;
		}
		else
		{
			std::string str( si, si+30);
			throw strus::runtime_error( _TXT( "semicolon expected as separator of commands at '..."), str.c_str());
		}
	}
	return rt;
}


int main( int argc, const char* argv[])
{
	int rt = 0;
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
				argc, argv, 5,
				"h,help", "v,version", "m,module:", "M,moduledir:", "T,trace:");
		if (opt( "help"))
		{
			printUsageAndExit = true;
		}
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus module version ") << STRUS_MODULE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus rpc version ") << STRUS_RPC_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus trace version ") << STRUS_TRACE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus base version ") << STRUS_BASE_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() < 2)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() > 2)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader(
			strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("error creating module loader"));

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
			std::cout << _TXT("usage:") << " strusAlterMetaData [options] <config> <cmds>" << std::endl;
			std::cout << "<config>  : " << _TXT("configuration string of the storage") << std::endl;
			std::cout << "            " << _TXT("semicolon';' separated list of assignments:") << std::endl;
			printStorageConfigOptions( std::cout, moduleLoader.get(), (opt.nofargs()>=1?opt[0]:""), errorBuffer.get());
			std::cout << "<cmds>    : " << _TXT("semicolon separated list of commands:") << std::endl;
			std::cout << "            alter <name> <newname> <newtype>" << std::endl;
			std::cout << "              <name>    :" << _TXT("name of the element to change") << std::endl;
			std::cout << "              <newname> :" << _TXT("new name of the element") << std::endl;
			std::cout << "              <newtype> :" << _TXT("new type (*) of the element") << std::endl;
			std::cout << "            add <name> <type>" << std::endl;
			std::cout << "              <name>    :" << _TXT("name of the element to add") << std::endl;
			std::cout << "              <type>    :" << _TXT("type (*) of the element to add") << std::endl;
			std::cout << "            delete <name>" << std::endl;
			std::cout << "              <name>    :" << _TXT("name of the element to remove") << std::endl;
			std::cout << "            rename <name> <newname>" << std::endl;
			std::cout << "              <name>    :" << _TXT("name of the element to rename") << std::endl;
			std::cout << "              <newname> :" << _TXT("new name of the element") << std::endl;
			std::cout << "            clear <name>" << std::endl;
			std::cout << "              <name>    :" << _TXT("name of the element to clear all values") << std::endl;
			std::cout << "(*)       :" << _TXT("type of an element is one of the following:") << std::endl;
			std::cout << "              INT8      :" << _TXT("one byte signed integer value") << std::endl;
			std::cout << "              UINT8     :" << _TXT("one byte unsigned integer value") << std::endl;
			std::cout << "              INT16     :" << _TXT("two bytes signed integer value") << std::endl;
			std::cout << "              UINT16    :" << _TXT("two bytes unsigned integer value") << std::endl;
			std::cout << "              INT32     :" << _TXT("four bytes signed integer value") << std::endl;
			std::cout << "              UINT32    :" << _TXT("four bytes unsigned integer value") << std::endl;
			std::cout << "              FLOAT16   :" << _TXT("two bytes floating point value (IEEE 754 small)") << std::endl;
			std::cout << "              FLOAT32   :" << _TXT("four bytes floating point value (IEEE 754 single)") << std::endl;
			std::cout << _TXT("description: Executes a list of alter the meta data table commands.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string storagecfg = opt[0];
		std::vector<AlterMetaDataCommand> cmds = parseCommands( opt[1]);

		// Declare trace proxy objects:
		typedef strus::Reference<strus::TraceProxy> TraceReference;
		std::vector<TraceReference> trace;
		if (opt("trace"))
		{
			std::vector<std::string> tracecfglist( opt.list("trace"));
			std::vector<std::string>::const_iterator ti = tracecfglist.begin(), te = tracecfglist.end();
			for (; ti != te; ++ti)
			{
				trace.push_back( new strus::TraceProxy( moduleLoader.get(), *ti, errorBuffer.get()));
			}
		}

		// Create objects for altering the meta data table:
		std::auto_ptr<strus::StorageObjectBuilderInterface> builder;
		std::auto_ptr<strus::StorageAlterMetaDataTableInterface> md;

		builder.reset( moduleLoader->createStorageObjectBuilder());
		if (!builder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::StorageObjectBuilderInterface* builderproxy = (*ti)->createProxy( builder.get());
			builder.release();
			builder.reset( builderproxy);
		}

		md.reset( strus::createAlterMetaDataTable( builder.get(), errorBuffer.get(), storagecfg));
		if (!md.get()) throw strus::runtime_error(_TXT("failed to create storage alter metadata table structure"));

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
		std::cerr << _TXT("updating meta data table changes...") << std::endl;
		if (!md->commit()) throw strus::runtime_error(_TXT("alter meta data commit failed"));

		std::cerr << _TXT("done") << std::endl;
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("unhandled error in alter meta data"));
		}
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
	return -1;
}


