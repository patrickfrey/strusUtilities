/*
 * Copyright (c) 2018 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/error.hpp"
#include "strus/lib/postagger_std.hpp"
#include "strus/lib/module.hpp"
#include "strus/lib/filecrawler.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/lib/analyzer_prgload_std.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/string_conv.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/thread.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "strus/reference.hpp"
#include "strus/documentClassDetectorInterface.hpp"
#include "strus/posTaggerInterface.hpp"
#include "strus/posTaggerInstanceInterface.hpp"
#include "strus/posTaggerDataInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/segmenterInstanceInterface.hpp"
#include "strus/segmenterContextInterface.hpp"
#include "private/fileCrawlerInterface.hpp"
#include "strus/tokenizerFunctionInterface.hpp"
#include "strus/tokenizerFunctionInstanceInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "private/versionUtilities.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <memory>

static strus::ErrorBufferInterface* g_errorBuffer = 0;	// error buffer
static bool g_verbose = false;

enum EntityIdType {EntityIdGroup=1,EntityIdId=2,EntityIdValue=3};
enum {NofEntityIdTypes=4};

typedef std::pair<std::string,std::string> EntityDef;

static std::string normalizeSpaces( const std::string& src)
{
	std::string rt;
	char const* si = src.c_str();
	for (; *si; ++si)
	{
		if ((unsigned char)*si <= 32)
		{
			if (rt.empty() || rt[ rt.size()-1] == ' ')
			{}
			else
			{
				rt.push_back( ' ');
			}
		}
		else
		{
			rt.push_back( *si);
		}
	}
	return rt;
}

static std::vector<EntityDef> extractReferencedEntities(
		const strus::SegmenterInstanceInterface* entitySegmenter,
		const strus::analyzer::DocumentClass& documentclass,
		const std::string& content)
{
	std::string id;
	std::string value;
	std::set<EntityDef> eset;
	strus::SegmenterPosition pos;
	const char* segment = 0;
	std::size_t segmentsize = 0;
	strus::local_ptr<strus::SegmenterContextInterface> eseg( entitySegmenter->createContext( documentclass));

	if (!eseg.get())
	{
		throw strus::runtime_error( _TXT("failed to create segmenter context for extracting entities: %s"), g_errorBuffer->fetchError());
	}
	eseg->putInput( content.c_str(), content.size(), true/*eof*/);
	int eid;
	while (eseg->getNext( eid, pos, segment, segmentsize))
	{
		EntityIdType etype = (EntityIdType)(eid % NofEntityIdTypes);
		std::string estr = std::string( segment, segmentsize);
		switch (etype)
		{
			case EntityIdGroup:
				if (!id.empty() || !value.empty())
				{
					eset.insert( EntityDef( id, normalizeSpaces(value)));
					id.clear();
					value.clear();
				}
			break;
			case EntityIdId:
				estr = strus::string_conv::trim( estr);
				if (estr.empty()) continue;
				if (!id.empty() && id != estr)
				{
					throw strus::runtime_error( _TXT("failed to extract entities: duplicate definition"));
				}
				id = estr;
			break;
			case EntityIdValue:
				if (estr.empty()) continue;
				value.append( estr);
			break;
		}
	}
	if (!id.empty() || !value.empty())
	{
		eset.insert( EntityDef( id, normalizeSpaces(value)));
	}
	return std::vector<EntityDef>( eset.begin(), eset.end());
}

/// \brief Parse a pair of select expressions separated by ':' or a single select expression (second element of return value empty)
static std::pair<std::string,std::string> parseExpressionPair( const std::string& val)
{
	std::pair<std::string,std::string> rt;
	char const* valueExpressionStart = 0;
	char const* vi = val.c_str();
	const char* ve = vi + val.size();
	for (; vi != ve; ++vi)
	{
		if (*vi == '\"' || *vi == '\'')
		{
			char eb = *vi++;
			for (; vi != ve && *vi != eb; ++vi)
			{
				if (*vi == '\\')
				{
					++vi;
					if (vi == ve) throw std::runtime_error(_TXT("unexpected end of string in select expression"));
				}
			}
			if (vi == ve) throw std::runtime_error(_TXT("unexpected end of string in select expression"));
		}
		else if (*vi == ':')
		{
			if (valueExpressionStart) throw std::runtime_error(_TXT("more than 3 elements (separated by ':' in entity expression declaration"));
			rt.first.append( val.c_str(), vi - val.c_str());
			if (rt.first.empty()) throw std::runtime_error(_TXT("empty string for select expression (first element) is not allowed"));
			valueExpressionStart = vi +1;
		}
	}
	if (valueExpressionStart)
	{
		rt.second.append( valueExpressionStart, vi-valueExpressionStart);
	}
	else
	{
		rt.first.append( val);
	}
	return rt;
}


static void writePosTaggerInput(
		const std::string& inputpath,
		const std::string& outputFile,
		strus::FileCrawlerInterface* crawler,
		const strus::DocumentClassDetectorInterface* dclassdetector,
		const strus::analyzer::DocumentClass& dclass,
		const strus::PosTaggerInstanceInterface* postaggerinst,
		const strus::SegmenterInstanceInterface* entitySegmenter,
		const std::string& entityPrefix,
		const std::string& fileTagPrefix)
{
	std::ostream* out = NULL;
	std::ofstream fout;
	std::vector<std::string> ar = crawler->fetch();
	for (; !ar.empty(); ar = crawler->fetch())
	{
		std::vector<std::string>::const_iterator ai = ar.begin(), ae = ar.end();
		for (; ai != ae; ++ai)
		{
			if (!strus::stringStartsWith( *ai, inputpath)) throw strus::runtime_error(_TXT("internal: input path '%s' does not have prefix '%s"), ai->c_str(), inputpath.c_str());
			std::string content;
			std::string posInputContent;
			std::vector<EntityDef> entities;
			int ec = strus::readFile( *ai, content);
			if (ec) throw strus::runtime_error(_TXT("failed to read input file '%s': %s"), ai->c_str(), ::strerror(ec));
			strus::analyzer::DocumentClass documentclass;
			if (!dclass.defined())
			{
				if (!dclassdetector->detect( documentclass, content.c_str(), content.size(), true/*is complete*/))
				{
					const char* errormsg = g_errorBuffer->fetchError();
					if (!errormsg) errormsg = "unsupported content type";
					throw strus::runtime_error(_TXT("failed to detect document class of file '%s': %s"), ai->c_str(), errormsg);
				}
				posInputContent = postaggerinst->getPosTaggerInput( documentclass, content);
				if (entitySegmenter)
				{
					entities = extractReferencedEntities( entitySegmenter, documentclass, content);
				}
			}
			else
			{
				posInputContent = postaggerinst->getPosTaggerInput( dclass, content);
				if (entitySegmenter)
				{
					entities = extractReferencedEntities( entitySegmenter, dclass, content);
				}
			}
			if (!out)
			{
				if (outputFile == "-")
				{
					out = &std::cout;
				}
				else
				{
					fout.open( outputFile.c_str(), std::ofstream::out);
					if(fout.fail()) throw strus::runtime_error( _TXT("failed to open output file '%s': %s"), outputFile.c_str(), ::strerror(errno));
					if(!fout.is_open()) throw strus::runtime_error( _TXT("could not open output file '%s': %s"), outputFile.c_str(), ::strerror(errno));
					out = &fout;
				}
			}
			*out << fileTagPrefix << std::string( ai->c_str() + inputpath.size(), ai->size() - inputpath.size()) << std::endl;
			std::vector<EntityDef>::const_iterator ei = entities.begin(), ee = entities.end();
			for (; ei != ee; ++ei)
			{
				if (ei->second.empty())
				{
					*out << entityPrefix << ei->first << std::endl;
				}
				else
				{
					*out << entityPrefix << ei->first << entityPrefix << ei->second << std::endl;
				}
			}
			*out << posInputContent << std::endl;
		}
	}
	if (out != &std::cout)
	{
		fout.close();
	}
}

static bool isAlphaNum( char ch)
{
	return ((ch|32) >= 'a' && (ch|32) <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
}

static std::string fetchLineToken( char const*& si, const char* se)
{
	const char* start = si;
	while (si != se && *si != '\t') ++si;
	return strus::string_conv::trim( start, si - start);
}

static strus::PosTaggerDataInterface::Element parseElement( const char* ln, std::size_t lnsize)
{
	typedef strus::PosTaggerDataInterface::Element Element;
	Element::Type type = Element::Content;
	std::string tag;
	std::string value;
	std::string ref;
	char const* si = ln;
	char const* se = si + lnsize;
	for (; si != se && isAlphaNum(*si); ++si)
	{
		tag.push_back(*si);
	}
	if (si != se && *si == '!')
	{
		++si;
		type = Element::Marker;
		if (tag == "_")
		{
			throw strus::runtime_error_ec( strus::ErrorCodeSyntax, _TXT("syntax error; tag identifier '_' (bound to previous) not allowed in combination with '!': %s"), ln);
		}
	}
	else if (tag == "_")
	{
		type = Element::BoundToPrevious;
		tag.clear();
	}
	if (si != se && *si == '\t')
	{
		++si;
		value = fetchLineToken( si, se);

		if (si != se && *si == '\t')
		{
			++si;
			ref = fetchLineToken( si, se);
		}
	}
	if (si != se)
	{
		throw strus::runtime_error_ec( strus::ErrorCodeSyntax, _TXT("invalid line in pos tagger file: %s"), ln);
	}
	return Element( type, tag, value, ref);
}

static void loadPosTaggingFile( strus::PosTaggerDataInterface* data, std::map<std::string,int>& filemap, const std::string& inputpath, const std::string& posTagFile, const std::string& fileTagPrefix)
{
	typedef strus::PosTaggerDataInterface::Element Element;
	typedef std::map<const std::string,int>::value_type FileMapValue;
	std::string filename;
	int docnocnt = filemap.size();
	strus::InputStream inp( posTagFile);
	char linebuf[ 1<<14];
	std::vector<Element> elements;
	int linecnt = 0;

	while (!inp.eof()) try
	{
		const char* ln = inp.readLine( linebuf, sizeof( linebuf), true/*failOnNoLine*/);
		++linecnt;
		if (!ln) throw strus::runtime_error( _TXT("error reading POS tagging file '%s' line %d: %s"), posTagFile.c_str(), linecnt, ::strerror( inp.error()));
		std::size_t lnsize = std::strlen( ln);
		if (lnsize > fileTagPrefix.size() && 0==std::memcmp( ln, fileTagPrefix.c_str(), fileTagPrefix.size()))
		{
			if (!elements.empty())
			{
				if (filename.empty()) throw strus::runtime_error( _TXT("got POS tagging info without associated file in '%s' line %d"), posTagFile.c_str(), linecnt);
				data->insert( docnocnt, elements);
				if (g_errorBuffer->hasError()) throw std::runtime_error( g_errorBuffer->fetchError());
				if (g_verbose) std::cerr << strus::string_format( _TXT("load POS tagging %d elements for docno %d file '%s' line %d"), (int)elements.size(), docnocnt, filename.c_str(), linecnt) << std::endl;
				elements.clear();
			}
			filename = strus::joinFilePath( inputpath, ln + fileTagPrefix.size());
			FileMapValue filemapvalue( filename, ++docnocnt);
			if (!filemap.insert( filemapvalue).second)
			{
				throw strus::runtime_error( _TXT("duplicate definition of file '%s' in POS tagging file '%s' line %d"), filename.c_str(), posTagFile.c_str(), linecnt);
			}
		}
		else if (*ln)
		{
			elements.push_back( parseElement( ln, lnsize));
		}
	}
	catch (const std::runtime_error& err)
	{
		throw strus::runtime_error( _TXT("error loading POS tagging file '%s': %s"), posTagFile.c_str(), err.what());
	}
	int err = inp.error();
	if (err) throw strus::runtime_error(_TXT("error reading POS tag file '%s': %s"), posTagFile.c_str(), ::strerror( err));
	if (!elements.empty())
	{
		if (filename.empty()) throw strus::runtime_error( _TXT("got POS tagging info without associated filein POS tagging file '%s' line %d"), posTagFile.c_str(), linecnt);
		data->insert( docnocnt, elements);
		if (g_verbose) std::cerr << strus::string_format( _TXT("load POS tagging %d elements for docno %d file '%s'"), (int)elements.size(), docnocnt, filename.c_str()) << std::endl;
	}
}

static void writePosTagging( 
		const strus::PosTaggerDataInterface* data,
		const std::map<std::string,int>& filemap,
		const std::string& outputpath,
		const std::string& errorpath,
		const strus::DocumentClassDetectorInterface* dclassdetector,
		const strus::analyzer::DocumentClass& dclass,
		const strus::PosTaggerInstanceInterface* postaggerinst,
		strus::FileCrawlerInterface* crawler)
{
	std::vector<std::string> ar = crawler->fetch();
	for (; !ar.empty(); ar = crawler->fetch())
	{
		std::vector<std::string>::const_iterator ai = ar.begin(), ae = ar.end();
		for (; ai != ae; ++ai)
		{
			std::map<std::string,int>::const_iterator fi = filemap.find( *ai);
			if (fi == filemap.end()) throw strus::runtime_error(_TXT("file '%s' not defined in POS tagging data"), ai->c_str());

			int docno = fi->second;
			std::string content;
			std::string output;
			int ec = strus::readFile( *ai, content);
			if (ec) throw strus::runtime_error(_TXT("failed to read input file '%s': %s"), ai->c_str(), ::strerror(ec));
			strus::analyzer::DocumentClass documentclass;
			if (!dclass.defined())
			{
				if (!dclassdetector->detect( documentclass, content.c_str(), content.size(), true/*is complete*/))
				{
					const char* errormsg = g_errorBuffer->fetchError();
					if (!errormsg) errormsg = "unsupported content type";
					throw strus::runtime_error(_TXT("failed to detect document class of file '%s': %s"), ai->c_str(), errormsg);
				}
				output = postaggerinst->markupDocument( data, docno, documentclass, content);
			}
			else
			{
				output = postaggerinst->markupDocument( data, docno, dclass, content);
			}
			if (output.empty())
			{
				const char* errormsg = g_errorBuffer->fetchError();
				if (!errormsg) errormsg = "output empty";
				if (!errorpath.empty())
				{
					ec = strus::appendFile( errorpath, strus::string_format(_TXT("File '%s': %s\n"), ai->c_str(), errormsg));
					if (ec)
					{
						std::cerr << strus::string_format(_TXT("failed to append to error file '%s': %s"), errorpath.c_str(), ::strerror(ec)) << std::endl;
						throw strus::runtime_error(_TXT("failed to POS tag file '%s': %s"), ai->c_str(), errormsg);
					}
				}
				else
				{
					throw strus::runtime_error(_TXT("failed to POS tag file '%s': %s"), ai->c_str(), errormsg);
				}
			}
			std::string outputfilename;
			if (outputpath.empty())
			{
				outputfilename = *ai + ".pos";
			}
			else if (outputpath == "-")
			{
				std::cout << output << std::endl;
				continue;
			}
			else
			{
				ec = strus::getFileName( *ai, outputfilename, true);
				if (ec) throw strus::runtime_error(_TXT("failed to get output file name for '%s': %s"), ai->c_str(), ::strerror(ec));
				outputfilename = strus::joinFilePath( outputpath, outputfilename);
				if (outputfilename.empty()) throw std::bad_alloc();
				ec = strus::mkdirp( outputpath);
				if (ec) throw strus::runtime_error(_TXT("failed to create output file path for '%s': %s"), outputpath.c_str(), ::strerror(ec));
			}
			ec = strus::writeFile( outputfilename, output);
			if (ec) throw strus::runtime_error(_TXT("failed to write POS tagged output file '%s': %s"), outputfilename.c_str(), ::strerror(ec));
			if (g_verbose) std::cerr << strus::string_format( _TXT("writed tagged file '%s'"), outputfilename.c_str()) << std::endl;
		}
	}
}

class WorkerBase
{
public:
	virtual ~WorkerBase(){}
	virtual void run()=0;
};

class PosInputWorker
	:public WorkerBase
{
public:
	virtual ~PosInputWorker(){}
	PosInputWorker(
			int threadid_,
			strus::FileCrawlerInterface* crawler_,
			const strus::DocumentClassDetectorInterface* dclassdetector_,
			const strus::analyzer::DocumentClass& dclass_,
			const strus::PosTaggerInstanceInterface* postaggerinst_,
			const strus::SegmenterInstanceInterface* entitySegmenter_,
			const std::string& entityPrefix_,
			const std::string& fileTagPrefix_,
			const std::string& inputPath_,
			const std::string& outputFile_)
		:m_threadid(threadid_),m_inputPath(inputPath_),m_outputFile(outputFile_),m_crawler(crawler_)
		,m_dclassdetector(dclassdetector_),m_dclass(dclass_),m_postaggerinst(postaggerinst_)
		,m_entitySegmenter(entitySegmenter_),m_entityPrefix(entityPrefix_)
		,m_fileTagPrefix(fileTagPrefix_)
	{
		if (threadid_ >= 0)
		{
			m_outputFile.append( strus::string_format( "%d", threadid_));
		}
	}

	virtual void run()
	{
		try
		{
			writePosTaggerInput( m_inputPath, m_outputFile, m_crawler, m_dclassdetector, m_dclass, m_postaggerinst, m_entitySegmenter, m_entityPrefix, m_fileTagPrefix);
		}
		catch (const std::bad_alloc& err)
		{
			std::cerr << _TXT("ERROR out of memory");
			if (m_threadid >= 0) std::cerr << strus::string_format( _TXT(" in thread %d"), m_threadid);
			std::cerr << std::endl;
		}
		catch (const std::runtime_error& err)
		{
			std::cerr << _TXT("ERROR runtime error");
			if (m_threadid >= 0) std::cerr << strus::string_format( _TXT(" in thread %d"), m_threadid);
			std::cerr << ": " << err.what() << std::endl;
		}
	}

private:
	int m_threadid;
	std::string m_inputPath;
	std::string m_outputFile;
	strus::FileCrawlerInterface* m_crawler;
	const strus::DocumentClassDetectorInterface* m_dclassdetector;
	strus::analyzer::DocumentClass m_dclass;
	const strus::PosTaggerInstanceInterface* m_postaggerinst;
	const strus::SegmenterInstanceInterface* m_entitySegmenter;
	std::string m_entityPrefix;
	std::string m_fileTagPrefix;
};


class PosOutputWorker
	:public WorkerBase
{
public:
	virtual ~PosOutputWorker(){}
	PosOutputWorker(
			int threadid_,
			strus::FileCrawlerInterface* crawler_,
			const strus::DocumentClassDetectorInterface* dclassdetector_,
			const strus::analyzer::DocumentClass& dclass_,
			const strus::PosTaggerInstanceInterface* postaggerinst_,
			const strus::PosTaggerDataInterface* data_,
			const std::map<std::string,int>* filemap_,
			const std::string& outputpath_,
			const std::string& errorpath_)
		:m_threadid(threadid_),m_crawler(crawler_),m_dclassdetector(dclassdetector_),m_dclass(dclass_)
		,m_postaggerinst(postaggerinst_)
		,m_data(data_),m_filemap(filemap_),m_outputpath(outputpath_),m_errorpath(errorpath_)
	{}

	virtual void run()
	{
		try
		{
			writePosTagging( m_data, *m_filemap, m_outputpath, m_errorpath, m_dclassdetector, m_dclass, m_postaggerinst, m_crawler);
		}
		catch (const std::bad_alloc& err)
		{
			std::cerr << _TXT("ERROR out of memory");
			if (m_threadid >= 0) std::cerr << strus::string_format( _TXT(" in thread %d"), m_threadid);
			std::cerr << std::endl;
		}
		catch (const std::runtime_error& err)
		{
			std::cerr << _TXT("ERROR runtime error");
			if (m_threadid >= 0) std::cerr << strus::string_format( _TXT(" in thread %d"), m_threadid);
			std::cerr << ": " << err.what() << std::endl;
		}
	}

private:
	int m_threadid;
	strus::FileCrawlerInterface* m_crawler;
	const strus::DocumentClassDetectorInterface* m_dclassdetector;
	strus::analyzer::DocumentClass m_dclass;
	const strus::PosTaggerInstanceInterface* m_postaggerinst;
	const strus::PosTaggerDataInterface* m_data;
	const std::map<std::string,int>* m_filemap;
	std::string m_outputpath;
	std::string m_errorpath;
};

static void declareIgnoreToken( strus::PosTaggerDataInterface* taggerdata, const strus::TokenizerFunctionInstanceInterface* tokenizer, const std::string& value)
{
	std::vector<strus::analyzer::Token> tokens = tokenizer->tokenize( value.c_str(), value.size());
	std::vector<strus::analyzer::Token>::const_iterator ti = tokens.begin(), te = tokens.end();
	for (; ti != te; ++ti)
	{
		std::string ignoretok( value.c_str() + ti->origpos().ofs(), ti->origsize());
		taggerdata->declareIgnoredToken( ignoretok);
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
		g_errorBuffer = errorBuffer.get();
		bool printUsageAndExit = false;

		strus::ProgramOptions opt(
				errorBuffer.get(), argc, argv, 25,
				"h,help", "v,version", "V,verbose",
				"license", "G,debug:", "m,module:",
				"M,moduledir:", "r,rpc:", "T,trace:", "R,resourcedir:",
				"g,segmenter:", "C,contenttype:", "x,extension:",
				"e,contentexpr:", "X,entityexpr:", "E,spaceexpr:", "p,punctexpr:", "D,punctdelim:",
				"I,posinp", "t,threads:", "f,fetch:",
				"P,prefix:", "Y,entityprefix:", "o,output:", "F,erroutput:");
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
			std::cout << _TXT("usage:") << " strusPosTagger [options] <docpath> <posfile>" << std::endl;
			std::cout << "<docpath> = " << _TXT("path of input file/directory") << std::endl;
			std::cout << "<posfile> = " << _TXT("path of input (POS output) or input (POS input)") << std::endl;
			std::cout << "            " << _TXT("file depending of action ('-' for stdout/stdin)") << std::endl;
			std::cout << _TXT("description: a) dumps POS tagger input if started with option -I.") << std::endl;
			std::cout << _TXT("             b) output POS tagged files if started without option -I.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-V,--verbose" << std::endl;
			std::cout << "    " << _TXT("Verbose output of actions to stderr") << std::endl;
			std::cout << "-I|--posinp" << std::endl;
			std::cout << "    " << _TXT("Action is collect POS input to the argument file <file>") << std::endl;
			std::cout << "    " << _TXT("If not specified then the action is POS tagging") << std::endl;
			std::cout << "    " << _TXT("with the tags read from the argument <file> (output of POS tagger)") << std::endl;
			std::cout << "-e|--contentexpr <XPATH>" << std::endl;
			std::cout << "    " << _TXT("Use <XPATH> as expression (abbreviated syntax of XPath)") << std::endl;
			std::cout << "    " << _TXT("to select content to process (many definitions allowed).") << std::endl;
			std::cout << "-p|--punctexpr <XPATH>" << std::endl;
			std::cout << "    " << _TXT("Use <XPATH> as expression (abbreviated syntax of XPath)") << std::endl;
			std::cout << "    " << _TXT("to select tags that issue a sentence delimiter as POS tagger input.") << std::endl;
			std::cout << "    " << _TXT("Remark: Strus extends the syntax of syntax of XPath with a trailing '~'") << std::endl;
			std::cout << "    " << _TXT("to denote the end of a tag selected.") << std::endl;
			std::cout << "    " << _TXT("This option is available if -I|--posinp) is specified.") << std::endl;
			std::cout << "-E|--spaceexpr <XPATH>" << std::endl;
			std::cout << "    " << _TXT("Use <XPATH> as expression (abbreviated syntax of XPath)") << std::endl;
			std::cout << "    " << _TXT("to select a tag issueing a space delimiter as POS tagger input.") << std::endl;
			std::cout << "    " << _TXT("Similar to --punctuation but issuing a space ' ' instead of") << std::endl;
			std::cout << "    " << _TXT("a delimiter declared with --delimiter.") << std::endl;
			std::cout << "    " << _TXT("This option is available if -I|--posinp) is specified.") << std::endl;
			std::cout << "-X|--entityexpr <EXPR>" << std::endl;
			std::cout << "    " << _TXT("<EXPR> is an expression (abbreviated syntax of XPath) or a pair") << std::endl;
			std::cout << "    " << _TXT("of expressions separated by ':' that select entities to be printed") << std::endl;
			std::cout << "    " << _TXT("at the start of the POS tagger input.") << std::endl;
			std::cout << "    " << _TXT("See description of option -Y|--entityprefix for a description of the") << std::endl;
			std::cout << "    " << _TXT("output (POS tagger input) if this option and option -I|--posinp") << std::endl;
			std::cout << "    " << _TXT("is specified.") << std::endl;
			std::cout << "-Y|--entityprefix <PREFIX>" << std::endl;
			std::cout << "    " << _TXT("Use the string <PREFIX> as prefix entities if they are selected to") << std::endl;
			std::cout << "    " << _TXT("be printed as generated POS tagger input at the start of the") << std::endl;
			std::cout << "    " << _TXT("document. (Option -I) Default is '##'.") << std::endl;
			std::cout << "    " << _TXT("Such an entity declaration line has the form <PREFIX> entity-id <PREFIX> value,") << std::endl;
			std::cout << "    " << _TXT("if the declaration or the entity with the option -X|--entityexpr defines") << std::endl;
			std::cout << "    " << _TXT("a pair or expressions separated by ':' or the form <PREFIX> entity-id, if the") << std::endl;
			std::cout << "    " << _TXT("argument of option -X|--entityexpr is a single expression.") << std::endl;
			std::cout << "    " << _TXT("This option is available if -I|--posinp) is specified.") << std::endl;
			std::cout << "-D|--punctdelim <DELIM>" << std::endl;
			std::cout << "    " << _TXT("Use <DELIM> as end of sentence (punctuation) issued when a") << std::endl;
			std::cout << "    " << _TXT("tag selecting punctuation matches (Default is ';\n').") << std::endl;
			std::cout << "    " << _TXT("This option is available if -I|--posinp) is specified.") << std::endl;
			std::cout << "-P|--prefix <STR>" << std::endl;
			std::cout << "    " << _TXT("Use the string <STR> as prefix for a file declaration line in") << std::endl;
			std::cout << "    " << _TXT("the POS tagging input or output file.") << std::endl;
			std::cout << "    " << _TXT("Default is '#FILE#'.") << std::endl;
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
			std::cout << "    " << _TXT("forced definition of the document class of the document processed.") << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    " << _TXT("extension of the input files processed.") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of threads to use") << std::endl;
			std::cout << "-f|--fetch <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of files fetched in each iteration") << std::endl;
			std::cout << "    " << _TXT("Default is 100") << std::endl;
			std::cout << "-o|--output <PATH>" << std::endl;
			std::cout << "    " << _TXT("Write output POS tagging output files to subdirectories of") << std::endl;
			std::cout << "    " << _TXT("<PATH> or to stdout if '-' is specified") << std::endl;
			std::cout << "-F|--erroutput <PATH>" << std::endl;
			std::cout << "    " << _TXT("Write tagging errors to output file <PATH> instead of throwing an exception") << std::endl;
			std::cout << "    " << _TXT("Use '-' for stderr.") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string segmenterName;
		std::string contenttype;
		std::string fileext;
		std::string filenamePrefix = "#FILE#";
		std::string entityPrefix = "##";
		std::vector<std::string> contentExpression;
		std::vector<std::string> punctExpression;
		std::vector<std::string> spaceExpression;
		std::vector<std::pair<std::string,std::string> > entityExpression;
		std::string punctDelimiter = "; ";
		std::string spaceDelimiter = " ";
		enum {MaxNofThreads=1024};
		int threads = opt( "threads") ? opt.asUint( "threads") : 0;
		if (threads > MaxNofThreads) threads = MaxNofThreads;
		int fetchSize = opt( "fetch") ? opt.asUint( "fetch") : 100;
		if (!fetchSize) fetchSize = 1;
		std::string outputpath;
		std::string erroutputpath;

		enum Action {DoGenInput,DoGenOutput};
		Action action = opt( "posinp") ? DoGenInput : DoGenOutput;
		if (opt( "output"))
		{
			if (action == DoGenInput) throw strus::runtime_error(_TXT("option -o|--output makes no sense with option -I"));
			outputpath = opt[ "output"];
		}
		if (opt( "erroutput"))
		{
			if (action == DoGenInput) throw strus::runtime_error(_TXT("option -F|--erroutput makes no sense with option -I"));
			erroutputpath = opt[ "erroutput"];
		}
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
			if (!fileext.empty() && fileext[0] != '.') fileext = std::string(".") + fileext;
		}
		if (opt( "verbose"))
		{
			g_verbose = true;
		}
		if (opt( "prefix"))
		{
			filenamePrefix = opt[ "prefix"];
		}
		if (opt( "entityprefix"))
		{
			if (action != DoGenInput) throw strus::runtime_error(_TXT("option -Y|--entityprefix makes no sense with option -I"));
			entityPrefix = opt[ "entityprefix"];
		}
		if (opt( "contentexpr"))
		{
			contentExpression = opt.list( "contentexpr");
		}
		if (opt( "punctexpr"))
		{
			punctExpression = opt.list( "punctexpr");
		}
		if (opt( "spaceexpr"))
		{
			spaceExpression = opt.list( "spaceexpr");
		}
		if (opt( "entityexpr"))
		{
			if (action != DoGenInput) throw strus::runtime_error(_TXT("option -X|--entityexpr makes no sense with option -I"));
			std::vector<std::string> elist = opt.list( "entityexpr");
			std::vector<std::string>::const_iterator ei = elist.begin(), ee = elist.end();
			for (; ei != ee; ++ei)
			{
				entityExpression.push_back( parseExpressionPair( *ei));
			}
		}
		if (opt( "punctdelim"))
		{
			punctDelimiter = opt[ "punctdelim"];
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
		int ec = 0;
		std::string docpath = opt[0];
		std::string docdir;
		std::string posfile = opt[1];
		if (g_errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("invalid arguments"));
		}
		ec = strus::resolveUpdirReferences( docpath);
		if (ec) throw strus::runtime_error( _TXT("failed to resolve updir references of path '%s': %s"), docpath.c_str(), ::strerror(ec));

		if (strus::isFile( docpath))
		{
			ec = strus::getParentPath( docpath, docdir);
			if (ec) throw strus::runtime_error( _TXT("failed to get parent path of '%s': %s"), docpath.c_str(), ::strerror(ec));
		}
		else
		{
			docdir = docpath;
		}

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
		// Initialize the text processor:
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw std::runtime_error( _TXT("failed to get text processor"));

		// Get the document class if specified, defines the segmenter to use:
		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty())
		{
			documentClass = strus::parse_DocumentClass( contenttype, errorBuffer.get());
			if (!documentClass.defined() && errorBuffer->hasError())
			{
				throw std::runtime_error( _TXT("failed to parse document class"));
			}
		}

		// Initialize the file crawler and segmenter:
		strus::local_ptr<strus::FileCrawlerInterface> fileCrawler( strus::createFileCrawlerInterface( docpath, fetchSize, fileext, errorBuffer.get()));
		if (!fileCrawler.get()) throw std::runtime_error( errorBuffer->fetchError());
		strus::local_ptr<strus::DocumentClassDetectorInterface> documentClassDetector( analyzerBuilder->createDocumentClassDetector());
		if (!documentClassDetector.get()) throw std::runtime_error( errorBuffer->fetchError());
		const strus::SegmenterInterface* segmenter = NULL;
		strus::analyzer::SegmenterOptions segmenterOpts;
		strus::local_ptr<strus::SegmenterInstanceInterface> entitySegmenterInst;

		if (segmenterName.empty())
		{
			if (documentClass.defined())
			{
				segmenter = textproc->getSegmenterByMimeType( documentClass.mimeType());
				if (!documentClass.scheme().empty())
				{
					segmenterOpts = textproc->getSegmenterOptions( documentClass.scheme());
				}
			}
			else if (strus::caseInsensitiveEquals( fileext, ".xml"))
			{
				segmenter = textproc->getSegmenterByMimeType( "application/xml");
			}
			else if (strus::caseInsensitiveEquals( fileext, ".json") || strus::caseInsensitiveEquals( fileext, ".js"))
			{
				segmenter = textproc->getSegmenterByMimeType( "application/json");
			}
			else if (strus::caseInsensitiveEquals( fileext, ".tsv"))
			{
				segmenter = textproc->getSegmenterByName( "tsv");
			}
			else if (strus::caseInsensitiveEquals( fileext, ".txt"))
			{
				segmenter = textproc->getSegmenterByName( "plain");
			}
			else
			{
				std::cerr << _TXT("no segmenter or document class specified, assuming documents to be XML") << std::endl;
				segmenter = textproc->getSegmenterByMimeType( "application/xml");
			}
		}
		else
		{
			segmenter = textproc->getSegmenterByName( segmenterName);
		}
		if (!segmenter) throw std::runtime_error( _TXT( "failed to get segmenter"));
		const strus::PosTaggerInterface* postaggertype = textproc->getPosTagger();
		if (!postaggertype) throw std::runtime_error( _TXT("failed to get POS tagger"));
		strus::local_ptr<strus::PosTaggerInstanceInterface> postagger( postaggertype->createInstance( segmenter, segmenterOpts));
		if (!postagger.get()) throw std::runtime_error( _TXT("failed to create POS tagger instance"));

		// Define entity expression segmenter if selector expressions for entities are defined:
		if (!entityExpression.empty())
		{
			entitySegmenterInst.reset( segmenter->createInstance( segmenterOpts));
			if (!entitySegmenterInst.get())
			{
				throw strus::runtime_error( _TXT("failed to create segmenter instance for extracting entities: %s"), g_errorBuffer->fetchError());
			}
			std::vector<std::pair<std::string,std::string> >::const_iterator ei = entityExpression.begin(), ee = entityExpression.end();
			for (int eidx=0; ei != ee; ++ei,eidx+=NofEntityIdTypes)
			{
				if (ei->second.empty())
				{
					entitySegmenterInst->defineSelectorExpression( eidx+EntityIdGroup, ei->first);
				}
				else
				{
					std::size_t commonAncestorSize = 0;
					while (ei->first.size() > commonAncestorSize && ei->second.size() > commonAncestorSize && ei->first[commonAncestorSize] == ei->second[commonAncestorSize])
					{
						++commonAncestorSize;
					}
					if (commonAncestorSize)
					{
						entitySegmenterInst->defineSelectorExpression( eidx+EntityIdGroup, std::string( ei->first.c_str(), commonAncestorSize));
					}
					entitySegmenterInst->defineSelectorExpression( eidx+EntityIdValue, ei->second);
				}
				entitySegmenterInst->defineSelectorExpression( eidx+EntityIdId, ei->first);
			}
		}

		// Define content and punctuation for POS tagger input:
		{
			std::vector<std::string>::const_iterator ei = contentExpression.begin(), ee = contentExpression.end();
			for (; ei != ee; ++ei) postagger->addContentExpression( *ei);
		}{
			std::vector<std::string>::const_iterator ei = punctExpression.begin(), ee = punctExpression.end();
			for (; ei != ee; ++ei) postagger->addPosTaggerInputPunctuation( *ei, punctDelimiter, 3);
		}{
			std::vector<std::string>::const_iterator ei = spaceExpression.begin(), ee = spaceExpression.end();
			for (; ei != ee; ++ei) postagger->addPosTaggerInputPunctuation( *ei, spaceDelimiter, 1);
		}

		// Define the tokenizer:
		const strus::TokenizerFunctionInterface* entityTokenizerFunc = textproc->getTokenizer( "langtoken");
		if (!entityTokenizerFunc) throw std::runtime_error( _TXT( "failed to get tokenizer 'langtoken'"));
		strus::local_ptr<strus::TokenizerFunctionInstanceInterface> entityTokenizer( entityTokenizerFunc->createInstance( std::vector<std::string>(), textproc));
		if (!entityTokenizer.get()) throw std::runtime_error( _TXT( "failed to get tokenizer instance for 'langtoken'"));

		// Define the POS tagger data (not needed for input):
		const strus::TokenizerFunctionInstanceInterface* entityTokenizerPtr = entityTokenizer.get();
		strus::local_ptr<strus::PosTaggerDataInterface> posTagData( textproc->createPosTaggerData( entityTokenizer.get()));
		entityTokenizer.release();//... ownership passed to posTagData

		// Define tokens to ignore if not present in document to tag (potentially added by input generator)
		declareIgnoreToken( posTagData.get(), entityTokenizerPtr, punctDelimiter);
		declareIgnoreToken( posTagData.get(), entityTokenizerPtr, ".");
		declareIgnoreToken( posTagData.get(), entityTokenizerPtr, spaceDelimiter);

		std::map<std::string,int> posTagDocnoMap;

		// Build the worker data:
		typedef strus::Reference<WorkerBase> WorkerBaseReference;
		WorkerBaseReference workers[ MaxNofThreads];

		switch (action)
		{
			case DoGenInput:
			{
				int ti = 0, te = threads ? threads : 1;
				for (; ti != te; ++ti)
				{
					int threadid = threads ? (ti+1) : -1;
					workers[ti].reset(
						new PosInputWorker(
							threadid, fileCrawler.get(), documentClassDetector.get(),
							documentClass, postagger.get(), entitySegmenterInst.get(),
							entityPrefix, filenamePrefix, docdir, posfile));
				}
				std::cerr << _TXT("Generate input for POS tagging ...") << std::endl;
				break;
			}
			case DoGenOutput:
			{
				std::cerr << _TXT("Loading POS tag file ...");
				loadPosTaggingFile( posTagData.get(), posTagDocnoMap, docdir, posfile, filenamePrefix);
				std::cerr << _TXT(" done") << std::endl;

				int ti = 0, te = threads ? threads : 1;
				for (; ti != te; ++ti)
				{
					std::string errorpath;
					if (!erroutputpath.empty())
					{
						if (threads)
						{
							const char* ee = std::strchr( erroutputpath.c_str(), '.');
							if (ee)
							{
								errorpath.append( erroutputpath.c_str(), (ee-erroutputpath.c_str())+1);
								errorpath.append( strus::string_format( "%d", ti));
								errorpath.append( ee);
							}
							else
							{
								errorpath.append( erroutputpath);
								errorpath.append( strus::string_format( ".%d.txt", ti));
							}
						}
						else
						{
							errorpath = erroutputpath;
						}
					}
					int threadid = threads ? (ti+1) : -1;
					workers[ti].reset(
						new PosOutputWorker(
							threadid, fileCrawler.get(), documentClassDetector.get(),
							documentClass, postagger.get(), posTagData.get(), &posTagDocnoMap,
							outputpath, errorpath));
				}
				std::cerr << _TXT("Tagging documents with POS tagging results ...") << std::endl;
				break;
			}
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error loading the POS tagger data"));
		}

		// Run the jobs to do:
		if (threads)
		{
			std::cerr << strus::string_format( _TXT("Starting %d threads ..."), threads) << std::endl;
			std::vector<strus::Reference<strus::thread> > threadGroup;
			for (int ti=0; ti<threads; ++ti)
			{
				WorkerBase* tc = workers[ ti].get();
				strus::Reference<strus::thread> th( new strus::thread( &WorkerBase::run, tc));
				threadGroup.push_back( th);
			}
			std::vector<strus::Reference<strus::thread> >::iterator gi = threadGroup.begin(), ge = threadGroup.end();
			for (; gi != ge; ++gi) (*gi)->join();
		}
		else
		{
			workers[0]->run();
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in POS tagger"));
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



