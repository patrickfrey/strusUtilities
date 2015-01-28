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
#include "strus/storageLib.hpp"
#include "strus/postingIteratorInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/forwardIteratorInterface.hpp"
#include "strus/attributeReaderInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/index.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/private/arithmeticVariantAsString.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

namespace strus
{
	typedef boost::scoped_ptr<PostingIteratorInterface> PostingIteratorReference;
	typedef boost::scoped_ptr<StorageInterface> StorageReference;
	typedef boost::scoped_ptr<ForwardIteratorInterface> ForwardIteratorReference;
	typedef boost::scoped_ptr<MetaDataReaderInterface> MetaDataReaderReference;
}

static strus::Index stringToIndex( const char* value)
{
	std::ostringstream val;
	return boost::lexical_cast<strus::Index>( std::string( value));
}
static bool isIndex( char const* cc)
{
	while (*cc >= '0' && *cc <= '9') ++cc;
	return (*cc == '\0');
}

static void inspectPositions( strus::StorageInterface& storage, const char** key, int size)
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

static void inspectDocumentFrequency( strus::StorageInterface& storage, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 2) throw std::runtime_error( "too few arguments");

	strus::PostingIteratorReference itr(
		storage.createTermPostingIterator(
			std::string(key[0]), std::string(key[1])));
	std::cout << itr->documentFrequency();
}

static void inspectFeatureFrequency( strus::StorageInterface& storage, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 2) throw std::runtime_error( "too few arguments");
	strus::PostingIteratorReference itr(
		storage.createTermPostingIterator(
			std::string(key[0]), std::string(key[1])));

	std::cout << (*itr).frequency() << std::endl;
}

static void inspectDocAttribute( const strus::StorageInterface& storage, const char** key, int size)
{
	if (size > 2) throw std::runtime_error( "too many arguments");
	if (size < 2) throw std::runtime_error( "too few arguments");

	boost::scoped_ptr<strus::AttributeReaderInterface>
		attreader( storage.createAttributeReader());

	strus::Index docno = isIndex(key[1])
			?stringToIndex( key[1])
			:storage.documentNumber( key[1]);

	strus::Index hnd = attreader->elementHandle( key[0]);

	attreader->skipDoc( docno);
	std::string value = attreader->getValue( hnd);

	std::cout << value << std::endl;
}

static void inspectDocMetaData( const strus::StorageInterface& storage, const char** key, int size)
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

static void inspectDocMetaTable( const strus::StorageInterface& storage, const char**, int size)
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

static void inspectContent( strus::StorageInterface& storage, const char** key, int size)
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

static void inspectToken( strus::StorageInterface& storage, const char** key, int size)
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

static void inspectDocid( strus::StorageInterface& storage, const char** key, int size)
{
	if (size > 1) throw std::runtime_error( "too many arguments");
	if (size < 1) throw std::runtime_error( "too few arguments");

	std::cout << storage.documentNumber( key[0]) << std::endl;
}


int main( int argc, const char* argv[])
{
	if (argc <= 1 || std::strcmp( argv[1], "-h") == 0 || std::strcmp( argv[1], "--help") == 0)
	{
		std::cerr << "usage: strusInspect <config> <what...>" << std::endl;
		std::cerr << "<config>  : configuration string of the storage" << std::endl;
		strus::printIndentMultilineString(
					std::cerr,
					12, strus::getStorageConfigDescription(
						strus::CmdCreateStorageClient));
		std::cerr << "<what>    : what to inspect:" << std::endl;
		std::cerr << "            \"pos\" <typeno> <valueno> <doc-id/no>" << std::endl;
		std::cerr << "            \"ff\" <typeno> <valueno> <doc-id/no>" << std::endl;
		std::cerr << "            \"df\" <typeno> <valueno>" << std::endl;
		std::cerr << "            \"metadata\" <name> <doc-id/no>" << std::endl;
		std::cerr << "            \"metatable\"" << std::endl;
		std::cerr << "            \"attribute\" <name> <doc-id/no>" << std::endl;
		std::cerr << "            \"content\" <typeno> <doc-id/no>" << std::endl;
		std::cerr << "            \"token\" <typeno> <doc-id/no>" << std::endl;
		std::cerr << "            \"docno\" <docid>" << std::endl;
		return 0;
	}
	try
	{
		if (argc < 3) throw std::runtime_error( "too few arguments (expected storage configuration string)");

		strus::StorageReference storage(
			strus::createStorageClient( argv[1]));
		
		if (0==std::strcmp( argv[2], "pos"))
		{
			inspectPositions( *storage, argv+3, argc-3);
		}
		else if (0==std::strcmp( argv[2], "ff"))
		{
			inspectFeatureFrequency( *storage, argv+3, argc-3);
		}
		else if (0==std::strcmp( argv[2], "df"))
		{
			inspectDocumentFrequency( *storage, argv+3, argc-3);
		}
		else if (0==std::strcmp( argv[2], "metadata"))
		{
			inspectDocMetaData( *storage, argv+3, argc-3);
		}
		else if (0==std::strcmp( argv[2], "metatable"))
		{
			inspectDocMetaTable( *storage, argv+3, argc-3);
		}
		else if (0==std::strcmp( argv[2], "attribute"))
		{
			inspectDocAttribute( *storage, argv+3, argc-3);
		}
		else if (0==std::strcmp( argv[2], "content"))
		{
			inspectContent( *storage, argv+3, argc-3);
		}
		else if (0==std::strcmp( argv[2], "docno"))
		{
			inspectDocid( *storage, argv+3, argc-3);
		}
		else if (0==std::strcmp( argv[2], "token"))
		{
			inspectToken( *storage, argv+3, argc-3);
		}
		else
		{
			throw std::runtime_error( std::string( "unknown item name '") + argv[2] + "'");
		}
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


