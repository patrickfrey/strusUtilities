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
#include "strus/lib/analyzer_prgload_std.hpp"
#include "strus/lib/pattern_resultformat.hpp"
#include "strus/lib/segmenter_cjson.hpp"
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
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "strus/debugTraceInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/reference.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/string_conv.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/shared_ptr.hpp"
#include "strus/base/thread.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include "private/versionUtilities.hpp"
#include "private/programLoader.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <memory>

#define STRUS_DBGTRACE_COMPONENT_NAME "pattern"
#define STRUS_PATTERN_DEFAULT_RESULT_FORMAT  "{name} [{ordpos}..{ordend}, {startseg}\\|{startpos} .. {endseg}\\|{endpos}]:{value}| {name} [{ordpos}..{ordend}, {startseg}\\|{startpos} .. {endseg}\\|{endpos}] '{value}'|"

static strus::ErrorBufferInterface* g_errorhnd = 0;

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
			const std::string& docid_selectexpr_,
			const std::string& fileprefix_,
			const std::vector<std::string>& files_,
			unsigned int nofFilesPerFetch_,
			const strus::analyzer::DocumentClass documentClass_,
			const std::map<std::string,int>& markups_,
			const std::string& resultMarker_,
			const std::string& resultFormat_,
			const std::pair<std::string,std::string>& resultMarkupTag_,
			bool printTokens_,
			bool fileContainsMultipleDocuments_)
		:m_ptinst(ptinst_)
		,m_crinst(crinst_)
		,m_textproc(textproc_)
		,m_segmenterName(segmenterName_)
		,m_selectexpr(selectexpr_)
		,m_docid_selectexpr(docid_selectexpr_)
		,m_nofFilesPerFetch(nofFilesPerFetch_)
		,m_documentClass(documentClass_)
		,m_markups(markups_)
		,m_resultMarker(resultMarker_)
		,m_resultMarkupTag(resultMarkupTag_)
		,m_fileprefix(fileprefix_)
		,m_files(files_)
		,m_formatmap()
		,m_printTokens(printTokens_)
		,m_fileContainsMultipleDocuments(fileContainsMultipleDocuments_)
	{
		m_fileitr = m_files.begin();
		if (!resultFormat_.empty())
		{
			m_formatmap.reset( new strus::PatternResultFormatMap( resultFormat_.c_str(), g_errorhnd));
			if (g_errorhnd->hasError()) throw std::runtime_error( g_errorhnd->fetchError());
		}
		m_tokenMarkup.reset( strus::createTokenMarkupInstance_standard( g_errorhnd));
		if (g_errorhnd->hasError())
		{
			throw std::runtime_error( _TXT("global context initialization failed"));
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

	const std::string& docid_selectexpr() const
	{
		return m_docid_selectexpr;
	}

	strus::TokenMarkupContextInterface* createTokenMarkupContext( const strus::SegmenterInstanceInterface* segmenter) const
	{
		strus::TokenMarkupContextInterface* rt = m_tokenMarkup->createContext( segmenter);
		if (!rt)
		{
			throw std::runtime_error( _TXT("failed to create token markup context"));
		}
		return rt;
	}

	const strus::PatternMatcherInstanceInterface* PatternMatcherInstance() const	{return m_ptinst;}
	const strus::PatternLexerInstanceInterface* PatternLexerInstance() const	{return m_crinst;}

	bool printTokens() const							{return m_printTokens;}
	bool fileContainsMultipleDocuments() const					{return m_fileContainsMultipleDocuments;}

	std::vector<std::string> fetchFiles()
	{
		unsigned int filecnt = m_nofFilesPerFetch;
		strus::unique_lock lock( m_mutex);
		std::vector<std::string> rt;
		for (; m_fileitr != m_files.end() && filecnt; ++m_fileitr,--filecnt)
		{
			rt.push_back( *m_fileitr);
		}
		return rt;
	}

	const std::string& fileprefix() const
	{
		return m_fileprefix;
	}
	const std::map<std::string,int>& markups() const
	{
		return m_markups;
	}
	const std::string& resultMarker() const
	{
		return m_resultMarker;
	}
	const std::pair<std::string,std::string>& resultMarkupTag() const
	{
		return m_resultMarkupTag;
	}
	const strus::TextProcessorInterface* textproc() const
	{
		return m_textproc;
	}
	const strus::PatternResultFormatMap* formatmap() const
	{
		return m_formatmap.get();
	}

private:
	strus::mutex m_mutex;
	const strus::PatternMatcherInstanceInterface* m_ptinst;
	const strus::PatternLexerInstanceInterface* m_crinst;
	const strus::TextProcessorInterface* m_textproc;
	std::string m_segmenterName;
	std::vector<std::string> m_selectexpr;
	std::string m_docid_selectexpr;
	unsigned int m_nofFilesPerFetch;
	strus::analyzer::DocumentClass m_documentClass;
	strus::local_ptr<strus::TokenMarkupInstanceInterface> m_tokenMarkup;
	std::map<std::string,int> m_markups;
	std::string m_resultMarker;
	std::pair<std::string,std::string> m_resultMarkupTag;
	std::string m_fileprefix;
	std::vector<std::string> m_files;
	std::vector<std::string>::const_iterator m_fileitr;
	strus::Reference<strus::PatternResultFormatMap> m_formatmap;
	bool m_printTokens;
	bool m_fileContainsMultipleDocuments;
};

class ThreadContext
{
public:
	~ThreadContext(){}

	ThreadContext( const ThreadContext& o)
		:m_globalContext(o.m_globalContext)
		,m_debugTrace(o.m_debugTrace)
		,m_objbuilder(o.m_objbuilder)
		,m_defaultSegmenter(o.m_defaultSegmenter)
		,m_defaultDocidSegmenterInstance(o.m_defaultDocidSegmenterInstance)
		,m_defaultProcessSegmenterInstance(o.m_defaultProcessSegmenterInstance)
		,m_docidSegmenterMap(o.m_docidSegmenterMap)
		,m_processSegmenterMap(o.m_processSegmenterMap)
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
		,m_debugTrace(g_errorhnd->debugTrace())
		,m_objbuilder(objbuilder_)
		,m_defaultSegmenter(0)
		,m_defaultDocidSegmenterInstance()
		,m_defaultProcessSegmenterInstance()
		,m_docidSegmenterMap()
		,m_processSegmenterMap()
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
				throw strus::runtime_error(_TXT("failed to get default segmenter by name: %s"), g_errorhnd->fetchError());
			}
			m_defaultProcessSegmenterInstance.reset( m_defaultSegmenter->createInstance());
			if (!m_defaultProcessSegmenterInstance.get())
			{
				throw strus::runtime_error(_TXT("failed to create default segmenter instace: %s"), g_errorhnd->fetchError());
			}
			initProcessSegmenterInstance( m_defaultProcessSegmenterInstance.get());

			m_defaultDocidSegmenterInstance.reset( m_defaultSegmenter->createInstance());
			if (!m_defaultDocidSegmenterInstance.get())
			{
				throw strus::runtime_error(_TXT("failed to create default segmenter instace: %s"), g_errorhnd->fetchError());
			}
			initDocidSegmenterInstance( m_defaultDocidSegmenterInstance.get());
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

	void initProcessSegmenterInstance( strus::SegmenterInstanceInterface* intrf) const
	{
		std::vector<std::string>::const_iterator ei = m_globalContext->selectexpr().begin(), ee = m_globalContext->selectexpr().end();
		for (int eidx=1; ei != ee; ++ei,++eidx)
		{
			intrf->defineSelectorExpression( eidx, *ei);
		}
	}

	void initDocidSegmenterInstance( strus::SegmenterInstanceInterface* intrf) const
	{
		if (!m_globalContext->docid_selectexpr().empty())
		{
			intrf->defineSelectorExpression( 1, m_globalContext->docid_selectexpr());
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
			enum {MaxHdrSize = 8092};
			std::size_t hdrsize = content.size() > MaxHdrSize ? MaxHdrSize : content.size();
			if (!m_globalContext->textproc()->detectDocumentClass( rt, content.c_str(), hdrsize, MaxHdrSize < content.size()))
			{
				throw std::runtime_error( _TXT("failed to detect document class"));
			}
			return rt;
		}
	}

	const strus::SegmenterInstanceInterface* getProcessSegmenterInstance( const std::string& content, const strus::analyzer::DocumentClass& documentClass)
	{
		if (m_defaultSegmenter)
		{
			return m_defaultProcessSegmenterInstance.get();
		}
		else
		{
			
			SegmenterMap::const_iterator si = m_processSegmenterMap.find( documentClass.mimeType());
			if (si == m_processSegmenterMap.end())
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
				m_processSegmenterMap[ documentClass.mimeType()] = segmenterinstref;
				initProcessSegmenterInstance( segmenterinstref.get());
				return segmenterinstref.get();
			}
			else
			{
				return si->second.get();
			}
		}
	}

	const strus::SegmenterInstanceInterface* getDocidSegmenterInstance( const std::string& content, const strus::analyzer::DocumentClass& documentClass)
	{
		if (m_defaultSegmenter)
		{
			return m_defaultDocidSegmenterInstance.get();
		}
		else
		{
			
			SegmenterMap::const_iterator si = m_docidSegmenterMap.find( documentClass.mimeType());
			if (si == m_docidSegmenterMap.end())
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
				m_docidSegmenterMap[ documentClass.mimeType()] = segmenterinstref;
				initDocidSegmenterInstance( segmenterinstref.get());
				return segmenterinstref.get();
			}
			else
			{
				return si->second.get();
			}
		}
	}

	std::string getDocidFromDocument( const std::string& content, const strus::analyzer::DocumentClass& documentClass)
	{
		const strus::SegmenterInstanceInterface* segmenterInstance = getDocidSegmenterInstance( content, documentClass);
		strus::local_ptr<strus::SegmenterContextInterface> segmenter( segmenterInstance->createContext( documentClass));
		if (!segmenter.get()) throw std::runtime_error(g_errorhnd->fetchError());
			
		segmenter->putInput( content.c_str(), content.size(), true);
		int id;
		strus::SegmenterPosition segmentpos;
		const char* segment;
		std::size_t segmentsize;
		if (segmenter->getNext( id, segmentpos, segment, segmentsize))
		{
			std::string rt = std::string( segment, segmentsize);
			if (segmenter->getNext( id, segmentpos, segment, segmentsize))
			{
				throw strus::runtime_error(_TXT("duplicate definition of docid in document"));
			}
			return rt;
		}
		else if (g_errorhnd->hasError())
		{
			throw strus::runtime_error(_TXT("failed to get docid from document: %s"), g_errorhnd->fetchError());
		}
		else
		{
			throw strus::runtime_error(_TXT("no docid found in document"));
		}
	}

	typedef std::map<strus::SegmenterPosition,std::size_t> SegmenterPositionMap;

	static std::string encodeOutput( const char* ptr, std::size_t size, std::size_t maxsize)
	{
		std::string rt;
		if (maxsize && maxsize < size)
		{
			enum {B10000000=(128),B11000000=(128+64)};
			size = maxsize;
			while (size && (ptr[size-1] & B11000000) == B10000000) --size;
		}
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

	std::string lexemOutputString( const strus::SegmenterPosition& segmentpos, const char* segment, const strus::analyzer::PatternLexem& lx) const
	{
		const char* lexemname = m_globalContext->PatternLexerInstance()->getLexemName( lx.id());
		std::string content = encodeOutput( segment+lx.origpos().ofs(), lx.origsize(), 0/*no maxsize*/);
		return strus::string_format(
				"%d [%d] : %u %s %s",
				(int)lx.ordpos(), (unsigned int)(segmentpos+lx.origpos().ofs()), lx.id(), lexemname?lexemname:"?", content.c_str());
	}

	void processDocumentFile( const std::string& filename)
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
		strus::analyzer::DocumentClass documentClass = getDocumentClass( content);
		const strus::SegmenterInstanceInterface* segmenterInstance = getProcessSegmenterInstance( content, documentClass);

		const char* resultid;
		if (strus::stringStartsWith( filename, m_globalContext->fileprefix()))
		{
			char const* di = filename.c_str() + m_globalContext->fileprefix().size();
			while (*di == strus::dirSeparator()) ++di;
			resultid = di;
		}
		else
		{
			resultid = filename.c_str();
		}
		if (m_globalContext->fileContainsMultipleDocuments())
		{
			if (documentClass.mimeType() == "application/json")
			{
				std::vector<std::string> contentlist = strus::splitJsonDocumentList( documentClass.encoding(), content, g_errorhnd);
				if (g_errorhnd->hasError())
				{
					throw strus::runtime_error(_TXT("error splitting documents for %s: %s"), documentClass.mimeType().c_str(), g_errorhnd->fetchError());
				}
				std::vector<std::string>::const_iterator ci = contentlist.begin(), ce = contentlist.end();
				int cidx = 1;
				for (; ci != ce; ++ci,++cidx)
				{
					std::string subdocid;
					if (m_globalContext->docid_selectexpr().empty())
					{
						subdocid = strus::string_format( "%s:%d", resultid, cidx);
					}
					else
					{
						subdocid = getDocidFromDocument( *ci, documentClass);
					}
					processDocumentContent( segmenterInstance, documentClass, subdocid, *ci);
				}
			}
			else
			{
				throw strus::runtime_error(_TXT("multiple documents in one file not implemented for %s"), documentClass.mimeType().c_str());
			}
		}
		else
		{
			if (m_globalContext->docid_selectexpr().empty())
			{
				std::string docid = getDocidFromDocument( content, documentClass);
				processDocumentContent( segmenterInstance, documentClass, docid, content);
			}
			else
			{
				processDocumentContent( segmenterInstance, documentClass, resultid, content);
			}
		}
	}

	void processDocumentContent( const strus::SegmenterInstanceInterface* segmenterInstance, const strus::analyzer::DocumentClass& documentClass, const std::string& resultid, const std::string& content)
	{
		strus::local_ptr<strus::SegmenterContextInterface> segmenter( segmenterInstance->createContext( documentClass));
		strus::local_ptr<strus::PatternMatcherContextInterface> mt( m_globalContext->PatternMatcherInstance()->createContext());
		strus::local_ptr<strus::PatternLexerContextInterface> crctx( m_globalContext->PatternLexerInstance()->createContext());
		if (!segmenter.get() || !crctx.get() || !mt.get()) throw std::runtime_error(g_errorhnd->fetchError());

		*m_output << m_globalContext->resultMarker() << resultid << ":" << std::endl;
		if (DBG) DBG->open( "input", resultid);

		segmenter->putInput( content.c_str(), content.size(), true);
		int id;
		strus::SegmenterPosition segmentpos;
		const char* segment;
		std::size_t segmentsize;
		SegmenterPositionMap segmentposmap;
		std::string source;
		unsigned int ordposOffset = 0;
		strus::SegmenterPosition prev_segmentpos = (strus::SegmenterPosition)std::numeric_limits<std::size_t>::max();

		while (segmenter->getNext( id, segmentpos, segment, segmentsize))
		{
			if (prev_segmentpos != segmentpos)
			{
				segmentposmap[ segmentpos] = source.size();
				source.append( segment, segmentsize);
				source.push_back('\0');
				if (DBG)
				{
					std::string dbgseg  = encodeOutput(segment,segmentsize,200);
					DBG->event( "segment", "%d [%s] at %d", id, dbgseg.c_str(), (int)segmentpos);
				}
				std::vector<strus::analyzer::PatternLexem> crmatches = crctx->match( segment, segmentsize);
				if (crmatches.size() == 0 && g_errorhnd->hasError())
				{
					throw std::runtime_error( "failed to scan for tokens with char regex match automaton");
				}
				std::vector<strus::analyzer::PatternLexem>::iterator ti = crmatches.begin(), te = crmatches.end();
				for (; ti != te; ++ti)
				{
					ti->setOrigPosition( strus::analyzer::Position( segmentpos, ti->origpos().ofs()));
					ti->setOrdpos( ti->ordpos() + ordposOffset);
					if (m_globalContext->printTokens())
					{
						*m_output << lexemOutputString( segmentpos, segment, *ti) << std::endl;
					}
					if (DBG)
					{
						std::string eventstr = lexemOutputString( segmentpos, segment, *ti);
						DBG->event( "token", "%s", eventstr.c_str());
					}
					mt->putInput( *ti);
				}
				if (crmatches.size() > 0)
				{
					ordposOffset = crmatches.back().ordpos();
				}
			}
		}
		if (DBG)
		{
			DBG->close();
		}
		if (g_errorhnd->hasError())
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
			markupResults( *m_output, segmentposmap, results, documentClass, source, content, segmenterInstance);
		}
		*m_output << std::endl;
		if (g_errorhnd->hasError())
		{
			throw std::runtime_error("error printing results");
		}
	}

	void printFormatOutput( std::ostream& out, const char* value, const SegmenterPositionMap& segmentposmap, const std::string& src) const
	{
		strus::PatternResultFormatChunk chunk;
		char const* vi = value;
		while (strus::PatternResultFormatChunk::parseNext( chunk, vi, g_errorhnd))
		{
			if (chunk.value)
			{
				out << std::string( chunk.value, chunk.valuesize);
			}
			else
			{
				SegmenterPositionMap::const_iterator starti = segmentposmap.find( chunk.start_seg);
				SegmenterPositionMap::const_iterator endi = segmentposmap.find( chunk.end_seg);
				if (starti == segmentposmap.end() || endi == segmentposmap.end()) throw std::runtime_error(_TXT("corrupt result segment position"));

				std::size_t start_srcpos = starti->second + chunk.start_pos;
				std::size_t end_srcpos = endi->second + chunk.end_pos;
				std::size_t len = end_srcpos - start_srcpos;
				out << encodeOutput( src.c_str()+start_srcpos, len, 0/*maxsize*/);
			}
		}
	}

	void printResults( std::ostream& out, const SegmenterPositionMap& segmentposmap, std::vector<strus::analyzer::PatternMatcherResult>& results, const std::string& src)
	{
		std::vector<strus::analyzer::PatternMatcherResult>::const_iterator
			ri = results.begin(), re = results.end();
		const strus::PatternResultFormatMap* formatmap = m_globalContext->formatmap();
		if (formatmap)
		{
			for (; ri != re; ++ri)
			{
				std::string resdump = formatmap->map( *ri);
				printFormatOutput( out, resdump.c_str(), segmentposmap, src);
				out << "\n";
			}
		}
		else
		{
			throw std::runtime_error(_TXT("format string for result is empty"));
		}
	}

	strus::analyzer::TokenMarkup getTokenMarkup( const char* name, const char* value, const SegmenterPositionMap& segmentposmap, const std::string& src) const
	{
		std::vector<strus::analyzer::TokenMarkup::Attribute> attr;
		if (!m_globalContext->resultMarkupTag().first.empty())
		{
			name = m_globalContext->resultMarkupTag().first.c_str();
		}
		if (!m_globalContext->resultMarkupTag().second.empty() && value)
		{
			std::ostringstream out;
			printFormatOutput( out, value, segmentposmap, src);
			attr.push_back( strus::analyzer::TokenMarkup::Attribute( m_globalContext->resultMarkupTag().second, out.str()));
		}
		return strus::analyzer::TokenMarkup( name, attr);
	}

	void markupResults( std::ostream& out,
				const SegmenterPositionMap& segmentposmap, 
				const std::vector<strus::analyzer::PatternMatcherResult>& results,
				const strus::analyzer::DocumentClass& documentClass,
				const std::string& src,
				const std::string& origsrc,
				const strus::SegmenterInstanceInterface* segmenterInstance)
	{
		strus::local_ptr<strus::TokenMarkupContextInterface> markupContext( m_globalContext->createTokenMarkupContext( segmenterInstance));

		std::vector<strus::analyzer::PatternMatcherResult>::const_iterator
			ri = results.begin(), re = results.end();
		for (; ri != re; ++ri)
		{
			{
				std::map<std::string,int>::const_iterator mi = m_globalContext->markups().find( ri->name());
				if (mi != m_globalContext->markups().end())
				{
					markupContext->putMarkup(
						ri->origpos(), ri->origend(), getTokenMarkup( ri->name(), ri->value(), segmentposmap, src), mi->second);
				}
			}
			std::vector<strus::analyzer::PatternMatcherResultItem>::const_iterator
				ei = ri->items().begin(), ee = ri->items().end();

			for (; ei != ee; ++ei)
			{
				std::map<std::string,int>::const_iterator mi = m_globalContext->markups().find( ei->name());
				if (mi != m_globalContext->markups().end())
				{
					markupContext->putMarkup(
						ei->origpos(), ei->origend(), getTokenMarkup( ri->name(), ri->value(), segmentposmap, src), mi->second);
				}
			}
		}
		std::string content = markupContext->markupDocument( documentClass, origsrc);
		out << content << "\n";
	}

	void run()
	{
		DBG = m_debugTrace ? m_debugTrace->createTraceContext( STRUS_DBGTRACE_COMPONENT_NAME) : NULL;
		for (;;)
		{
			std::vector<std::string> filenames = m_globalContext->fetchFiles();
			if (filenames.empty()) break;
			std::vector<std::string>::const_iterator fi = filenames.begin(), fe = filenames.end();
			for (; fi != fe; ++fi)
			{
				if (DBG) DBG->open( "file", *fi);
				*m_outerr << strus::string_format( _TXT("thread %u processing file '%s'"), m_threadid, fi->c_str()) << std::endl;
				try
				{
					processDocumentFile( *fi);
					if (g_errorhnd->hasError())
					{
						*m_outerr << strus::string_format( _TXT("error thread %u file '%s': %s"), m_threadid, fi->c_str(), g_errorhnd->fetchError()) << std::endl;
					}
					if (DBG) DBG->close();
				}
				catch (const std::runtime_error& err)
				{
					if (g_errorhnd->hasError())
					{
						*m_outerr << strus::string_format( _TXT("error thread %u file '%s': %s, %s"), m_threadid, fi->c_str(), err.what(), g_errorhnd->fetchError()) << std::endl;
					}
					else
					{
						*m_outerr << strus::string_format( _TXT("error thread %u file '%s': %s"), m_threadid, fi->c_str(), err.what()) << std::endl;
					}
					if (DBG) DBG->close();
				}
				catch (const std::bad_alloc&)
				{
					*m_outerr << strus::string_format( _TXT( "out of memory thread %u processing document '%s'"), m_threadid, fi->c_str()) << std::endl;
					if (DBG) DBG->close();
				}
			}
		}
		if (g_errorhnd->hasError())
		{
			*m_outerr << strus::string_format( _TXT("error thread %u: %s"), m_threadid, g_errorhnd->fetchError()) << std::endl;
		}
		g_errorhnd->releaseContext();
	}

private:
	GlobalContext* m_globalContext;
	strus::DebugTraceInterface* m_debugTrace;
	strus::DebugTraceContextInterface* DBG;
	const strus::AnalyzerObjectBuilderInterface* m_objbuilder;
	const strus::SegmenterInterface* m_defaultSegmenter;
	strus::Reference<strus::SegmenterInstanceInterface> m_defaultDocidSegmenterInstance;
	strus::Reference<strus::SegmenterInstanceInterface> m_defaultProcessSegmenterInstance;
	typedef std::map<std::string,strus::Reference<strus::SegmenterInstanceInterface> > SegmenterMap;
	SegmenterMap m_docidSegmenterMap;
	SegmenterMap m_processSegmenterMap;
	unsigned int m_threadid;
	std::string m_outputfile;
	strus::shared_ptr<std::ofstream> m_outputfilestream;
	std::ostream* m_output;
	std::string m_outerrfile;
	strus::shared_ptr<std::ofstream> m_outerrfilestream;
	std::ostream* m_outerr;
};

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

static std::pair<std::string,std::string> parseResultMarkupTag( const std::string& source)
{
	char const* si = std::strchr( source.c_str(), ':');
	if (si)
	{
		return std::pair<std::string,std::string>( std::string( source.c_str(), si - source.c_str()), si+1);
	}
	else
	{
		return std::pair<std::string,std::string>( source, "");
	}
}

std::vector<std::string> split( const std::string& val, char splitchr)
{
	std::vector<std::string> rt;
	char const* si = val.c_str();
	char const* sn = std::strchr( si, splitchr);
	for (; sn; sn = si=sn+1,sn=std::strchr( si, splitchr))
	{
		rt.push_back( strus::string_conv::trim( std::string( si, sn-si)));
	}
	rt.push_back( strus::string_conv::trim( si));
	return rt;
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
	g_errorhnd = errorBuffer.get();

	try
	{
		bool printUsageAndExit = false;
		strus::ProgramOptions opt(
				errorBuffer.get(), argc, argv, 27,
				"h,help", "v,version", "license",
				"G,debug:", "g,segmenter:", "x,ext:", "C,contenttype:", "F,filelist",
				"e,expression:", "d,docid:", "K,tokens", "p,program:",
				"Z,marker:", "H,markup:", "Q,markuptag:",
				"X,lexer:", "Y,matcher:", "P,format:",
				"t,threads:", "f,fetch:", "o,output:", "O,outerr:",
				"M,moduledir:", "m,module:", "r,rpc:", "R,resourcedir:", "T,trace:");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}

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

		unsigned int nofThreads = 0;
		if (opt("threads"))
		{
			nofThreads = opt.asUint( "threads");
			if (!errorBuffer->setMaxNofThreads( nofThreads+1))
			{
				std::cerr << _TXT("failed to set threads of error buffer") << std::endl;
				return -1;
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
#if STRUS_PATTERN_STD_ENABLED
		if (!moduleLoader->loadModule( strus::Constants::standard_pattern_matcher_module()))
		{
			std::cerr << _TXT("failed to load module ") << "'" << strus::Constants::standard_pattern_matcher_module() << "': " << g_errorhnd->fetchError() << std::endl;
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
			std::cout << "-G|--debug <COMP>" << std::endl;
			std::cout << "    " << _TXT("Issue debug messages for component <COMP> to stderr") << std::endl;
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
			std::cout << "    " << _TXT("(JSONL for list of JSON documents in one file.") << std::endl;
			std::cout << "-F|--filelist" << std::endl;
			std::cout << "    " << _TXT("inputpath is a file containing the list of files to process.") << std::endl;
			std::cout << "-e|--expression <EXP>" << std::endl;
			std::cout << "    " << _TXT("Define a selection expression <EXP> for the content to process.") << std::endl;
			std::cout << "    " << _TXT("Process all content if nothing specified)") << std::endl;
			std::cout << "-d|--docid <EXP>" << std::endl;
			std::cout << "    " << _TXT("Define a selection expression <EXP> for item to use as docid.") << std::endl;
			std::cout << "    " << _TXT("By default the filename is taken as docid.") << std::endl;
			std::cout << "-H|--markup <NAME>{,<NAME>}" << std::endl;
			std::cout << "    " << _TXT("Output the content with markups of the rules or variables with name <NAME>") << std::endl;
			std::cout << "    " << _TXT("Rules defined first have lower priority and are ousted by later defnitions if") << std::endl;
			std::cout << "    " << _TXT("covered completely.") << std::endl;
			std::cout << "-Z|--marker <MRK>" << std::endl;
			std::cout << "    " << _TXT("Define a character sequence inserted before every result declaration") << std::endl;
			std::cout << "-Q|--markuptag <NAME>[:<ATTR>]" << std::endl;
			std::cout << "    " << _TXT("Output the content with markups of the rules or variables with name <NAME>") << std::endl;
			std::cout << "-X|--lexer <LX>" << std::endl;
			std::cout << "    " << _TXT("Use pattern lexer named <LX>") << std::endl;
			std::cout << "    " << _TXT("The default is 'std'") << std::endl;
			std::cout << "-Y|--matcher <PT>" << std::endl;
			std::cout << "    " << _TXT("Use pattern lexer named <PT>") << std::endl;
			std::cout << "    " << _TXT("The default is 'std'") << std::endl;
			std::cout << "-P|--format <PT>" << std::endl;
			std::cout << "    " << _TXT("Use format string <FT> for result output") << std::endl;
			std::cout << "    " << _TXT("The default result format is \"") << STRUS_PATTERN_DEFAULT_RESULT_FORMAT << "\"" << std::endl;
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
		std::string docidexpr;
		std::string matcher( strus::Constants::standard_pattern_matcher());
		std::string lexer( strus::Constants::standard_pattern_matcher());
		std::string programfile;
		bool printTokens = false;
		std::map<std::string,int> markups;
		std::pair<std::string,std::string> resultMarkupTag;
		std::string resultmarker;
		std::string resultFormat = STRUS_PATTERN_DEFAULT_RESULT_FORMAT;
		unsigned int nofFilesFetch = 1;
		std::string outputfile;
		std::string outerrfile;
		bool inputIsAListOfFiles = false;
		bool fileContainsMultipleDocuments = false;

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
			if (strus::caseInsensitiveEquals( contenttype, "jsonl"))
			{
				fileContainsMultipleDocuments = true;
				contenttype = "application/json";
			}
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
		if (opt( "docid"))
		{
			docidexpr = opt[ "docid"];
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
		if (opt("format"))
		{
			resultFormat = opt[ "format"];
		}
		if (opt( "markup"))
		{
			std::vector<std::string> list = opt.list( "markup");
			std::vector<std::string>::const_iterator li = list.begin(), le = list.end();
			for (unsigned int lidx=1; li != le; ++li,++lidx)
			{
				std::vector<std::string> items = split( *li, ',');
				std::vector<std::string>::const_iterator ii = items.begin(), ie = items.end();
				for (; ii != ie; ++ii)
				{
					markups.insert( std::pair<std::string, int>( *ii, lidx));
				}
			}
		}
		if (opt( "markuptag"))
		{
			resultMarkupTag = parseResultMarkupTag( opt[ "markuptag"]);
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
		if (errorBuffer->hasError())
		{
			throw std::runtime_error( _TXT("error in initialization"));
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
		if (opt( "program"))
		{
			programfile = getFileArg( opt[ "program"], moduleLoader.get());
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
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::AnalyzerObjectBuilderInterface* proxy = (*ti)->createProxy( analyzerBuilder.get());
			analyzerBuilder.release();
			analyzerBuilder.reset( proxy);
		}
		if (g_errorhnd->hasError())
		{
			throw std::runtime_error( _TXT("error in initialization"));
		}
		// Create objects:
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw std::runtime_error( _TXT("could not get text processor interface"));
		const strus::PatternMatcherInterface* pti = textproc->getPatternMatcher( matcher);
		if (!pti) throw std::runtime_error( _TXT("unknown pattern matcher"));
		const strus::PatternLexerInterface* lxi = textproc->getPatternLexer( lexer);
		if (!lxi) throw std::runtime_error( _TXT("unknown pattern lexer"));
		strus::local_ptr<strus::PatternMatcherInstanceInterface> ptinst( pti->createInstance());
		strus::local_ptr<strus::PatternLexerInstanceInterface> lxinst( lxi->createInstance());

		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty())
		{
			documentClass = strus::parse_DocumentClass( contenttype, errorBuffer.get());
			if (!documentClass.defined() && errorBuffer->hasError())
			{
				throw std::runtime_error( _TXT("failed to parse document class"));
			}
		}
		std::cerr << "load program ..." << std::endl;
		if (programfile.empty())
		{
			throw std::runtime_error( _TXT("no program for pattern matching defined (option --program|-p)"));
		}
		else if (!strus::load_PatternMatcher_programfile( textproc, lxinst.get(), ptinst.get(), programfile, errorBuffer.get()))
		{
			throw std::runtime_error( _TXT("failed to load program"));
		}
		if (!lxinst->compile() || !ptinst->compile())
		{
			throw std::runtime_error( g_errorhnd->fetchError());
		}
		if (expressions.empty())
		{
			expressions.push_back( "");
		}
		std::vector<std::string> inputfiles;
		std::string fileprefix;
		if (inputIsAListOfFiles)
		{
			int ec = strus::getParentPath( inputpath, fileprefix);
			if (ec)
			{
				throw strus::runtime_error(_TXT("error (%u) getting parent path of %s: %s"), ec, inputpath.c_str(), ::strerror(ec));
			}
			fileprefix += strus::dirSeparator();
			loadFileNamesFromFile( inputfiles, inputpath);
		}
		else
		{
			if (inputpath == "-")
			{
				
			}
			else if (strus::isDir( inputpath))
			{
				fileprefix = inputpath + strus::dirSeparator();
			}
			else
			{
				int ec = strus::getParentPath( inputpath, fileprefix);
				if (ec)
				{
					throw strus::runtime_error(_TXT("error (%u) getting parent path of %s: %s"), ec, inputpath.c_str(), ::strerror(ec));
				}
				fileprefix += strus::dirSeparator();
			}
			loadFileNames( inputfiles, inputpath, fileext);
		}
		GlobalContext globalContext(
				ptinst.get(), lxinst.get(), textproc, segmentername,
				expressions, docidexpr, fileprefix, inputfiles, nofFilesFetch, documentClass,
				markups, resultmarker, resultFormat, resultMarkupTag, printTokens, fileContainsMultipleDocuments);

		std::cerr << "start matching ..." << std::endl;
		if (nofThreads)
		{
			fprintf( stderr, _TXT("starting %u threads for evaluation ...\n"), nofThreads);

			std::vector<strus::Reference<ThreadContext> > processorList;
			processorList.reserve( nofThreads);
			for (unsigned int pi=0; pi<nofThreads; ++pi)
			{
				processorList.push_back( new ThreadContext( &globalContext, analyzerBuilder.get(), pi+1, outputfile, outerrfile));
			}
			{
				std::vector<strus::Reference<strus::thread> > threadGroup;
				for (unsigned int pi=0; pi<nofThreads; ++pi)
				{
					ThreadContext* tc = processorList[ pi].get();
					strus::Reference<strus::thread> th( new strus::thread( &ThreadContext::run, tc));
					threadGroup.push_back( th);
				}
				std::vector<strus::Reference<strus::thread> >::iterator gi = threadGroup.begin(), ge = threadGroup.end();
				for (; gi != ge; ++gi) (*gi)->join();
			}
		}
		else
		{
			ThreadContext ctx( &globalContext, analyzerBuilder.get(), 0, outputfile, outerrfile);
			ctx.run();
		}
		if (g_errorhnd->hasError())
		{
			throw std::runtime_error( _TXT("uncaught error in pattern matcher"));
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


