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
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/reference.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseClientInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/postingIteratorInterface.hpp"
#include "strus/forwardIteratorInterface.hpp"
#include "strus/documentTermIteratorInterface.hpp"
#include "strus/attributeReaderInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/index.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionBase.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/numericVariant.hpp"
#include "strus/base/configParser.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
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

namespace strus
{
	typedef strus::utils::ScopedPtr<PostingIteratorInterface> PostingIteratorReference;
	typedef strus::utils::ScopedPtr<DocumentTermIteratorInterface> DocumentTermIteratorReference;
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
	if (size > 3) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 2) throw strus::runtime_error( _TXT("too few arguments"));

	strus::PostingIteratorReference itr(
		storage.createTermPostingIterator(
			std::string(key[0]), std::string(key[1])));
	if (!itr.get()) throw strus::runtime_error(_TXT("failed to create term posting iterator"));

	if (size == 2)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
		for (; docno <= maxDocno; ++docno)
		{
			docno = itr->skipDoc( docno);
			if (!docno) break;

			std::cout << docno << ':';
			strus::Index pos=0;
			while (0!=(pos=itr->skipPos(pos+1)))
			{
				std::cout << ' ' << pos;
			}
			std::cout << std::endl;
		}
	}
	else
	{
		strus::Index docno = isIndex(key[2])
				?stringToIndex( key[2])
				:storage.documentNumber( key[2]);
		if (docno)
		{
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
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
}

static void inspectDocumentIndexTerms( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 1) throw strus::runtime_error( _TXT("too few arguments"));

	strus::DocumentTermIteratorReference itr(
		storage.createDocumentTermIterator( std::string(key[0])));
	if (!itr.get()) throw strus::runtime_error(_TXT("failed to create document term iterator"));

	if (size == 1)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
		for (; docno <= maxDocno; ++docno)
		{
			if (!itr->skipDoc( docno)) continue;

			std::cout << docno << ':' << std::endl;
			strus::DocumentTermIteratorInterface::Term term;
			while (itr->nextTerm( term))
			{
				std::string termstr = itr->termValue( term.termno);
				std::cout << "\t" << term.firstpos << ' ' << term.tf << ' ' << termstr << std::endl;
			}
		}
	}
	else
	{
		strus::Index docno = isIndex(key[1])
				?stringToIndex( key[1])
				:storage.documentNumber( key[1]);
		if (docno)
		{
			if (itr->skipDoc( docno))
			{
				strus::DocumentTermIteratorInterface::Term term;
				while (itr->nextTerm( term))
				{
					std::string termstr = itr->termValue( term.termno);
					std::cout << term.firstpos << ' ' << term.tf << ' ' << termstr << std::endl;
				}
			}
		}
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
}

static void inspectDocumentFrequency( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 2) throw strus::runtime_error( _TXT("too few arguments"));

	strus::PostingIteratorReference itr(
		storage.createTermPostingIterator(
			std::string(key[0]), std::string(key[1])));
	if (!itr.get()) throw strus::runtime_error(_TXT("failed to create term posting iterator"));
	std::cout << itr->documentFrequency() << std::endl;
}

static void inspectDocumentTermTypeStats( strus::StorageClientInterface& storage, strus::StorageClientInterface::DocumentStatisticsType stat, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 1) throw strus::runtime_error( _TXT("too few arguments"));

	if (size == 1)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
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
		if (docno)
		{
			std::cout << storage.documentStatistics( docno, stat, key[0]) << std::endl;
		}
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
}

static void inspectFeatureFrequency( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 3) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 2) throw strus::runtime_error( _TXT("too few arguments"));

	strus::PostingIteratorReference itr(
		storage.createTermPostingIterator(
			std::string(key[0]), std::string(key[1])));
	if (!itr.get()) throw strus::runtime_error(_TXT("failed to create term posting iterator"));

	if (size == 2)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
		for (; docno <= maxDocno; ++docno)
		{
			docno = itr->skipDoc( docno);
			if (!docno) break;
			std::cout << docno << ' ' << (*itr).frequency() << std::endl;
		}
	}
	else
	{
		strus::Index docno = isIndex( key[2])
				?stringToIndex( key[2])
				:storage.documentNumber( key[2]);
		if (docno)
		{
			if (docno == itr->skipDoc( docno))
			{
				std::cout << (*itr).frequency() << std::endl;
			}
			else
			{
				std::cout << '0' << std::endl;
			}
		}
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
}

static void inspectNofDocuments( const strus::StorageClientInterface& storage, const char**, int size)
{
	if (size > 0) throw strus::runtime_error( _TXT("too many arguments"));
	std::cout << storage.nofDocumentsInserted() << std::endl;
}

static void inspectMaxDocumentNumber( const strus::StorageClientInterface& storage, const char**, int size)
{
	if (size > 0) throw strus::runtime_error( _TXT("too many arguments"));
	std::cout << storage.maxDocumentNumber() << std::endl;
}

static void inspectDocAttribute( const strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 1) throw strus::runtime_error( _TXT("too few arguments"));

	std::auto_ptr<strus::AttributeReaderInterface>
		attreader( storage.createAttributeReader());
	if (!attreader.get()) throw strus::runtime_error(_TXT("failed to create attribute reader"));
	strus::Index hnd = attreader->elementHandle( key[0]);

	if (size == 1)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
		for (; docno <= maxDocno; ++docno)
		{
			attreader->skipDoc( docno);
			std::string value = attreader->getValue( hnd);
			if (value.size())
			{
				std::cout << docno << ' ' << value << std::endl;
			}
		}
	}
	else
	{
		strus::Index docno = isIndex(key[1])
				?stringToIndex( key[1])
				:storage.documentNumber( key[1]);

		if (docno)
		{
			attreader->skipDoc( docno);
			std::string value = attreader->getValue( hnd);
			std::cout << value << std::endl;
		}
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
}

static void inspectDocAttributeNames( const strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 0) throw strus::runtime_error( _TXT("too many arguments"));

	std::auto_ptr<strus::AttributeReaderInterface>
		attreader( storage.createAttributeReader());
	if (!attreader.get()) throw strus::runtime_error(_TXT("failed to create attribute reader"));

	std::vector<std::string> alist = attreader->getAttributeNames();
	std::vector<std::string>::const_iterator ai = alist.begin(), ae = alist.end();

	for (; ai != ae; ++ai)
	{
		std::cout << *ai << std::endl;
	}
}

static void inspectDocMetaData( const strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 1) throw strus::runtime_error( _TXT("too few arguments"));

	strus::MetaDataReaderReference metadata( storage.createMetaDataReader());
	if (!metadata.get()) throw strus::runtime_error(_TXT("failed to create meta data reader"));
	strus::Index hnd = metadata->elementHandle( key[0]);
	if (size == 1)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
		for (; docno <= maxDocno; ++docno)
		{
			metadata->skipDoc( docno);
			strus::NumericVariant value = metadata->getValue( hnd);
			if (value.defined())
			{
				std::cout << docno << ' ' << value.tostring().c_str() << std::endl;
			}
		}
	}
	else
	{
		strus::Index docno = isIndex(key[1])
				?stringToIndex( key[1])
				:storage.documentNumber( key[1]);

		if (docno)
		{
			metadata->skipDoc( docno);
			strus::NumericVariant value = metadata->getValue( hnd);
			if (value.defined())
			{
				std::cout << value.tostring().c_str() << std::endl;
			}
			else
			{
				std::cout << "NULL" << std::endl;
			}
		}
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
}

static void inspectDocMetaTable( const strus::StorageClientInterface& storage, const char**, int size)
{
	if (size > 0) throw strus::runtime_error( _TXT("too many arguments"));

	strus::MetaDataReaderReference metadata( storage.createMetaDataReader());
	if (!metadata.get()) throw strus::runtime_error(_TXT("failed to create meta data reader"));

	strus::Index ei = 0, ee = metadata->nofElements();
	for (; ei != ee; ++ei)
	{
		std::cout << metadata->getName( ei) << " " << metadata->getType( ei) << std::endl;
	}
	std::cout << std::endl;
}

static void inspectContent( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 1) throw strus::runtime_error( _TXT("too few arguments"));

	strus::ForwardIteratorReference viewer( storage.createForwardIterator( std::string(key[0])));
	if (!viewer.get()) throw strus::runtime_error(_TXT("failed to create forward index iterator"));
	if (size == 1)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
		for (; docno <= maxDocno; ++docno)
		{
			viewer->skipDoc( docno);
			if (0 != viewer->skipPos(0))
			{
				std::cout << docno << ":";
				strus::Index pos=0;
				while (0!=(pos=viewer->skipPos(pos+1)))
				{
					std::cout << ' ' << viewer->fetch();
				}
				std::cout << std::endl;
			}
		}
	}
	else
	{
		strus::Index docno = isIndex(key[1])
				?stringToIndex( key[1])
				:storage.documentNumber( key[1]);
		if (docno)
		{
			viewer->skipDoc( docno);
			strus::Index pos=0;
			for (int idx=0; 0!=(pos=viewer->skipPos(pos+1)); ++idx)
			{
				if (idx) std::cout << ' ';
				std::cout << viewer->fetch();
			}
			std::cout << std::endl;
		}
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
}

static void fillForwardIndexStats(
		strus::StorageClientInterface& storage,
		strus::ForwardIteratorReference& viewer,
		std::map<std::string,unsigned int>& statmap,
		const strus::Index& docno)
{
	viewer->skipDoc( docno);
	strus::Index pos=0;
	while (0!=(pos=viewer->skipPos(pos+1)))
	{
		std::string value = viewer->fetch();
		statmap[ value] += 1;
	}
}

static std::string mapForwardIndexToken( const std::string& tok)
{
	const char* cntrl = "\a\b\t\n\v\f\r";
	const char* csubs = "abtnvfr";

	std::string val;
	std::string::const_iterator vi = tok.begin(), ve = tok.end();
	for (; vi != ve; ++vi)
	{
		char const* cp;
		if (*vi == '\'' || *vi == '\\')
		{
			val.push_back( '\\');
			val.push_back( *vi);
		}
		else if (0 != (cp = std::strchr( cntrl, *vi)))
		{
			val.push_back( '\\');
			val.push_back( csubs[ cp - cntrl]);
		}
		else
		{
			val.push_back( *vi);
		}
	}
	return val;
}

static std::string mapCntrlToSpace( const std::string& tok)
{
	const char* cntrl = "\a\b\t\n\v\f\r";
	std::string val;
	std::string::const_iterator vi = tok.begin(), ve = tok.end();
	for (; vi != ve; ++vi)
	{
		char const* cp;
		if (0 != (cp = std::strchr( cntrl, *vi)))
		{
			val.push_back( ' ');
		}
		else
		{
			val.push_back( *vi);
		}
	}
	return val;
}

static void inspectForwardIndexStats( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 1) throw strus::runtime_error( _TXT("too few arguments"));

	strus::ForwardIteratorReference viewer( storage.createForwardIterator( std::string(key[0])));
	if (!viewer.get()) throw strus::runtime_error(_TXT("failed to create forward index iterator"));
	std::map<std::string,unsigned int> statmap;
	if (size == 1)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
		for (; docno <= maxDocno; ++docno)
		{
			fillForwardIndexStats( storage, viewer, statmap, docno);
		}
	}
	else
	{
		strus::Index docno = isIndex(key[1])
				?stringToIndex( key[1])
				:storage.documentNumber( key[1]);
		if (docno)
		{
			fillForwardIndexStats( storage, viewer, statmap, docno);
		}
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
	std::map<std::string,unsigned int>::const_iterator si = statmap.begin(), se = statmap.end();
	for (; si != se; ++si)
	{
		std::cout << "'" << mapForwardIndexToken( si->first) << "' " << si->second << std::endl;
	}
}

static void inspectForwardIndexMap( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 1) throw strus::runtime_error( _TXT("too few arguments"));

	strus::ForwardIteratorReference viewer( storage.createForwardIterator( std::string(key[0])));
	if (!viewer.get()) throw strus::runtime_error(_TXT("failed to create forward index iterator"));
	if (size == 1)
	{
		strus::Index maxDocno = storage.maxDocumentNumber();
		strus::Index docno = 1;
		for (; docno <= maxDocno; ++docno)
		{
			viewer->skipDoc( docno);
			strus::Index pos=0;
			while (0!=(pos=viewer->skipPos(pos+1)))
			{
				std::string value = viewer->fetch();
				std::cout << docno << " " << mapCntrlToSpace(value) << std::endl;
			}
		}
	}
	else
	{
		strus::Index docno = isIndex(key[1])
				?stringToIndex( key[1])
				:storage.documentNumber( key[1]);
		if (docno)
		{
			viewer->skipDoc( docno);
			strus::Index pos=0;
			while (0!=(pos=viewer->skipPos(pos+1)))
			{
				std::string value = viewer->fetch();
				std::cout << mapCntrlToSpace(value) << std::endl;
			}
		}
		else
		{
			throw strus::runtime_error( _TXT("unknown document"));
		}
	}
}

static void inspectToken( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 2) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 2) throw strus::runtime_error( _TXT("too few arguments"));

	strus::Index docno = isIndex(key[1])
			?stringToIndex( key[1])
			:storage.documentNumber( key[1]);

	if (docno)
	{
		strus::ForwardIteratorReference viewer( storage.createForwardIterator( std::string(key[0])));
		if (!viewer.get()) throw strus::runtime_error(_TXT("failed to create forward index iterator"));
		viewer->skipDoc( docno);
		strus::Index pos=0;
		while (0!=(pos=viewer->skipPos(pos+1)))
		{
			std::cout << "[" << pos << "] " << mapForwardIndexToken( viewer->fetch()) << std::endl;
		}
	}
	else
	{
		throw strus::runtime_error( _TXT("unknown document"));
	}
}

static void inspectDocno( strus::StorageClientInterface& storage, const char** key, int size)
{
	if (size > 1) throw strus::runtime_error( _TXT("too many arguments"));
	if (size < 1) throw strus::runtime_error( _TXT("too few arguments"));

	std::cout << storage.documentNumber( key[0]) << std::endl;
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
				argc, argv, 7,
				"h,help", "v,version", "m,module:", "M,moduledir:",
				"r,rpc:", "s,storage:", "T,trace:");
		if (opt( "help"))
		{
			printUsageAndExit = true;
		}
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus base version ") << STRUS_BASE_VERSION_STRING << std::endl;
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
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("failed to create module loader"));
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
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--module", "--rpc");
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
			std::cout << _TXT("usage:") << " strusInspect [options] <what...>" << std::endl;
			std::cout << "<what>    : " << _TXT("what to inspect:") << std::endl;
			std::cout << "            \"pos\" <type> <value> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the list of positions for a search index term.") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"ff\" <type> <value> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the feature frequency for a search index feature") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"df\" <type> <value>" << std::endl;
			std::cout << "               = " << _TXT("Get the document frequency for a search index feature") << std::endl;
			std::cout << "            \"ttf\" <type> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the term type frequency in a document") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"ttc\" <type> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the term type count (distinct) in a document") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"indexterms\" <type> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the list of tuples of term value, first position and ff ") << std::endl;
			std::cout << "                 " << _TXT("for a search index term type.") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"nofdocs\"" << std::endl;
			std::cout << "               = " << _TXT("Get the number of documents in the storage") << std::endl;
			std::cout << "            \"maxdocno\"" << std::endl;
			std::cout << "               = " << _TXT("Get the maximum document number allocated in the storage") << std::endl;
			std::cout << "            \"metadata\" <name> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the value of a meta data element") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"metatable\"" << std::endl;
			std::cout << "               = " << _TXT("Get the schema of the meta data table") << std::endl;
			std::cout << "            \"attribute\" <name> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the value of a document attribute") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"attrnames\"" << std::endl;
			std::cout << "               = " << _TXT("Get the list of all attribute names defined for the storage") << std::endl;
			std::cout << "            \"content\" <type> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the content of the forward index for a type") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump content for all docs.") << std::endl;
			std::cout << "            \"fwstats\" <type> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Get the statistis of the forward index for a type") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"fwmap\" <type> [<doc-id/no>]" << std::endl;
			std::cout << "               = " << _TXT("Print a map docno to forward index element for a type") << std::endl;
			std::cout << "                 " << _TXT("If document is not specified then dump value for all docs.") << std::endl;
			std::cout << "            \"token\" <type> <doc-id/no>" << std::endl;
			std::cout << "               = " << _TXT("Get the list of terms in the forward index for a type") << std::endl;
			std::cout << "            \"docno\" <docid>" << std::endl;
			std::cout << "               = " << _TXT("Get the internal document number for a document id") << std::endl;
			std::cout << _TXT("description: Inspect some data in the storage.") << std::endl;
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
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			if (!opt("rpc"))
			{
				std::cout << "    " << _TXT("<CONFIG> is a semicolon ';' separated list of assignments:") << std::endl;
				printStorageConfigOptions( std::cout, moduleLoader.get(), (opt("storage")?opt["storage"]:""), errorBuffer.get());
			}
			return rt;
		}
		// Parse arguments:
		std::string storagecfg;
		if (opt("storage"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--storage", "--rpc");
			storagecfg = opt["storage"];
		}
		std::string what = opt[0];
		const char** inpectarg = opt.argv() + 1;
		std::size_t inpectargsize = opt.nofargs() - 1;

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

		// Create objects for inspecting storage:
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

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::StorageObjectBuilderInterface* sproxy = (*ti)->createProxy( storageBuilder.get());
			storageBuilder.release();
			storageBuilder.reset( sproxy);
		}

		// Do inspect what is requested:
		std::auto_ptr<strus::StorageClientInterface>
			storage( strus::createStorageClient( storageBuilder.get(), errorBuffer.get(), storagecfg));
		if (!storage.get()) throw strus::runtime_error(_TXT("failed to create storage client"));

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
		else if (strus::utils::caseInsensitiveEquals( what, "indexterms"))
		{
			inspectDocumentIndexTerms( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "nofdocs"))
		{
			inspectNofDocuments( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "maxdocno"))
		{
			inspectMaxDocumentNumber( *storage, inpectarg, inpectargsize);
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
		else if (strus::utils::caseInsensitiveEquals( what, "attrnames"))
		{
			inspectDocAttributeNames( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "content"))
		{
			inspectContent( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "fwstats"))
		{
			inspectForwardIndexStats( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "fwmap"))
		{
			inspectForwardIndexMap( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "docno"))
		{
			inspectDocno( *storage, inpectarg, inpectargsize);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "token"))
		{
			inspectToken( *storage, inpectarg, inpectargsize);
		}
		else
		{
			throw strus::runtime_error( _TXT( "unknown item to inspect '%s'"), what.c_str());
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("unhandled error in inspect storage"));
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


