/*
 * Copyright (c) 2018 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/error.hpp"
#include "strus/lib/doctree.hpp"
#include "strus/lib/filecrawler.hpp"
#include "strus/lib/filelocator.hpp"
#include "strus/lib/textproc.hpp"
#include "strus/lib/detector_std.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/thread.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/string_conv.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/reference.hpp"
#include "strus/documentClassDetectorInterface.hpp"
#include "strus/fileCrawlerInterface.hpp"
#include "strus/fileLocatorInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/versionBase.hpp"
#include "strus/versionAnalyzer.hpp"
#include "private/versionUtilities.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <memory>

static strus::ErrorBufferInterface* g_errorBuffer = 0;	// error buffer
static bool g_verbose = false;

enum FileType {FileTypeXML};

class WorkerBase
{
public:
	virtual ~WorkerBase(){}
	virtual void run()=0;
};

class Worker
	:public WorkerBase
{
public:
	virtual ~Worker(){}
	Worker(
			int threadid_,
			strus::FileCrawlerInterface* crawler_,
			const strus::DocumentClassDetectorInterface* dclassdetector_,
			FileType fileType_,
			const std::set<std::string>& markupset_,
			const std::string& inputPath_,
			const std::string& markupPath_,
			const std::string& outputPath_,
			const std::string& errorPath_)
		:m_threadid(threadid_),m_inputPath(inputPath_),m_markupPath(markupPath_)
		,m_outputPath(outputPath_),m_errorPath(errorPath_)
		,m_crawler(crawler_),m_dclassdetector(dclassdetector_)
		,m_fileType(fileType_),m_markupset(markupset_)
	{}

	virtual void run()
	{
		std::vector<std::string> ar = m_crawler->fetch();
		for (; !ar.empty(); ar = m_crawler->fetch())
		{
			std::vector<std::string>::const_iterator ai = ar.begin(), ae = ar.end();
			for (; ai != ae; ++ai)
			{
				if (!strus::stringStartsWith( *ai, m_inputPath)) throw strus::runtime_error(_TXT("internal: input path '%s' does not have prefix '%s"), ai->c_str(), m_inputPath.c_str());
				std::string restPath( ai->c_str()+m_inputPath.size());
				std::string inputFile = *ai;
				std::string markupFile = strus::joinFilePath( m_markupPath, restPath);
				std::string outputFile;
				std::string errorFile;
				if (m_outputPath != "-" && !m_outputPath.empty())
				{
					outputFile = strus::joinFilePath( m_outputPath, restPath);
				}
				
				if (!m_errorPath.empty())
				{
					errorFile = strus::replaceFileExtension( strus::joinFilePath( m_errorPath, restPath), ".err");
					if (errorFile.empty()) throw std::bad_alloc();
				}
				else if (!outputFile.empty())
				{
					errorFile = strus::replaceFileExtension( outputFile, ".err");
					if (errorFile.empty()) throw std::bad_alloc();
				}
				processFile( inputFile, markupFile, outputFile, errorFile);
			}
		}
	}

private:
	strus::analyzer::DocumentClass detectDocumentClass( const std::string& inputStr)
	{
		strus::analyzer::DocumentClass rt;
		std::size_t detbufsize = inputStr.size() > 1000 ? 1000 : inputStr.size();
		if (m_dclassdetector->detect( rt, inputStr.c_str(), detbufsize, detbufsize == inputStr.size()))
		{
			return rt;
		}
		else
		{
			throw strus::runtime_error( _TXT("failed to detect document class: %s"), g_errorBuffer->fetchError());
		}
	}

	strus::DocTreeRef readDocTree( const strus::analyzer::DocumentClass& dclass, const std::string& content)
	{
		strus::DocTreeRef rt;
		if (!dclass.defined()) return rt;
		if (dclass.mimeType() == "application/xml")
		{
			rt.reset( strus::createDocTree_xml( dclass.encoding().c_str(), content.c_str(), content.size(), g_errorBuffer));
			if (!rt.get())
			{
				throw strus::runtime_error( _TXT("failed to build tree from XML: %s"), g_errorBuffer->fetchError());
			}
		}
		else
		{
			throw std::runtime_error( _TXT("file not XML (only XML supported till now)"));
		}
		return rt;
	}

	void writeDocTree( const strus::analyzer::DocumentClass& dclass, const std::string& outputFile, const strus::DocTreeRef& doctree)
	{
		std::ostringstream outbuf;
		if (dclass.mimeType() == "application/xml")
		{
			if (!strus::printDocTree_xml( outbuf, dclass.encoding().c_str(), *doctree, g_errorBuffer))
			{
				throw strus::runtime_error(_TXT("XML serialization error: %s"), g_errorBuffer->fetchError());
			}
			printOutput( outputFile, outbuf.str());
		}
		else
		{
			throw std::runtime_error( _TXT("file not XML (only XML supported till now)"));
		}
	}

	void processFile( const std::string& inputFile, const std::string& markupFile, const std::string& outputFile, const std::string& errorFile)
	{
		try
		{
			int ec = 0;
			if (!errorFile.empty())
			{
				ec = strus::removeFile( errorFile, false/*fail if not exist*/);
				if (ec) throw strus::runtime_error(_TXT("failed to remove previous error file: %s"), std::strerror(ec));
			}
			std::string inputStr;
			std::string markupStr;

			ec = strus::readFile( inputFile, inputStr);
			if (ec) throw strus::runtime_error(_TXT("error in thread %d reading input file '%s': %s"), m_threadid, inputFile.c_str(), std::strerror(ec));
			ec = strus::readFile( markupFile, markupStr);
			if (ec) throw strus::runtime_error( _TXT("error reading markup file '%s': %s"), markupFile.c_str(), std::strerror(ec));

			strus::analyzer::DocumentClass inputClass;
			strus::analyzer::DocumentClass markupClass;
			try
			{
				inputClass = detectDocumentClass( inputStr);
			}
			catch (const std::runtime_error& err)
			{
				throw strus::runtime_error(_TXT("failed to detect document class of input file: %s"), err.what());
			}
			try
			{
				markupClass = detectDocumentClass( markupStr);
			}
			catch (const std::runtime_error& err)
			{
				throw strus::runtime_error(_TXT("failed to detect document class of markup file: %s"), err.what());
			}
			strus::DocTreeRef inputTree;
			strus::DocTreeRef markupTree;
			strus::DocTreeRef resultTree;
			try
			{
				inputTree = readDocTree( inputClass, inputStr);
			}
			catch (const std::runtime_error& err)
			{
				throw strus::runtime_error( _TXT("failed to create document structure tree from input content: %s"), err.what());
			}
			try
			{
				markupTree = readDocTree( markupClass, markupStr);
			}
			catch (const std::runtime_error& err)
			{
				throw strus::runtime_error( _TXT("failed to create document structure tree from markup content: %s"), err.what());
			}
			try
			{
				resultTree = mergeTree( inputTree, markupTree);
			}
			catch (const std::runtime_error& err)
			{
				throw strus::runtime_error( _TXT("failed to merge markups into input tree structure: %s"), err.what());
			}
			try
			{
				writeDocTree( inputClass, outputFile, resultTree);
			}
			catch (const std::runtime_error& err)
			{
				throw strus::runtime_error( _TXT("failed to write merged document tree to output file: %s"), err.what());
			}
		}
		catch (const std::runtime_error& err)
		{
			printError( inputFile, errorFile, err.what());
		}
	}

	void printError( const std::string& inputFile, const std::string& errorFile, const std::string& msg)
	{
		if (errorFile.empty())
		{
			std::cerr << strus::string_format( _TXT("error in thread %d processing file %s: %s"), m_threadid, inputFile.c_str(), msg.c_str()) << std::endl;
		}
		else
		{
			std::string errorDir;
			int ec = strus::getParentPath( errorFile, errorDir);
			if (ec) throw strus::runtime_error(_TXT("error in thread %d get parent path of error file %s: %s"), m_threadid, errorFile.c_str(), std::strerror(ec));
			std::string firstDirectoryCreated;
			ec = strus::mkdirp( errorDir, firstDirectoryCreated);
			if (ec) throw strus::runtime_error(_TXT("error in thread %d create parent path (mkdirp) of error directory %s: %s"), m_threadid, errorDir.c_str(), std::strerror(ec));
			ec = strus::writeFile( errorFile, msg + "\n");
			if (ec) throw strus::runtime_error(_TXT("error in thread %d writing error to file %s: %s"), m_threadid, errorFile.c_str(), std::strerror(ec));
		}
	}

	void printOutput( const std::string& outputFile, const std::string& content)
	{
		if (outputFile.empty())
		{
			std::cout << content << std::endl;
		}
		else
		{
			std::string outputDir;
			int ec = strus::getParentPath( outputFile, outputDir);
			if (ec) throw strus::runtime_error(_TXT("error in thread %d get parent path of output file %s: %s"), m_threadid, outputFile.c_str(), std::strerror(ec));
			std::string firstDirectoryCreated;
			ec = strus::mkdirp( outputDir, firstDirectoryCreated);
			if (ec) throw strus::runtime_error(_TXT("error in thread %d create parent path (mkdirp) of output directory %s: %s"), m_threadid, outputDir.c_str(), std::strerror(ec));
			ec = strus::writeFile( outputFile, content);
			if (ec) throw strus::runtime_error(_TXT("error in thread %d writing output to file %s: %s"), m_threadid, outputFile.c_str(), std::strerror(ec));
		}
	}

	struct Segment
	{
		std::string key;
		std::string content;
		strus::DocTreeRef node;
		strus::DocTreeRef replace;
		std::vector<std::string> tags;

		Segment( const std::string& content_, const strus::DocTreeRef& node_, const std::vector<std::string>& tagstk)
			:key(getKey(content_)),content(content_),node(node_),replace(),tags(tagstk){}
		Segment( const Segment& o)
			:key(o.key),content(o.content),node(o.node),replace(o.replace),tags(o.tags){}

		static std::string getKey( const std::string& content)
		{
			std::string rt;
			std::string::const_iterator ci = content.begin(), ce = content.end();
			for (; ci != ce; ++ci)
			{
				if ((unsigned char)*ci > 32) rt.push_back( *ci);
			}
			return rt;
		}
	};

	void segmentTree( std::vector<Segment>& dest, const strus::DocTreeRef& node, bool withEmptyKeys, const std::vector<std::string>& tagstk_)
	{
		std::vector<std::string> tagstk = tagstk_;
		if (!node->name().empty())
		{
			tagstk.push_back( node->name());
		}
		if (node->chld().empty())
		{
			dest.push_back( Segment( node->value(), node, tagstk));
			if (!withEmptyKeys && dest.back().key.empty())
			{
				dest.pop_back();
			}
		}
		else
		{
			strus::DocTree::chld_iterator ci = node->chld().begin(), ce = node->chld().end();
			for (; ci != ce; ++ci)
			{
				segmentTree( dest, *ci, withEmptyKeys, tagstk);
			}
		}
	}

	void printTreeToString( std::string& dest, const strus::DocTreeRef& node)
	{
		if (!node->name().empty())
		{
			dest.append( strus::string_format( "<%s>", node->name().c_str()));
		}
		if (!node->value().empty())
		{
			dest.append( node->value());
		}
		else
		{
			strus::DocTree::chld_iterator ci = node->chld().begin(), ce = node->chld().end();
			for (; ci != ce; ++ci)
			{
				printTreeToString( dest, *ci);
			}
		}
		if (!node->name().empty())
		{
			dest.append( strus::string_format( "</%s>", node->name().c_str()));
		}
	}

	std::string treeToString( const strus::DocTreeRef& node)
	{
		std::string rt;
		printTreeToString( rt, node);
		return rt;
	}

	bool stringHasPrefix( const char* si, const char* se, const std::string& prefix)
	{
		if (si + prefix.size() > se) return false;
		return 0==std::memcmp( si, prefix.c_str(), prefix.size());
	}

	static std::string tagPath( const std::vector<std::string>& tags)
	{
		std::string rt;
		std::vector<std::string>::const_iterator ti = tags.begin(), te = tags.end();
		for (; ti != te; ++ti)
		{
			rt.push_back('/');
			rt.append( *ti);
		}
		return rt;
	}

	static bool tagPathStartsWith( const std::vector<std::string>& tags, const std::vector<std::string>& prefix_tags)
	{
		if (tags.size() < prefix_tags.size()) return false;
		std::vector<std::string>::const_iterator ti = tags.begin();
		std::vector<std::string>::const_iterator pi = prefix_tags.begin(), pe = prefix_tags.end();
		for (; pi != pe && *ti == *pi; ++ti,++pi){}
		return pi == pe;
	}

	static bool hasMarkup( const strus::DocTreeRef& node)
	{
		strus::DocTree::chld_iterator ni = node->chld().begin(), ne = node->chld().end();
		for (; ni != ne; ++ni)
		{
			if (!(*ni)->name().empty()) return true;
			if (hasMarkup(*ni)) return true;
		}
		return false;
	}

	strus::DocTreeRef matchSegment( const Segment& segment, std::vector<Segment>::const_iterator ci, const std::vector<Segment>::const_iterator ce)
	{
		strus::DocTreeRef rt;
		if (segment.key.empty()) return rt;
		for (int cidx=0; ci != ce; ++ci,++cidx)
		{
			if (strus::stringStartsWith( segment.key, ci->key) && tagPathStartsWith( ci->tags, segment.tags))
			{
				rt.reset( new strus::DocTree( ""/*name*/, ""/*value*/, segment.node->attr()));

				char const* ki = segment.key.c_str();
				const char* ke = ki + segment.key.size();
				std::vector<Segment>::const_iterator xi = ci, xe = ce;
				int xidx = cidx;
				for (; xi != xe && stringHasPrefix( ki, ke, xi->key) && tagPathStartsWith( xi->tags, segment.tags);
					ki+=xi->key.size(),++xi,++xidx)
				{
					if (!xi->node->name().empty() && !m_markupset.empty())
					{
						if (m_markupset.find( xi->node->name()) == m_markupset.end())
						{
							break;
						}
					}
					rt->addChld( xi->node);
				}
				if (ki == ke && hasMarkup( rt))
				{
					//... matched
					if (rt->chld().size() == 1 && segment.node->name() == (*rt->chld().begin())->name())
					{
						//... single child, the embed it:
						strus::DocTreeRef single_chld = *rt->chld().begin();
						if (Segment::getKey( single_chld->value()) == Segment::getKey( segment.node->value()))
						{
							rt.reset();
							continue;
						}
						else
						{
							rt = single_chld;
						}
					}
					return rt;
				}
				rt.reset();
			}
		}
		return rt;
	}

	strus::DocTreeRef deepCopyTree( const strus::DocTreeRef& node, const std::map<const strus::DocTree*,const strus::DocTree*>& nodeReplaceMap)
	{
		strus::DocTreeRef rt;
		std::map<const strus::DocTree*,const strus::DocTree*>::const_iterator
			ni = nodeReplaceMap.find( node.get());
		if (ni != nodeReplaceMap.end())
		{
			rt.reset( new strus::DocTree( node->name(), ni->second->value(), node->attr(), ni->second->chld()));
		}
		else
		{
			rt.reset( new strus::DocTree( node->name(), node->value(), node->attr()));
			strus::DocTree::chld_iterator ci = node->chld().begin(), ce = node->chld().end();
			for (; ci != ce; ++ci)
			{
				rt->addChld( deepCopyTree( *ci, nodeReplaceMap));
			}
		}
		return rt;
	}

	strus::DocTreeRef mergeTree( const strus::DocTreeRef& inputTree, const strus::DocTreeRef& markupTree)
	{
		std::vector<Segment> inputseg;
		std::vector<Segment> markupseg;
		segmentTree( inputseg, inputTree, false/*withEmptyKeys*/, std::vector<std::string>());
		segmentTree( markupseg, markupTree, true/*withEmptyKeys*/, std::vector<std::string>());

		std::vector<Segment>::iterator si = inputseg.begin(), se = inputseg.end();
		std::vector<Segment>::const_iterator mi = markupseg.begin(), me = markupseg.end();

		std::map<const strus::DocTree*,const strus::DocTree*> nodeReplaceMap;
		for (; si != se; ++si)
		{
			strus::DocTreeRef mt = matchSegment( *si, mi, me);
			if (mt.get())
			{
				if (g_verbose)
				{
					std::cerr << "MATCH [" << si->content << "] => " << treeToString( mt) << std::endl;
				}
				si->replace = mt;
				nodeReplaceMap[ si->node.get()] = si->replace.get();
			}
		}
		return deepCopyTree( inputTree, nodeReplaceMap);
	}
	
private:
	int m_threadid;
	std::string m_inputPath;
	std::string m_markupPath;
	std::string m_outputPath;
	std::string m_errorPath;
	strus::FileCrawlerInterface* m_crawler;
	const strus::DocumentClassDetectorInterface* m_dclassdetector;
	FileType m_fileType;
	std::set<std::string> m_markupset;
};


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
				errorBuffer.get(), argc, argv, 10,
				"h,help", "v,version",
				"V,verbose", "G,debug:",
				"x,extension:", "k,markup:",
				"t,threads:", "f,fetch:",
				"o,output:", "F,erroutput:");
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

		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus base version ") << STRUS_BASE_VERSION_STRING << std::endl;
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
			std::cout << _TXT("usage:") << " strusMergeMarkup [options] <markuppath> <inputpath>" << std::endl;
			std::cout << "<markuppath> = " << _TXT("path of input file/directory with markup") << std::endl;
			std::cout << "<inputpath>  = " << _TXT("path of input file/directory without markup)") << std::endl;
			std::cout << _TXT("description: Takes file(s) from <inputpath> and merge the markup tags") << std::endl;
			std::cout << _TXT("             from the file(s) in <markuppath> into. Write the results") << std::endl;
			std::cout << _TXT("             to an output file/directory or stdout if not specified") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-V,--verbose" << std::endl;
			std::cout << "    " << _TXT("Verbose output of actions to stderr") << std::endl;
			std::cout << "-G|--debug <COMP>" << std::endl;
			std::cout << "    " << _TXT("Issue debug messages for component <COMP> to stderr") << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    " << _TXT("extension of the input files processed") << std::endl;
			std::cout << "    " << _TXT("(default depending on the content type).") << std::endl;
			std::cout << "-k|--markup <TAGS>" << std::endl;
			std::cout << "    " << _TXT("specify comma separated list of markup tags to process.") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of threads to use") << std::endl;
			std::cout << "-f|--fetch <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of files fetched in each iteration") << std::endl;
			std::cout << "    " << _TXT("Default is 100") << std::endl;
			std::cout << "-o|--output <PATH>" << std::endl;
			std::cout << "    " << _TXT("Write output to subdirectories of") << std::endl;
			std::cout << "    " << _TXT("<PATH> or to stdout if '-' is specified") << std::endl;
			std::cout << "-F|--erroutput <PATH>" << std::endl;
			std::cout << "    " << _TXT("Write tagging errors to output file <PATH> instead of throwing an exception") << std::endl;
			std::cout << "    " << _TXT("Use '-' for stderr.") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string fileext;
		enum {MaxNofThreads=1024};
		int threads = opt( "threads") ? opt.asUint( "threads") : 0;
		if (threads > MaxNofThreads) threads = MaxNofThreads;
		int fetchSize = opt( "fetch") ? opt.asUint( "fetch") : 100;
		if (!fetchSize) fetchSize = 1;
		std::set<std::string> markupset;
		std::string markuppath;
		std::string inputpath;
		std::string outputpath;
		std::string errorpath;

		if (opt( "output"))
		{
			outputpath = opt[ "output"];
		}
		if (opt( "erroutput"))
		{
			errorpath = opt[ "erroutput"];
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
		if (opt( "markup"))
		{
			std::string markupstr = opt[ "markup"];
			char const* mi = markupstr.c_str();
			const char* me = mi + markupstr.size();
			while (mi != me)
			{
				std::string mk;
				for (;mi != me && (unsigned char)*mi <= 32; ++mi){}
				for (;mi != me && (unsigned char)*mi > 32 && *mi != ',' && *mi != ';' && *mi != ':'; ++mi)
				{
					mk.push_back( *mi);
				}
				if (!mk.empty()) markupset.insert( mk);
				for (;mi != me && ((unsigned char)*mi <= 32 || *mi == ',' || *mi == ';' || *mi == ':'); ++mi){}
			}
		}
		if (g_verbose)
		{
			std::set<std::string>::const_iterator mi = markupset.begin(), me = markupset.end();
			for (; mi != me; ++mi)
			{
				std::cerr << strus::string_format( _TXT("using markup tag '%s'"), mi->c_str()) << std::endl;
			}
		}
		int ec = 0;
		markuppath = opt[0];
		inputpath = opt[1];

		if (g_errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("invalid arguments"));
		}
		ec = strus::resolveUpdirReferences( markuppath);
		if (ec) throw strus::runtime_error( _TXT("failed to resolve updir references of path '%s': %s"), markuppath.c_str(), ::strerror(ec));
		ec = strus::resolveUpdirReferences( inputpath);
		if (ec) throw strus::runtime_error( _TXT("failed to resolve updir references of path '%s': %s"), inputpath.c_str(), ::strerror(ec));

		// Initialize:
		strus::local_ptr<strus::FileLocatorInterface> fileloc( strus::createFileLocator_std( g_errorBuffer));
		if (!fileloc.get()) throw std::runtime_error(_TXT("failed to create file locator"));
		strus::local_ptr<strus::TextProcessorInterface> textproc( strus::createTextProcessor( fileloc.get(), g_errorBuffer));
		if (!textproc.get()) throw std::runtime_error(_TXT("failed to create text processor"));
		strus::local_ptr<strus::DocumentClassDetectorInterface> detect( strus::createDetector_std( textproc.get(), g_errorBuffer));
		if (!detect.get()) throw std::runtime_error(_TXT("failed to create document class detector"));
		strus::local_ptr<strus::FileCrawlerInterface> fileCrawler( strus::createFileCrawlerInterface( inputpath, fetchSize, fileext, errorBuffer.get()));
		if (!fileCrawler.get()) throw std::runtime_error( errorBuffer->fetchError());

		// Build the worker data:
		typedef strus::Reference<WorkerBase> WorkerBaseReference;
		WorkerBaseReference workers[ MaxNofThreads];

		int ti = 0, te = threads ? threads : 1;
		for (; ti != te; ++ti)
		{
			int threadid = threads ? (ti+1) : -1;
			workers[ti].reset(
				new Worker(
					threadid, fileCrawler.get(), detect.get(),
					FileTypeXML, markupset,
					inputpath, markuppath, outputpath, errorpath));
		}
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error loading the POS tagger data"));
		}
		// Run the jobs:
		if (threads)
		{
			std::cerr << strus::string_format( _TXT("Starting %d threads ..."), threads) << std::endl;
			std::vector<strus::Reference<strus::thread> > threadGroup;
			for (ti=0; ti<threads; ++ti)
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



