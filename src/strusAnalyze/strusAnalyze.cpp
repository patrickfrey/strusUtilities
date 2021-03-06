/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/analyzer_prgload_std.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/lib/filecrawler.hpp"
#include "strus/lib/fieldtrees.hpp"
#include "strus/fileCrawlerInterface.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "strus/storage/index.hpp"
#include "private/versionUtilities.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/analyzer/segmenterOptions.hpp"
#include "strus/reference.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/string_conv.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/programOptions.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include "private/documentAnalyzer.hpp"
#include "private/programLoader.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <cerrno>
#include <cstdio>
#include <algorithm>
#include <limits>

struct TermOrder
{
	bool operator()( const strus::analyzer::DocumentTerm& aa, const strus::analyzer::DocumentTerm& bb)
	{
		if (aa.pos() != bb.pos()) return (aa.pos() < bb.pos());
		int cmp;
		cmp = aa.type().compare( bb.type());
		if (cmp != 0) return (cmp < 0);
		cmp = aa.value().compare( bb.value());
		if (cmp != 0) return (cmp < 0);
		return false;
	}
};

static bool skipSpace( char const*& di)
{
	while (*di && (unsigned char)*di <= 32) ++di;
	return *di != '\0';
}

static void skipIdent( char const*& di)
{
	for (; *di && (((unsigned char)(*di|32) >= 'a' && (unsigned char)(*di|32) <= 'z') || *di == '_'); ++di){}
}

struct DumpConfigItem
{
	std::string value;
	int priority;

	DumpConfigItem()
		:value(),priority(std::numeric_limits<int>::min()){}
	DumpConfigItem( const std::string& value_, int priority_)
		:value(value_),priority(priority_){}
	DumpConfigItem( const DumpConfigItem& o)
		:value(o.value),priority(o.priority){}
};

typedef std::map<std::string,DumpConfigItem> DumpConfig;
typedef std::pair<std::string,DumpConfigItem> DumpConfigElem;

static DumpConfigElem getNextDumpConfigElem( char const*& di, int priority_)
{
	skipSpace( di);
	char const* de = di;
	skipIdent( de);
	std::string type( di, de-di);
	std::string value;
	di = de;
	skipSpace( di);
	if (*di == '=')
	{
		++di;
		skipSpace( di);
		if (*di == '\'' || *di == '\"')
		{
			char eb = *di++;
			char const* valstart = di;
			for (; *di && *di != eb; ++di){}
			value = strus::string_conv::unescape( std::string( valstart, di-valstart));
			if (*di) ++di;
			skipSpace( di);
		}
		else
		{
			char const* valstart = di;
			for (; *di && *di != ',' && (unsigned char)*di >= 32; ++di){}
			value.append( valstart, di-valstart);
			skipSpace( di);
		}
	}
	if (*di == ',')
	{
		++di;
	}
	else if (*di)
	{
		throw strus::runtime_error(_TXT("illegal token in dump configuration string at '%s'"), di);
	}
	return DumpConfigElem( type, DumpConfigItem( value, priority_));
}


static void filterTerms( std::vector<strus::analyzer::DocumentTerm>& termar, const DumpConfig& dumpConfig, const std::vector<strus::analyzer::DocumentTerm>& inputtermar)
{
	std::vector<strus::analyzer::DocumentTerm>::const_iterator
		ti = inputtermar.begin(), te = inputtermar.end();
	for (; ti != te; ++ti)
	{
		DumpConfig::const_iterator dci = dumpConfig.find( ti->type());
		if (dci != dumpConfig.end())
		{
			if (dci->second.value.empty())
			{
				termar.push_back( *ti);
			}
			else
			{
				termar.push_back( strus::analyzer::DocumentTerm( ti->type(), dci->second.value, ti->pos()));
			}
		}
	}
}

static void filterTermsUniquePosition( std::vector<strus::analyzer::DocumentTerm>& termar, const DumpConfig& dumpConfig, const std::vector<strus::analyzer::DocumentTerm>& inputtermar)
{
	strus::analyzer::DocumentTerm bestTerm;
	int bestPriority = std::numeric_limits<int>::min();

	std::vector<strus::analyzer::DocumentTerm>::const_iterator
		ti = inputtermar.begin(), te = inputtermar.end();
	for (; ti != te; ++ti)
	{
		if (bestTerm.defined() && ti->pos() > bestTerm.pos())
		{
			termar.push_back( bestTerm);
			bestTerm.clear();
			bestPriority = std::numeric_limits<int>::min();
		}
		DumpConfig::const_iterator dci = dumpConfig.find( ti->type());
		if (dci != dumpConfig.end() && bestPriority < dci->second.priority)
		{
			bestPriority = dci->second.priority;
			if (dci->second.value.empty())
			{
				bestTerm = *ti;
			}
			else
			{
				bestTerm = strus::analyzer::DocumentTerm( ti->type(), dci->second.value, ti->pos());
			}
		}
	}
	if (bestTerm.defined())
	{
		termar.push_back( bestTerm);
	}
}

static std::string getFileArg( const std::string& filearg, strus::ModuleLoaderInterface* moduleLoader)
{
	std::string programFileName = filearg;
	std::string programDir;
	int ec;

	if (strus::isExplicitPath( programFileName))
	{
		ec = strus::getParentPath( programFileName, programDir);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file directory from explicit path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		moduleLoader->addResourcePath( programDir);
	}
	else
	{
		std::string filedir;
		std::string filenam;
		ec = strus::getFileName( programFileName, filenam);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file name from absolute path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		ec = strus::getParentPath( programFileName, filedir);
		if (ec) throw strus::runtime_error( _TXT("failed to get program file directory from absolute path '%s': %s"), programFileName.c_str(), ::strerror(ec)); 
		programDir = filedir;
		programFileName = filenam;
		moduleLoader->addResourcePath( programDir);
	}
	return programFileName;
}

static bool compareOrderDocumentStructureByStartPos( const strus::analyzer::DocumentStructure& a, const strus::analyzer::DocumentStructure& b)
{
	if (a.source().start() == b.source().start())
	{
		if (a.source().end() == b.source().end())
		{
			if (a.sink().start() == b.sink().start())
			{
				if (a.sink().end() == b.sink().end())
				{
					return a.name() < b.name();
				}
				else
				{
					return a.sink().end() < b.sink().end();
				}
			}
			else
			{
				return a.sink().start() < b.sink().start();
			}
		}
		else
		{
			return a.source().end() < b.source().end();
		}
	}
	else
	{
		return a.source().start() < b.source().start();
	}
}

enum OutputMode {
	DumpOutput,
	UniqueDumpOutput,
	FeatureListOutput,
	StructureListOutput,
	StructureFieldTreeOutput
};

static std::string getMostUsedForwardIndexTerm( const strus::analyzer::Document& doc)
{
	std::map<std::string, std::set<strus::Index> > pmap;
	std::vector<strus::analyzer::DocumentTerm>::const_iterator
		fi = doc.forwardIndexTerms().begin(), fe = doc.forwardIndexTerms().end();
	for (; fi != fe; ++fi)
	{
		pmap[ fi->type()].insert( fi->pos());
	}
	std::size_t maxOccurrence = 0;
	std::string rt;
	std::map<std::string, std::set<strus::Index> >::const_iterator
		pi = pmap.begin(), pe = pmap.end();
	for (; pi != pe; ++pi)
	{
		std::size_t occurrence = pi->second.size();
		if (occurrence > maxOccurrence)
		{
			maxOccurrence = occurrence;
			rt = pi->first;
		}
	}
	return rt;
}

static std::map<strus::Index,std::string> getForwardIndexPosTermMap( const strus::analyzer::Document& doc)
{
	std::map<strus::Index,std::string> rt;
	std::string value = getMostUsedForwardIndexTerm( doc);
	std::vector<strus::analyzer::DocumentTerm>::const_iterator
		fi = doc.forwardIndexTerms().begin(), fe = doc.forwardIndexTerms().end();
	for (; fi != fe; ++fi)
	{
		rt[ fi->pos()] = fi->value();
	}
	return rt;
}

static std::string getFieldContentString( const strus::IndexRange& field, const std::map<strus::Index,std::string>& fmap)
{
	std::string rt;
	strus::Index pos = field.start();
	while (pos < field.end())
	{
		if (!rt.empty()) rt.push_back(' ');

		std::map<strus::Index,std::string>::const_iterator fi = fmap.lower_bound( pos);
		if (fi == fmap.end())
		{
			rt.append( "...");
			return rt;
		}
		else if (fi->first > pos)
		{
			rt.append( "...");
			if (fi->first < field.end()) rt.append( fi->second);
		}
		else
		{
			rt.append( fi->second);
		}
		pos = fi->first+1;
	}
	return rt;
}

static void printIndent( std::ostream& out, int depth)
{
	for (; depth>0; --depth) out << "  ";
}

static void printRange( std::ostream& out, const strus::IndexRange& range)
{
	out << strus::string_format("[%d,%d]", (int)range.start(), (int)range.end());
}

static void printFieldTreeContent( std::ostream& out, const strus::FieldTree& tree, const std::map<strus::Index,std::string>& fmap, int depth)
{
	printIndent( out, depth);
	printRange( out, tree.range);
	if (tree.chld.empty())
	{
		out << " " << getFieldContentString( tree.range, fmap) << std::endl;
	}
	else
	{
		out << ":" << std::endl;
		strus::FieldTree::const_iterator ci = tree.chld.begin(), ce = tree.chld.end();
		for (; ci != ce; ++ci)
		{
			printFieldTreeContent( out, *ci, fmap, depth+1);
		}
	}
}


static void analyzeDocument(
	const strus::DocumentAnalyzer& analyzerMap,
	const strus::TextProcessorInterface* textproc,
	const strus::analyzer::DocumentClass& documentClass,
	const std::string& docpath,
	OutputMode outputMode,
	const DumpConfig& dumpConfig)
{
	// Create the document analyzer context:
	const strus::DocumentAnalyzerInstanceInterface* analyzer = analyzerMap.get( documentClass);
	if (!analyzer)
	{
		throw strus::runtime_error( _TXT( "no analyzer defined for document class with MIME type '%s' schema '%s'"), documentClass.mimeType().c_str(), documentClass.schema().c_str()); 
	}
	strus::local_ptr<strus::DocumentAnalyzerContextInterface>
		analyzerContext( analyzer->createContext( documentClass));
	if (!analyzerContext.get()) throw std::runtime_error( _TXT("failed to create document analyzer context"));

	// Process the document:
	strus::InputStream input( docpath);
	enum {AnalyzerBufSize=8192};
	char buf[ AnalyzerBufSize];
	bool eof = false;
	while (!eof)
	{
		std::size_t readsize = input.read( buf, sizeof(buf));
		if (readsize < sizeof(buf))
		{
			if (input.error())
			{
				throw strus::runtime_error( _TXT("failed to read document file '%s': %s"), docpath.c_str(), ::strerror(input.error())); 
			}
			eof = true;
		}
		analyzerContext->putInput( buf, readsize, eof);

		// Analyze the document and print the result:
		strus::analyzer::Document doc;
		while (analyzerContext->analyzeNext( doc))
		{
			switch (outputMode)
			{
				case DumpOutput:
				case UniqueDumpOutput:
				{
					std::vector<strus::analyzer::DocumentTerm> termar;
					std::vector<strus::analyzer::DocumentMetaData>::const_iterator
						mi = doc.metadata().begin(), me = doc.metadata().end();
					for (; mi != me; ++mi)
					{
						DumpConfig::const_iterator dci = dumpConfig.find( mi->name());
						if (dci != dumpConfig.end())
						{
							if (dci->second.value.empty())
							{
								termar.push_back( strus::analyzer::DocumentTerm( mi->name(), mi->value().tostring().c_str(), 0/*pos*/));
							}
							else
							{
								termar.push_back( strus::analyzer::DocumentTerm( mi->name(), dci->second.value, 0/*pos*/));
							}
						}
					}
					std::vector<strus::analyzer::DocumentAttribute>::const_iterator
						ai = doc.attributes().begin(), ae = doc.attributes().end();
					for (; ai != ae; ++ai)
					{
						DumpConfig::const_iterator dci = dumpConfig.find( ai->name());
						if (dci != dumpConfig.end())
						{
							if (dci->second.value.empty())
							{
								termar.push_back( strus::analyzer::DocumentTerm( ai->name(), ai->value(), 0/*pos*/));
							}
							else
							{
								termar.push_back( strus::analyzer::DocumentTerm( ai->name(), dci->second.value, 0/*pos*/));
							}
						}
					}
					if (outputMode == UniqueDumpOutput)
					{
						filterTermsUniquePosition( termar, dumpConfig, doc.searchIndexTerms());
						filterTermsUniquePosition( termar, dumpConfig, doc.forwardIndexTerms());
					}
					else
					{
						filterTerms( termar, dumpConfig, doc.forwardIndexTerms());
						filterTerms( termar, dumpConfig, doc.searchIndexTerms());
					}
					std::sort( termar.begin(), termar.end(), TermOrder());

					std::vector<strus::analyzer::DocumentTerm>::const_iterator
						ti = termar.begin(), te = termar.end();
					for (unsigned int tidx=0; ti != te; ++ti,++tidx)
					{
						if (tidx) std::cout << ' ';
						std::cout << ti->value();
					}
					break;
				}
				case FeatureListOutput:
				{
					if (!doc.subDocumentTypeName().empty())
					{
						std::cout << "-- " << strus::string_format( _TXT("document type name %s"), doc.subDocumentTypeName().c_str()) << std::endl;
					}
					std::vector<strus::analyzer::DocumentTerm> itermar = doc.searchIndexTerms();
					std::sort( itermar.begin(), itermar.end(), TermOrder());
		
					std::vector<strus::analyzer::DocumentTerm>::const_iterator
						ti = itermar.begin(), te = itermar.end();

					std::cout << std::endl << _TXT("search index terms:") << std::endl;
					for (; ti != te; ++ti)
					{
						std::cout << ti->pos() << ":"
							  << " " << ti->type()
							  << " '" << ti->value() << "'"
							  << std::endl;
					}

					if (!doc.searchIndexStructures().empty())
					{
						std::vector<strus::analyzer::DocumentStructure>
							structlist = doc.searchIndexStructures();
						std::sort( structlist.begin(), structlist.end(), &compareOrderDocumentStructureByStartPos);

						std::cout << std::endl << _TXT("search index structures:") << std::endl;
						std::vector<strus::analyzer::DocumentStructure>::const_iterator
							si = structlist.begin(), se = structlist.end();
						for (; si != se; ++si)
						{
							std::cout
								<< strus::string_format("%s: [%d,%d] -> [%d,%d]",
									si->name().c_str(),
									(int)si->source().start(), (int)si->source().end(),
									(int)si->sink().start(), (int)si->sink().end())
								<< std::endl;
						}
					}

					std::vector<strus::analyzer::DocumentTerm> ftermar = doc.forwardIndexTerms();
					std::sort( ftermar.begin(), ftermar.end(), TermOrder());

					std::vector<strus::analyzer::DocumentTerm>::const_iterator
						fi = ftermar.begin(), fe = ftermar.end();

					std::cout << std::endl << _TXT("forward index terms:") << std::endl;
					for (; fi != fe; ++fi)
					{
						std::cout << fi->pos()
							  << " " << fi->type()
							  << " '" << fi->value() << "'"
							  << std::endl;
					}

					std::vector<strus::analyzer::DocumentMetaData>::const_iterator
						mi = doc.metadata().begin(), me = doc.metadata().end();
		
					std::cout << std::endl << _TXT("metadata:") << std::endl;
					for (; mi != me; ++mi)
					{
						std::cout << mi->name()
							  << " '" << mi->value().tostring().c_str() << "'"
							  << std::endl;
					}
	
					std::vector<strus::analyzer::DocumentAttribute>::const_iterator
						ai = doc.attributes().begin(), ae = doc.attributes().end();

					std::cout << std::endl << _TXT("attributes:") << std::endl;
					for (; ai != ae; ++ai)
					{
						std::cout << ai->name()
							  << " '" << ai->value() << "'"
							  << std::endl;
					}
					break;
				}
				case StructureListOutput:
				{
					if (!doc.searchIndexStructures().empty())
					{
						std::map<strus::Index,std::string>
							fmap = getForwardIndexPosTermMap( doc);
						std::vector<strus::analyzer::DocumentStructure>
							structlist = doc.searchIndexStructures();
						std::sort( structlist.begin(), structlist.end(), &compareOrderDocumentStructureByStartPos);

						std::cout << std::endl << _TXT("search index structures:") << std::endl;
						std::vector<strus::analyzer::DocumentStructure>::const_iterator
							si = structlist.begin(), se = structlist.end();
						for (; si != se; ++si)
						{
							strus::IndexRange source( si->source().start(), si->source().end());
							strus::IndexRange sink( si->sink().start(), si->sink().end());
							std::cout << si->name() << ": [[";
							std::cout << getFieldContentString( source, fmap);
							std::cout << "]] => [[";
							std::cout << getFieldContentString( sink, fmap);
							std::cout << "]]" << std::endl;
						}
					}
					break;
				}
				case StructureFieldTreeOutput:
				{
					if (!doc.searchIndexStructures().empty())
					{
						std::map<strus::Index,std::string>
							fmap = getForwardIndexPosTermMap( doc);
						strus::LocalErrorBuffer errorbuf;
						std::vector<strus::analyzer::DocumentStructure>
							structlist = doc.searchIndexStructures();
						std::vector<strus::IndexRange> fieldlist;
						std::vector<strus::analyzer::DocumentStructure>::const_iterator
							si = structlist.begin(), se = structlist.end();
						for (; si != se; ++si)
						{
							strus::IndexRange source( si->source().start(), si->source().end());
							strus::IndexRange sink( si->sink().start(), si->sink().end());
							fieldlist.push_back( source);
							fieldlist.push_back( sink);
						}
						std::vector<strus::IndexRange> rest;
						std::vector<strus::FieldTree> treelist = strus::buildFieldTrees( rest, fieldlist, &errorbuf);
						if (errorbuf.hasError()) throw strus::runtime_error( _TXT("error in field tree output: %s"), errorbuf.fetchError());
						if (!rest.empty())
						{
							std::cerr << _TXT("got field overlaps without complete coverage, structure fields not strictly hierarchical:") << std::endl;
							std::vector<strus::IndexRange>::const_iterator
								ri = rest.begin(), re = rest.end();
							for (; ri != re; ++ri)
							{
								std::cerr << "=> " << getFieldContentString( *ri, fmap) << std::endl;
							}
						}
						std::cout << std::endl << _TXT("search index trees:") << std::endl;
						std::vector<strus::FieldTree>::const_iterator ti = treelist.begin(), te = treelist.end();
						for (; ti != te; ++ti)
						{
							printFieldTreeContent( std::cout, *ti, fmap, 0);
						}
					}
					break;
				}
			}
		}
	}
}

int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::DebugTraceInterface* dbgtrace = strus::createDebugTrace_standard( 2);
	if (!dbgtrace)
	{
		std::cerr << _TXT("failed to create debug trace") << std::endl;
		return -1;
	}
	strus::local_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2, dbgtrace/*passed with ownership*/));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	try
	{
		bool printUsageAndExit = false;
		strus::ProgramOptions opt(
				errorBuffer.get(), argc, argv, 17,
				"h,help", "v,version", "license", "G,debug:", "m,module:",
				"M,moduledir:", "r,rpc:", "T,trace:", "R,resourcedir:",
				"g,segmenter:", "C,contenttype:", "x,extension:", "d,delim:",
				"D,dump:", "U,unique", "structlist", "fieldtree");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}
		if (opt( "help")) printUsageAndExit = true;
		// Enable debugging selected with option 'debug':
		{
			std::vector<std::string> dbglist = opt.list( "debug");
			std::vector<std::string>::const_iterator gi = dbglist.begin(), ge = dbglist.end();
			for (; gi != ge; ++gi)
			{
				if (!dbgtrace->enable( *gi))
				{
					throw strus::runtime_error(_TXT("failed to enable debug '%s'"), gi->c_str());
				}
			}
		}
		strus::local_ptr<strus::ModuleLoaderInterface>
				moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw std::runtime_error( _TXT("failed to create module loader"));

		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--moduledir", "--rpc");
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
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--module", "--rpc");
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
		if (opt("license"))
		{
			std::vector<std::string> licenses_3rdParty = moduleLoader->get3rdPartyLicenseTexts();
			std::vector<std::string>::const_iterator ti = licenses_3rdParty.begin(), te = licenses_3rdParty.end();
			if (ti != te) std::cout << _TXT("3rd party licenses:") << std::endl;
			for (; ti != te; ++ti)
			{
				std::cout << *ti << std::endl;
			}
			std::cout << std::endl;
			if (!printUsageAndExit) return 0;
		}
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus module version ") << STRUS_MODULE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus rpc version ") << STRUS_RPC_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus trace version ") << STRUS_TRACE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus base version ") << STRUS_BASE_VERSION_STRING << std::endl;
			std::vector<std::string> versions_3rdParty = moduleLoader->get3rdPartyVersionTexts();
			std::vector<std::string>::const_iterator vi = versions_3rdParty.begin(), ve = versions_3rdParty.end();
			if (vi != ve) std::cout << _TXT("3rd party versions:") << std::endl;
			for (; vi != ve; ++vi)
			{
				std::cout << *vi << std::endl;
			}
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 2)
			{
				std::cerr << _TXT("error too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 2)
			{
				std::cerr << _TXT("error too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusAnalyze [options] <program> <document>" << std::endl;
			std::cout << "<program>   = " << _TXT("path of analyzer program") << std::endl;
			std::cout << "<document>  = " << _TXT("path of document to analyze ('-' for stdin)") << std::endl;
			std::cout << _TXT("description: Analyzes a document and dumps the result to stdout.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-G|--debug <COMP>" << std::endl;
			std::cout << "    " << _TXT("Issue debug messages for component <COMP> to stderr") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-g|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME>") << std::endl;
			std::cout << "-C|--contenttype <CT>" << std::endl;
			std::cout << "    " << _TXT("forced definition of the document class of the document analyzed.") << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    " << _TXT("Grab only the files with extension <EXT> (default all files)") << std::endl;
			std::cout << "    " << _TXT("in case of a directory as input.") << std::endl;
			std::cout << "-d|--delim <DELIM>" << std::endl;
			std::cout << "    " << _TXT("Delimiter for multiple results (case input is a directory)") << std::endl;
			std::cout << "-D|--dump <DUMPCFG>" << std::endl;
			std::cout << "    " << _TXT("Dump ouput according <DUMPCFG>.") << std::endl;
			std::cout << "    " << _TXT("<DUMPCFG> is a comma separated list of types or type value assignments.") << std::endl;
			std::cout << "    " << _TXT("A type in <DUMPCFG> specifies the type to dump.") << std::endl;
			std::cout << "    " << _TXT("A value an optional replacement of the term value.") << std::endl;
			std::cout << "    " << _TXT("This kind of output is suitable for content analysis.") << std::endl;
			std::cout << "    " << _TXT("Structures are ommited in the output of a dump.") << std::endl;
			std::cout << "-U|--unique" << std::endl;
			std::cout << "    " << _TXT("Ouput dump (Option -D|--dump) only one element per ordinal position.") << std::endl;
			std::cout << "    " << _TXT("Order of priorization specified in dump configuration.") << std::endl;
			std::cout << "    " << _TXT("Structures are ommited in the output of a dump.") << std::endl;
			std::cout << "--structlist" << std::endl;
			std::cout << "    " << _TXT("Output list of structures with contents from forward index.") << std::endl;
			std::cout << "--fieldtree" << std::endl;
			std::cout << "    " << _TXT("Output tree of structure fields with contents from forward index.") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string segmenterName;
		std::string contenttype;
		DumpConfig dumpConfig;
		OutputMode outputMode = FeatureListOutput;
		std::string fileext;
		std::string resultDelimiter;

		if (opt( "segmenter"))
		{
			segmenterName = opt[ "segmenter"];
		}
		if (opt( "contenttype"))
		{
			contenttype = opt[ "contenttype"];
		}
		if (opt( "extension"))
		{
			fileext = opt[ "extension"];
			if (fileext.size() && fileext[0] != '.')
			{
				fileext = std::string(".") + fileext;
			}
		}
		if (opt( "delim"))
		{
			resultDelimiter = opt[ "delim"];
		}
		std::pair<const char*,const char*>
			conflict = opt.conflictingOpts( 3, "dump", "structlist", "fieldtree");
		if (conflict.first && conflict.second)
		{
			throw strus::runtime_error( _TXT("conflicting options: --%s and --%s"), conflict.first, conflict.second);
		}
		if (opt( "dump"))
		{
			strus::local_ptr<strus::DebugTraceContextInterface> dbgtracectx( dbgtrace->createTraceContext( "dump"));
			std::string ds = opt[ "dump"];
			char const* di = ds.c_str();
			int priority = -1;
			for (; skipSpace( di); priority--)
			{
				DumpConfigElem dt( getNextDumpConfigElem( di, priority));
				dumpConfig.insert( dt);
				if (dbgtracectx.get())
				{
					if (dt.second.value.empty())
					{
						dbgtracectx->event( "dump", "config [%s]", dt.first.c_str());
					}
					else
					{
						dbgtracectx->event( "dump", "config [%s] = '%s'", dt.first.c_str(), dt.second.value.c_str());
					}
				}
			}
			outputMode = opt("unique") ? UniqueDumpOutput : DumpOutput;
		}
		else if (opt("unique"))
		{
			throw strus::runtime_error(_TXT("option --unique makes only sense with option --dump"));
		}
		else if (opt("structlist"))
		{
			outputMode = StructureListOutput;
		}
		else if (opt("fieldtree"))
		{
			outputMode = StructureFieldTreeOutput;
		}

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
		// Set paths for locating resources:
		if (opt("resourcedir"))
		{
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options %s and %s"), "--resourcedir", "--rpc");
			std::vector<std::string> pathlist( opt.list("resourcedir"));
			std::vector<std::string>::const_iterator
				pi = pathlist.begin(), pe = pathlist.end();
			for (; pi != pe; ++pi)
			{
				moduleLoader->addResourcePath( *pi);
			}
		}
		std::string programFileName = getFileArg( opt[0], moduleLoader.get());
		std::string docpath = opt[1];

		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in initialization"));
		}

		// Create objects for analyzer:
		strus::local_ptr<strus::RpcClientMessagingInterface> messaging;
		strus::local_ptr<strus::RpcClientInterface> rpcClient;
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;

		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw std::runtime_error( _TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw std::runtime_error( _TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw std::runtime_error( _TXT("failed to create rpc analyzer object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw std::runtime_error( _TXT("failed to create analyzer object builder"));
		}

		// Create proxy objects if tracing enabled:
		{
			std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
			for (; ti != te; ++ti)
			{
				strus::AnalyzerObjectBuilderInterface* proxy = (*ti)->createProxy( analyzerBuilder.get());
				analyzerBuilder.release();
				analyzerBuilder.reset( proxy);
			}
		}
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw std::runtime_error( _TXT("failed to get text processor"));

		// Load the document and get its properties:
		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty())
		{
			documentClass = strus::parse_DocumentClass( contenttype, errorBuffer.get());
			if (!documentClass.defined() && errorBuffer->hasError())
			{
				throw std::runtime_error( _TXT("failed to parse document class"));
			}
		}
		bool docpathIsFile = docpath == "-" || strus::isFile( docpath);
		if (!docpathIsFile && !strus::isDir( docpath))
		{
			throw strus::runtime_error( _TXT("input file/directory '%s' does not exist"), docpath.c_str());
		}
		// Detect document content type if not explicitely defined:
		if (!documentClass.defined() && docpathIsFile)
		{
			strus::InputStream input( docpath);
			char hdrbuf[ 4096];
			std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
			if (input.error())
			{
				throw strus::runtime_error( _TXT("failed to read document file '%s': %s"), docpath.c_str(), ::strerror(input.error())); 
			}
			if (!textproc->detectDocumentClass( documentClass, hdrbuf, hdrsize, hdrsize < sizeof(hdrbuf)))
			{
				throw strus::runtime_error( "%s",  _TXT("failed to detect document class")); 
			}
		}
		// Load analyzer program(s):
		strus::DocumentAnalyzer analyzerMap( analyzerBuilder.get(), documentClass, segmenterName, programFileName, errorBuffer.get());

		// Do analyze document(s):
		if (docpathIsFile)
		{
			analyzeDocument( analyzerMap, textproc, documentClass, docpath, outputMode, dumpConfig);
			if (errorBuffer->hasError())
			{
				throw std::runtime_error( _TXT("error in analyze document"));
			}
		}
		else
		{
			strus::local_ptr<strus::FileCrawlerInterface> fileCrawler( strus::createFileCrawlerInterface( docpath, 1/*fetchSize*/, fileext, errorBuffer.get()));
			if (!fileCrawler.get()) throw std::runtime_error( errorBuffer->fetchError());

			std::vector<std::string> files;
			std::vector<std::string>::const_iterator fitr;

			while (!(files=fileCrawler->fetch()).empty())
			{
				fitr = files.begin();
				for (int fidx=0; fitr != files.end(); ++fitr,++fidx)
				{
					if (fidx) std::cout << std::endl << resultDelimiter;
					analyzeDocument( analyzerMap, textproc, documentClass, *fitr, outputMode, dumpConfig);
				}
			}
		}
		std::cerr << _TXT("done.") << std::endl;
		if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
		{
			std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
		}
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
		return -2;
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
	if (!dumpDebugTrace( dbgtrace, NULL/*filename ~ NULL = stderr*/))
	{
		std::cerr << _TXT("failed to dump debug trace to file") << std::endl;
	}
	return -1;
}



