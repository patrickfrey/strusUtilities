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
#include "strus/storageAlterMetaDataTableInterface.hpp"
#include "programOptions.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/private/configParser.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <boost/algorithm/string.hpp>
#include <boost/scoped_ptr.hpp>

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
		throw std::runtime_error( std::string( "identifier (") + idname + ") expected at '" + std::string( si, se) + "'");
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
		std::string cmd( parseIdentifier( si, se, "command name"));
		if (boost::algorithm::iequals( cmd, "Alter"))
		{
			std::string name( parseIdentifier( si, se, "old element name"));
			std::string newname( parseIdentifier( si, se, "new element name"));
			std::string type( parseIdentifier( si, se, "new element type"));

			rt.push_back( AlterMetaDataCommand::AlterElement( name, newname, type));
		}
		else if (boost::algorithm::iequals( cmd, "Add"))
		{
			std::string name( parseIdentifier( si, se, "element name"));
			std::string type( parseIdentifier( si, se, "element type name"));

			rt.push_back( AlterMetaDataCommand::AddElement( name, type));
		}
		else if (boost::algorithm::iequals( cmd, "Rename"))
		{
			std::string name( parseIdentifier( si, se, "old element name"));
			std::string newname( parseIdentifier( si, se, "new element name"));

			rt.push_back( AlterMetaDataCommand::RenameElement( name, newname));
		}
		else if (boost::algorithm::iequals( cmd, "Delete"))
		{
			std::string name( parseIdentifier( si, se, "element name"));

			rt.push_back( AlterMetaDataCommand::DeleteElement( name));
		}
		else if (boost::algorithm::iequals( cmd, "Clear"))
		{
			std::string name( parseIdentifier( si, se, "element name"));
			
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
			throw std::runtime_error( std::string( "semicolon expected as separator of commands at '...") + std::string( si, si+30));
		}
	}
	return rt;
}


int main( int argc, const char* argv[])
{
	if (argc <= 1 || std::strcmp( argv[1], "-h") == 0 || std::strcmp( argv[1], "--help") == 0)
	{
		std::cerr << "usage: strusAlterMetaData <config> <cmds>" << std::endl;
		std::cerr << "<config>  : configuration string of the storage:" << std::endl;
		std::cerr << "            semicolon ';' separated list of assignments:" << std::endl;
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getDatabaseConfigDescription_leveldb(
						strus::CmdCreateDatabaseClient));
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
		return 0;
	}
	try
	{
		if (argc < 3) throw std::runtime_error( "too few arguments (expected storage configuration string)");
		if (argc > 3) throw std::runtime_error( "too many arguments for strusAlterMetaData");

		std::string config = argv[1];
		std::vector<AlterMetaDataCommand> cmds = parseCommands( argv[2]);

		boost::scoped_ptr<strus::DatabaseInterface>
			database( strus::createDatabaseClient_leveldb( config));

		boost::scoped_ptr<strus::StorageAlterMetaDataTableInterface>
			md( strus::createAlterMetaDataTable( database.get()));

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


