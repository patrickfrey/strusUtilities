/*
 * Copyright (c) 2016 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Program running pattern matching with a rule file on an input file
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/markup_std.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/constants.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/segmenterInstanceInterface.hpp"
#include "strus/patternMatcherInstanceInterface.hpp"
#include "strus/patternMatcherContextInterface.hpp"
#include "strus/patternMatcherInterface.hpp"
#include "strus/tokenMarkupInstanceInterface.hpp"
#include "strus/tokenMarkupContextInterface.hpp"
#include "strus/patternLexerInstanceInterface.hpp"
#include "strus/patternLexerContextInterface.hpp"
#include "strus/patternLexerInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/version.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/reference.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/local_ptr.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

#undef STRUS_LOWLEVEL_DEBUG

static strus::ErrorBufferInterface* g_errorBuffer = 0;

static std::string trimString( const char* li, const char* le)
{
	for (; *li && *li <= 32 && li < le; ++li){}
	for (; le > li && *(le-1) <= 32; --le){}
	return std::string( li, le - li);
}

static void loadFileNamesFromFile( std::vector<std::string>& result, const std::string& filename)
{
	std::string path;
	int ec = strus::getParentPath( filename, path);
	if (ec)
	{
		throw strus::runtime_error(_TXT("error (%u) getting parent path of %s: %s"), ec, filename.c_str(), ::strerror(ec));
	}
	if (!path.empty())
	{
		path += strus::dirSeparator();
	}
	std::string content;
	if (filename == "-")
	{
		ec = strus::readStdin( content);
	}
	else
	{
		ec = strus::readFile( filename, content);
	}
	if (ec)
	{
		throw strus::runtime_error(_TXT("error (%u) reading file list %s: %s"), ec, filename.c_str(), ::strerror(ec));
	}
	char const* ci = content.c_str();
	char const* cn = std::strchr( ci, '\n');
	for (; cn; ci=cn+1,cn = std::strchr( ci, '\n'))
	{
		std::string resultname( trimString( ci, cn));
		if (!resultname.empty())
		{
			if (strus::isRelativePath( resultname))
			{
				result.push_back( path + resultname);
			}
			else
			{
				result.push_back( resultname);
			}
		}
	}
	cn = std::strchr( ci, '\0');
	std::string resultname( trimString( ci, cn));
	if (!resultname.empty()) result.push_back( resultname);
}

static void loadFileNames( std::vector<std::string>& result, const std::string& path, const std::string& fileext)
{
	if (strus::isDir( path))
	{
		std::vector<std::string> filenames;
		int ec = strus::readDirFiles( path, fileext, filenames);
		if (ec)
		{
			throw strus::runtime_error( _TXT( "could not read directory to process '%s' (errno %u)"), path.c_str(), ec);
		}
		std::vector<std::string>::const_iterator fi = filenames.begin(), fe = filenames.end();
		for (; fi != fe; ++fi)
		{
			if (path.empty())
			{
				result.push_back( *fi);
			}
			else
			{
				result.push_back( path + strus::dirSeparator() + *fi);
			}
		}
		std::vector<std::string> subdirs;
		ec = strus::readDirSubDirs( path, subdirs);
		if (ec)
		{
			throw strus::runtime_error( _TXT( "could not read subdirectories to process '%s' (errno %u)"), path.c_str(), ec);
		}
		std::vector<std::string>::const_iterator si = subdirs.begin(), se = subdirs.end();
		for (; si != se; ++si)
		{
			loadFileNames( result, path + strus::dirSeparator() + *si, fileext);
		}
	}
	else
	{
		result.push_back( path);
	}
}

class ThreadContext;

class GlobalContext
{
public:
	explicit GlobalContext(
			const strus::PatternMatcherInstanceInterface* ptinst_,
			const strus::PatternLexerInstanceInterface* crinst_,
			const strus::TextProcessorInterface* textproc_,
			const std::string& segmenterName_,
			const std::vector<std::string>& selectexpr_,
			const std::vector<std::string>& files_,
			unsigned int nofFilesPerFetch_,
			const strus::analyzer::DocumentClass documentClass_,
			const std::map<std::string,int>& markups_,
			const std::string& resultMarker_,
			bool printTokens_)
		:m_ptinst(ptinst_)
		,m_crinst(crinst_)
		,m_textproc(textproc_)
		,m_segmenterName(segmenterName_)
		,m_selectexpr(selectexpr_)
		,m_nofFilesPerFetch(nofFilesPerFetch_)
		,m_documentClass(documentClass_)
		,m_markups(markups_)
		,m_resultMarker(resultMarker_)
		,m_files(files_)
		,m_printTokens(printTokens_)
	{
		m_fileitr = m_files.begin();

		m_tokenMarkup.reset( strus::createTokenMarkupInstance_standard( g_errorBuffer));
		if (g_errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("global context initialization failed"));
		}
	}

	const std::string& segmenterName() const
	{
		return m_segmenterName;
	}

	const strus::analyzer::DocumentClass& documentClass() const
	{
		return m_documentClass;
	}

	const std::vector<std::string>& selectexpr() const
	{
		return m_selectexpr;
	}

	strus::TokenMarkupContextInterface* createTokenMarkupContext() const
	{
		strus::TokenMarkupContextInterface* rt = m_tokenMarkup->createContext();
		if (!rt)
		{
			throw strus::runtime_error(_TXT("failed to create token markup context"));
		}
		return rt;
	}

	const strus::PatternMatcherInstanceInterface* PatternMatcherInstance() const	{return m_ptinst;}
	const strus::PatternLexerInstanceInterface* PatternLexerInstance() const	{return m_crinst;}

	bool printTokens() const							{return m_printTokens;}

	std::vector<std::string> fetchFiles()
	{
		unsigned int filecnt = m_nofFilesPerFetch;
		boost::mutex::scoped_lock lock( m_mutex);
		std::vector<std::string> rt;
		for (; m_fileitr != m_files.end() && filecnt; ++m_fileitr,--filecnt)
		{
			rt.push_back( *m_fileitr);
		}
		return rt;
	}

	const std::map<std::string,int>& markups() const
	{
		return m_markups;
	}
	const std::string& resultMarker() const
	{
		return m_resultMarker;
	}
	const strus::TextProcessorInterface* textproc() const
	{
		return m_textproc;
	}

private:
	boost::mutex m_mutex;
	const strus::PatternMatcherInstanceInterface* m_ptinst;
	const strus::PatternLexerInstanceInterface* m_crinst;
	const strus::TextProcessorInterface* m_textproc;
	std::string m_segmenterName;
	std::vector<std::string> m_selectexpr;
	unsigned int m_nofFilesPerFetch;
	strus::analyzer::DocumentClass m_documentClass;
	strus::local_ptr<strus::TokenMarkupInstanceInterface> m_tokenMarkup;
	std::map<std::string,int> m_markups;
	std::string m_resultMarker;
	std::vector<std::string> m_files;
	std::vector<std::string>::const_iterator m_fileitr;
	bool m_printTokens;
};

class ThreadContext
{
public:
	~ThreadContext(){}

	ThreadContext( const ThreadContext& o)
		:m_globalContext(o.m_globalContext)
		,m_objbuilder(o.m_objbuilder)
		,m_defaultSegmenter(o.m_defaultSegmenter)
		,m_defaultSegmenterInstance(o.m_defaultSegmenterInstance)
		,m_segmentermap(o.m_segmentermap)
		,m_threadid(o.m_threadid)
		,m_outputfile(o.m_outputfile)
		,m_outputfilestream(o.m_outputfilestream)
		,m_output(o.m_output)
		,m_outerrfile(o.m_outerrfile)
		,m_outerrfilestream(o.m_outerrfilestream)
		,m_outerr(o.m_outerr)
	{}

private:
	std::string getOutputFileName( const std::string& outputfile_) const
	{
		if (m_threadid > 0)
		{
			std::ostringstream namebuf;
			char const* substpos = std::strchr( outputfile_.c_str(), '.');
			if (substpos)
			{
				namebuf << std::string( outputfile_.c_str(), substpos-outputfile_.c_str())
					<< m_threadid << substpos;
			}
			else
			{
				namebuf << outputfile_ << m_threadid;
			}
			return namebuf.str();
		}
		else
		{
			return outputfile_;
		}
	}

public:
	ThreadContext( GlobalContext* globalContext_, const strus::AnalyzerObjectBuilderInterface* objbuilder_, unsigned int threadid_, const std::string& outputfile_="", const std::string& outerrfile_="")
		:m_globalContext(globalContext_)
		,m_objbuilder(objbuilder_)
		,m_defaultSegmenter(0)
		,m_defaultSegmenterInstance()
		,m_segmentermap()
		,m_threadid(threadid_)
		,m_outputfile()
		,m_outputfilestream()
		,m_output(0)
		,m_outerrfile()
		,m_outerrfilestream()
		,m_outerr(0)
	{
		if (!m_globalContext->segmenterName().empty())
		{
			m_defaultSegmenter = m_globalContext->textproc()->getSegmenterByName( m_globalContext->segmenterName());
			if (!m_defaultSegmenter)
			{
				throw strus::runtime_error(_TXT("failed to get default segmenter by name: %s"), g_errorBuffer->fetchError());
			}
			m_defaultSegmenterInstance.reset( m_defaultSegmenter->createInstance());
			if (!m_defaultSegmenterInstance.get())
			{
				throw strus::runtime_error(_TXT("failed to create default segmenter instace: %s"), g_errorBuffer->fetchError());
			}
			initSegmenterInstance( m_defaultSegmenterInstance.get());
		}
		if (outputfile_.empty())
		{
			m_output = &std::cout;
		}
		else
		{
			m_outputfile = getOutputFileName( outputfile_);
			try
			{
				m_outputfilestream.reset( new std::ofstream( m_outputfile.c_str()));
			}
			catch (const std::exception& err)
			{
				throw strus::runtime_error(_TXT("failed to open file '%s' for output: %s"), m_outputfile.c_str(), err.what());
			}
			m_output = m_outputfilestream.get();
		}
		if (outerrfile_.empty())
		{
			m_outerr = &std::cerr;
		}
		else
		{
			m_outerrfile = getOutputFileName( outerrfile_);
			try
			{
				m_outerrfilestream.reset( new std::ofstream( m_outerrfile.c_str()));
			}
			catch (const std::exception& err)
			{
				throw strus::runtime_error(_TXT("failed to open file '%s' for errors: %s"), m_outerrfile.c_str(), err.what());
			}
			m_outerr = m_outerrfilestream.get();
		}
	}

	void initSegmenterInstance( strus::SegmenterInstanceInterface* intrf) const
	{
		std::vector<std::string>::const_iterator ei = m_globalContext->selectexpr().begin(), ee = m_globalContext->selectexpr().end();
		for (int eidx=1; ei != ee; ++ei,++eidx)
		{
			intrf->defineSelectorExpression( eidx, *ei);
		}
	}

	strus::analyzer::DocumentClass getDocumentClass( const std::string& content) const
	{
		if (m_globalContext->documentClass().defined())
		{
			return m_globalContext->documentClass();
		}
		else
		{
			strus::analyzer::DocumentClass rt;
			if (!m_globalContext->textproc()->detectDocumentClass( rt, content.c_str(), content.size()))
			{
				throw strus::runtime_error(_TXT("failed to detect document class"));
			}
			return rt;
		}
	}

	const strus::SegmenterInstanceInterface* getSegmenterInstance( const std::string& content, const strus::analyzer::DocumentClass& documentClass)
	{
		if (m_defaultSegmenter)
		{
			return m_defaultSegmenterInstance.get();
		}
		else
		{
			SegmenterMap::const_iterator si = m_segmentermap.find( documentClass.mimeType());
			if (si == m_segmentermap.end())
			{
				const strus::SegmenterInterface* segmenter = m_globalContext->textproc()->getSegmenterByMimeType( documentClass.mimeType());
				if (!segmenter)
				{
					throw strus::runtime_error(_TXT("no segmenter defined for mime type '%s'"), documentClass.mimeType().c_str());
				}
				strus::analyzer::SegmenterOptions segmenteropts;
				if (!documentClass.scheme().empty())
				{
					segmenteropts = m_globalContext->textproc()->getSegmenterOptions( documentClass.scheme());
				}
				strus::Reference<strus::SegmenterInstanceInterface> segmenterinstref( segmenter->createInstance( segmenteropts));
				if (!segmenterinstref.get())
				{
					throw strus::runtime_error(_TXT("failed to create segmenter instance for mime type '%s'"), documentClass.mimeType().c_str());
				}
				m_segmentermap[ documentClass.mimeType()] = segmenterinstref;
				initSegmenterInstance( segmenterinstref.get());
				return segmenterinstref.get();
			}
			else
			{
				return si->second.get();
			}
		}
	}

	struct PositionInfo
	{
		strus::SegmenterPosition segpos;
		std::size_t srcpos;

		PositionInfo( strus::SegmenterPosition segpos_, std::size_t srcpos_)
			:segpos(segpos_),srcpos(srcpos_){}
		PositionInfo( const PositionInfo& o)
			:segpos(o.segpos),srcpos(o.srcpos){}
	};

	static std::string encodeOutput( const char* ptr, std::size_t size)
	{
		std::string rt;
		std::size_t si = 0, se = size;
		for (; si != se; ++si)
		{
			if ((unsigned char)ptr[si] <= 32)
			{
				rt.push_back( ' ');
			}
			else
			{
				rt.push_back( ptr[si]);
			}
		}
		return rt;
	}

	void processDocument( const std::string& filename)
	{
		std::string content;

		int ec;
		if (filename == "-")
		{
			ec = strus::readStdin( content);
		}
		else
		{
			ec = strus::readFile( filename, content);
		}
		if (ec)
		{
			throw strus::runtime_error(_TXT("error (%u) reading document %s: %s"), ec, filename.c_str(), ::strerror(ec));
		}
		strus::local_ptr<strus::PatternMatcherContextInterface> mt( m_globalContext->PatternMatcherInstance()->createContext());
		strus::local_ptr<strus::PatternLexerContextInterface> crctx( m_globalContext->PatternLexerInstance()->createContext());
		strus::analyzer::DocumentClass documentClass = getDocumentClass( content);
		const strus::SegmenterInstanceInterface* segmenterInstance = getSegmenterInstance( content, documentClass);
		strus::local_ptr<strus::SegmenterContextInterface> segmenter( segmenterInstance->createContext( documentClass));

		*m_output << m_globalContext->resultMarker() << filename << ":" << std::endl;

		segmenter->putInput( content.c_str(), content.size(), true);
		int id;
		strus::SegmenterPosition segmentpos;
		const char* segment;
		std::size_t segmentsize;
		std::vector<PositionInfo> segmentposmap;
		std::string source;
		std::size_t segmentidx;
		unsigned int ordposOffset = 0;
		strus::SegmenterPosition prev_segmentpos = (strus::SegmenterPosition)std::numeric_limits<std::size_t>::max();
		while (segmenter->getNext( id, segmentpos, segment, segmentsize))
		{
			segmentidx = segmentposmap.size();
			if (prev_segmentpos != segmentpos)
			{
				segmentposmap.push_back( PositionInfo( segmentpos, source.size()));
				source.append( segment, segmentsize);
#ifdef STRUS_LOWLEVEL_DEBUG
				*m_outerr << "processing segment " << id << " [" << encodeOutput(segment,segmentsize) << "] at " << segmentpos << std::endl;
#endif
				std::vector<strus::analyzer::PatternLexem> crmatches = crctx->match( segment, segmentsize);
				if (crmatches.size() == 0 && g_errorBuffer->hasError())
				{
					throw std::runtime_error( "failed to scan for tokens with char regex match automaton");
				}
				std::vector<strus::analyzer::PatternLexem>::iterator ti = crmatches.begin(), te = crmatches.end();
				for (; ti != te; ++ti)
				{
					ti->setOrigseg( segmentidx);
					ti->setOrdpos( ti->ordpos() + ordposOffset);
					if (m_globalContext->printTokens())
					{
						const char* lexemname = m_globalContext->PatternLexerInstance()->getLexemName( ti->id());
						if (lexemname)
						{
							*m_output << ti->ordpos() << " [" << (segmentpos+ti->origpos()) << "] : " << ti->id() << " " << lexemname << " " << encodeOutput( segment+ti->origpos(), ti->origsize()) << std::endl;
						}
						else
						{
							*m_output << ti->ordpos() << ": " << ti->id() << " ? " << encodeOutput( segment+ti->origpos(), ti->origsize()) << std::endl;
						}
					}
					mt->putInput( *ti);
				}
				if (crmatches.size() > 0)
				{
					ordposOffset = crmatches.back().ordpos();
				}
			}
		}
		if (g_errorBuffer->hasError())
		{
			throw std::runtime_error("error matching rules");
		}
		std::vector<strus::analyzer::PatternMatcherResult> results = mt->fetchResults();
		if (m_globalContext->markups().empty())
		{
			printResults( *m_output, segmentposmap, results, source);
		}
		else
		{
			markupResults( *m_output, results, documentClass, content, segmenterInstance);
		}
	}

	void printResults( std::ostream& out, const std::vector<PositionInfo>& segmentposmap, const std::vector<strus::analyzer::PatternMatcherResult>& results, const std::string& src)
	{
		std::vector<strus::analyzer::PatternMatcherResult>::const_iterator
			ri = results.begin(), re = results.end();
		for (; ri != re; ++ri)
		{
			std::size_t start_segpos = segmentposmap[ ri->start_origseg()].segpos;
			std::size_t end_segpos = segmentposmap[ ri->end_origseg()].segpos;
			out << ri->name() << " [" << ri->start_ordpos() << ".." << ri->end_ordpos()
				<< ", " << start_segpos << "|" << ri->start_origpos() << " .. "
				<< end_segpos << "|" << ri->end_origpos() << "]:";
			std::vector<strus::analyzer::PatternMatcherResultItem>::const_iterator
				ei = ri->items().begin(), ee = ri->items().end();

			for (; ei != ee; ++ei)
			{
				start_segpos = segmentposmap[ ei->start_origseg()].segpos;
				end_segpos = segmentposmap[ ei->end_origseg()].segpos;
				out << " " << ei->name() << " [" << ei->start_ordpos() << ".." << ei->end_ordpos()
						<< ", " << start_segpos << "|" << ei->start_origpos() << " .. " << end_segpos << "|" << ei->end_origpos() << "]";
				std::size_t start_srcpos = segmentposmap[ ei->start_origseg()].srcpos + ei->start_origpos();
				std::size_t end_srcpos = segmentposmap[ ei->start_origseg()].srcpos + ei->end_origpos();
				out << " '" << encodeOutput( src.c_str() + start_srcpos, end_srcpos - start_srcpos) << "'";
			}
			out << std::endl;
		}
	}

	void markupResults( std::ostream& out,
				const std::vector<strus::analyzer::PatternMatcherResult>& results,
				const strus::analyzer::DocumentClass& documentClass, const std::string& src,
				const strus::SegmenterInstanceInterface* segmenterInstance)
	{
		strus::local_ptr<strus::TokenMarkupContextInterface> markupContext( m_globalContext->createTokenMarkupContext());

		std::vector<strus::analyzer::PatternMatcherResult>::const_iterator
			ri = results.begin(), re = results.end();
		for (; ri != re; ++ri)
		{
			std::map<std::string,int>::const_iterator mi = m_globalContext->markups().find( ri->name());
			if (mi != m_globalContext->markups().end())
			{
				markupContext->putMarkup(
					ri->start_origseg(), ri->start_origpos(),
					ri->end_origseg(), ri->end_origpos(),
					strus::analyzer::TokenMarkup( ri->name()), mi->second);
			}
			std::vector<strus::analyzer::PatternMatcherResultItem>::const_iterator
				ei = ri->items().begin(), ee = ri->items().end();

			for (; ei != ee; ++ei)
			{
				std::map<std::string,int>::const_iterator mi = m_globalContext->markups().find( ei->name());
				if (mi != m_globalContext->markups().end())
				{
					markupContext->putMarkup(
						ei->start_origseg(), ei->start_origpos(),
						ei->end_origseg(), ei->end_origpos(),
						strus::analyzer::TokenMarkup( ri->name()), mi->second);
				}
			}
		}
		std::string content = markupContext->markupDocument( segmenterInstance, documentClass, src);
		out << content << std::endl;
	}

	void run()
	{
		for (;;)
		{
			std::vector<std::string> filenames = m_globalContext->fetchFiles();
			if (filenames.empty()) break;
			std::vector<std::string>::const_iterator fi = filenames.begin(), fe = filenames.end();
			for (; fi != fe; ++fi)
			{
#ifdef STRUS_LOWLEVEL_DEBUG
				*m_outerr << strus::string_format( _TXT("thread %u processing file '%s'"), m_threadid, *fi) << std::endl;
#endif
				try
				{
					processDocument( *fi);
					if (g_errorBuffer->hasError())
					{
						*m_outerr << strus::string_format( _TXT("error thread %u file '%s': %s"), m_threadid, fi->c_str(), g_errorBuffer->fetchError()) << std::endl;
					}
				}
				catch (const std::runtime_error& err)
				{
					if (g_errorBuffer->hasError())
					{
						*m_outerr << strus::string_format( _TXT("error thread %u file '%s': %s, %s"), m_threadid, fi->c_str(), err.what(), g_errorBuffer->fetchError()) << std::endl;
					}
					else
					{
						*m_outerr << strus::string_format( _TXT("error thread %u file '%s': %s"), m_threadid, fi->c_str(), err.what()) << std::endl;
					}
				}
				catch (const std::bad_alloc&)
				{
					*m_outerr << strus::string_format( _TXT( "out of memory thread %u processing document '%s'"), m_threadid, fi->c_str()) << std::endl;
				}
			}
		}
		if (g_errorBuffer->hasError())
		{
			*m_outerr << strus::string_format( _TXT("error thread %u: %s"), m_threadid, g_errorBuffer->fetchError()) << std::endl;
		}
		g_errorBuffer->releaseContext();
	}

private:
	GlobalContext* m_globalContext;
	const strus::AnalyzerObjectBuilderInterface* m_objbuilder;
	const strus::SegmenterInterface* m_defaultSegmenter;
	strus::Reference<strus::SegmenterInstanceInterface> m_defaultSegmenterInstance;
	typedef std::map<std::string,strus::Reference<strus::SegmenterInstanceInterface> > SegmenterMap;
	SegmenterMap m_segmentermap;
	unsigned int m_threadid;
	std::string m_outputfile;
	strus::utils::SharedPtr<std::ofstream> m_outputfilestream;
	std::ostream* m_output;
	std::string m_outerrfile;
	strus::utils::SharedPtr<std::ofstream> m_outerrfilestream;
	std::ostream* m_outerr;
};


int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::local_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	g_errorBuffer = errorBuffer.get();

	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		opt = strus::ProgramOptions(
				argc, argv, 23,
				"h,help", "v,version", "license",
				"g,segmenter:", "x,ext:", "C,contenttype:", "F,filelist",
				"e,expression:", "K,tokens", "p,program:",
				"Z,marker:", "H,markup:",
				"X,lexer:", "Y,matcher:",
				"t,threads:", "f,fetch:", "o,output:", "O,outerr:",
				"M,moduledir:", "m,module:", "r,rpc:", "R,resourcedir:", "T,trace:");

		if (opt( "help")) printUsageAndExit = true;
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 1)
			{
				std::cerr << _TXT("error too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 1)
			{
				std::cerr << _TXT("error too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		unsigned int nofThreads = 0;
		if (opt("threads"))
		{
			nofThreads = opt.asUint( "threads");
			if (nofThreads > 1)
			{
				strus::ErrorBufferInterface* alterr = strus::createErrorBuffer_standard( 0, nofThreads+1);
				if (!alterr)
				{
					std::cerr << _TXT("failed to recreate error buffer") << std::endl;
					return -1;
				}
				errorBuffer.reset( alterr);
				g_errorBuffer = alterr;
			}
		}
		strus::local_ptr<strus::ModuleLoaderInterface>
				moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("failed to create module loader"));

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
#if STRUS_PATTERN_STD_ENABLED
		if (!moduleLoader->loadModule( strus::Constants::standard_pattern_matcher_module()))
		{
			std::cerr << _TXT("failed to load module ") << "'" << strus::Constants::standard_pattern_matcher_module() << "': " << g_errorBuffer->fetchError() << std::endl;
		}
#endif
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
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusPatternMatch [options] <inputpath>" << std::endl;
			std::cout << "<inputpath>  : " << _TXT("input file or directory to process") << std::endl;
			std::cout << _TXT("description: Runs pattern matching on the input documents and dumps the result to stdout.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "    " << _TXT("The module modstrus_analyzer_pattern is implicitely defined") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-K|--tokens" << std::endl;
			std::cout << "    " << _TXT("Print the tokenization used for pattern matching too") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of matcher threads to use") << std::endl;
			std::cout << "-f|--fetch <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of files to fetch per iteration") << std::endl;
			std::cout << "-x|--ext <FILEEXT>" << std::endl;
			std::cout << "    " << _TXT("Do only process files with extension <FILEEXT>") << std::endl;
			std::cout << "-C|--contenttype <CT>" << std::endl;
			std::cout << "    " << _TXT("forced definition of the document class of all documents processed.") << std::endl;
			std::cout << "-F|--filelist" << std::endl;
			std::cout << "    " << _TXT("inputpath is a file containing the list of files to process.") << std::endl;
			std::cout << "-e|--expression <EXP>" << std::endl;
			std::cout << "    " << _TXT("Define a selection expression <EXP> for the content to process") << std::endl;
			std::cout << "    " << _TXT("  (default if nothing specified is \"//()\"") << std::endl;
			std::cout << "-H|--markup <NAME>" << std::endl;
			std::cout << "    " << _TXT("Output the content with markups of the rules or variables with name <NAME>") << std::endl;
			std::cout << "-Z|--marker <MRK>" << std::endl;
			std::cout << "    " << _TXT("Define a character sequence inserted before every result declaration") << std::endl;
			std::cout << "-X|--lexer <LX>" << std::endl;
			std::cout << "    " << _TXT("Use pattern lexer named <LX>") << std::endl;
			std::cout << "    " << _TXT("Default is 'std'") << std::endl;
			std::cout << "-Y|--matcher <PT>" << std::endl;
			std::cout << "    " << _TXT("Use pattern lexer named <PT>") << std::endl;
			std::cout << "    " << _TXT("Default is 'std'") << std::endl;
			std::cout << "-p|--program <PRG>" << std::endl;
			std::cout << "    " << _TXT("Load program <PRG> with patterns to process") << std::endl;
			std::cout << "-o|--output <FILE>" << std::endl;
			std::cout << "    " << _TXT("Write output to file <FILE> (thread id is inserted before '.' with threads)") << std::endl;
			std::cout << "-O|--outerr <FILE>" << std::endl;
			std::cout << "    " << _TXT("Write errors to file <FILE> (thread id is inserted before '.' with threads)") << std::endl;
			std::cout << "-g|--segmenter <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use the document segmenter with name <NAME>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string inputpath = opt[ 0];
		std::string segmentername;
		std::string fileext;
		std::string contenttype;
		std::vector<std::string> expressions;
		std::string matcher( strus::Constants::standard_pattern_matcher());
		std::string lexer( strus::Constants::standard_pattern_matcher());
		std::string programfile;
		bool printTokens = false;
		std::map<std::string,int> markups;
		std::string resultmarker;
		unsigned int nofFilesFetch = 1;
		std::string outputfile;
		std::string outerrfile;
		bool inputIsAListOfFiles = false;

		if (opt( "segmenter"))
		{
			segmentername = opt[ "segmenter"];
		}
		if (opt( "ext"))
		{
			fileext = opt[ "ext"];
			if (opt( "filelist"))
			{
				throw strus::runtime_error(_TXT("called with contradicting options --%s and --%s"), "ext", "filelist");
			}
		}
		if (opt( "contenttype"))
		{
			contenttype = opt[ "contenttype"];
		}
		if (opt("filelist"))
		{
			if (opt( "ext"))
			{
				throw strus::runtime_error(_TXT("called with contradicting options --%s and --%s"), "ext", "filelist");
			}
			inputIsAListOfFiles = true;
		}
		if (opt( "expression"))
		{
			expressions = opt.list( "expression");
		}
		if (opt( "tokens"))
		{
			printTokens = true;
		}
		if (opt( "matcher"))
		{
			matcher = opt[ "matcher"];
		}
		if (opt( "lexer"))
		{
			lexer = opt[ "lexer"];
		}
		if (opt( "program"))
		{
			programfile = opt[ "program"];
		}
		if (opt( "markup"))
		{
			std::vector<std::string> list = opt.list( "markup");
			std::vector<std::string>::const_iterator li = list.begin(), le = list.end();
			for (unsigned int lidx=1; li != le; ++li,++lidx)
			{
				markups.insert( std::pair<std::string, int>( *li, lidx));
			}
		}
		if (opt( "marker"))
		{
			resultmarker = opt["marker"];
		}
		if (opt( "threads"))
		{
			nofThreads = opt.asUint( "threads");
		}
		if (opt( "fetch"))
		{
			nofFilesFetch = opt.asUint( "fetch");
			if (!nofFilesFetch) nofFilesFetch = 1;
		}
		if (opt( "output"))
		{
			outputfile = opt[ "output"];
		}
		if (opt( "outerr"))
		{
			outerrfile = opt[ "outerr"];
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
		// Create objects for analyzer:
		strus::local_ptr<strus::RpcClientMessagingInterface> messaging;
		strus::local_ptr<strus::RpcClientInterface> rpcClient;
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;

		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error(_TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw strus::runtime_error(_TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create rpc analyzer object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create analyzer object builder"));
		}

		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::AnalyzerObjectBuilderInterface* proxy = (*ti)->createProxy( analyzerBuilder.get());
			analyzerBuilder.release();
			analyzerBuilder.reset( proxy);
		}
		if (g_errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("error in initialization"));
		}
		// Create objects:
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error(_TXT("could not get text processor interface"));
		const strus::PatternMatcherInterface* pti = textproc->getPatternMatcher( matcher);
		if (!pti) throw strus::runtime_error(_TXT("unknown pattern matcher"));
		const strus::PatternLexerInterface* lxi = textproc->getPatternLexer( lexer);
		if (!lxi) throw strus::runtime_error(_TXT("unknown pattern lexer"));
		strus::local_ptr<strus::PatternMatcherInstanceInterface> ptinst( pti->createInstance());
		strus::local_ptr<strus::PatternLexerInstanceInterface> lxinst( lxi->createInstance());

		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty() && !strus::parseDocumentClass( documentClass, contenttype, errorBuffer.get()))
		{
			throw strus::runtime_error(_TXT("failed to parse document class"));
		}
		std::cerr << "load program ..." << std::endl;
		std::string programsrc;
		int ec = strus::readFile( programfile, programsrc);
		if (ec) throw strus::runtime_error(_TXT("error (%u) reading rule file %s: %s"), ec, programfile.c_str(), ::strerror(ec));
		std::vector<std::string> warnings;
		if (!strus::loadPatternMatcherProgramWithLexer( lxinst.get(), ptinst.get(), programsrc, g_errorBuffer, warnings))
		{
			throw strus::runtime_error(_TXT("failed to load program"));
		}
		std::vector<std::string>::const_iterator wi = warnings.begin(), we = warnings.end();
		for (; wi != we; ++wi)
		{
			std::cerr << "warning: " << *we << std::endl;
		}
		if (expressions.empty())
		{
			if (segmentername == "plain")
			{
				expressions.push_back( "");
			}
			else
			{
				expressions.push_back( "//()");
			}
		}
		std::vector<std::string> inputfiles;
		if (inputIsAListOfFiles)
		{
			loadFileNamesFromFile( inputfiles, inputpath);
		}
		else
		{
			loadFileNames( inputfiles, inputpath, fileext);
		}
		GlobalContext globalContext(
				ptinst.get(), lxinst.get(), textproc, segmentername,
				expressions, inputfiles, nofFilesFetch, documentClass,
				markups, resultmarker, printTokens);

		std::cerr << "start matching ..." << std::endl;
		if (nofThreads)
		{
			fprintf( stderr, _TXT("starting %u threads for evaluation ...\n"), nofThreads);

			std::vector<strus::Reference<ThreadContext> > processorList;
			processorList.reserve( nofThreads);
			for (unsigned int ti=0; ti<nofThreads; ++ti)
			{
				processorList.push_back( new ThreadContext( &globalContext, analyzerBuilder.get(), ti+1, outputfile, outerrfile));
			}
			{
				boost::thread_group tgroup;
				for (unsigned int ti=0; ti<nofThreads; ++ti)
				{
					tgroup.create_thread( boost::bind( &ThreadContext::run, processorList[ti].get()));
				}
				tgroup.join_all();
			}
		}
		else
		{
			ThreadContext ctx( &globalContext, analyzerBuilder.get(), 0, outputfile, outerrfile);
			ctx.run();
		}
		if (g_errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("uncaught error in pattern matcher"));
		}
		std::cerr << _TXT("OK done") << std::endl;
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


