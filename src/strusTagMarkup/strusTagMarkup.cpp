/*
 * Copyright (c) 2018 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/error.hpp"
#include "strus/lib/markup_document_tags.hpp"
#include "strus/lib/module.hpp"
#include "strus/lib/filecrawler.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/lib/analyzer_prgload_std.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/numstring.hpp"
#include "strus/base/string_conv.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/thread.hpp"
#include "strus/base/stdint.h"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/versionUtilities.hpp"
#include "strus/reference.hpp"
#include "strus/documentClassDetectorInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/normalizerFunctionInterface.hpp"
#include "strus/normalizerFunctionInstanceInterface.hpp"
#include "private/fileCrawlerInterface.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include "private/parseFunctionDef.hpp"
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <memory>
#include <limits>

static strus::ErrorBufferInterface* g_errorBuffer = 0;	// error buffer
static bool g_verbose = false;

static bool isDigit( char ch)
{
	return ch >= '0' && ch <=  '9';
}

struct CountFormat
{
	std::string format;
	int start;

	CountFormat()
		:format(),start(0){}
	CountFormat( const CountFormat& o)
		:format(o.format),start(o.start){}
	explicit CountFormat( const std::string& parameter)
		:format(),start(0)
	{
		std::string prefix;
		char const* pi = parameter.c_str() + parameter.size();
		for (; pi != parameter.c_str() && *(pi-1) && isDigit(*(pi-1)); --pi){}

		prefix.append( parameter.c_str(), pi-parameter.c_str());
		int fmt0 = 0;

		for (; *pi == '0'; ++pi,++fmt0){}
		if (fmt0)
		{
			format = strus::string_format( "%s%%0%dd", prefix.c_str(), fmt0 + (int)std::strlen(pi));
		}
		else
		{
			format = strus::string_format( "%s%%d", prefix.c_str());
		}
		if (*pi)
		{
			start = strus::numstring_conv::touint( pi, std::strlen(pi), std::numeric_limits<int>::max());
		}
	}
};

static std::string startString( const std::string& val, char delim)
{
	const char* term = std::strchr( val.c_str(), delim);
	if (!term) return std::string();
	return std::string( val.c_str(), term - val.c_str());
}

static std::string followString( const std::string& val, char delim)
{
	const char* term = std::strchr( val.c_str(), delim);
	return term ? std::string(term+1) : std::string();
}

struct MapFormat
	:public CountFormat
{
	std::vector<strus::utils::FunctionDef> normalizers;

	MapFormat()
		:normalizers(){}
	MapFormat( const MapFormat& o)
		:CountFormat(o),normalizers(o.normalizers){}
	explicit MapFormat( const std::string& parameter)
		:CountFormat(startString(parameter,':')),normalizers(strus::utils::parseFunctionDefs( followString( parameter,':'), g_errorBuffer))
	{}
};


class TagAttributeMarkupCount:
	public strus::TagAttributeMarkupInterface
{
public:
	explicit TagAttributeMarkupCount( const std::string& attributename_, const std::string& parameter, int instanceidx, int nofinstances)
		:m_attributename(attributename_),m_formatstring(),m_current(0),m_incr(1)
	{
		CountFormat param( parameter);
		m_formatstring = param.format;
		m_current = param.start;

		if (nofinstances)
		{
			m_incr = nofinstances;
			m_current += instanceidx;
		}
	}
	virtual ~TagAttributeMarkupCount(){}

	virtual strus::analyzer::DocumentAttribute synthesizeAttribute( const std::string& tagname, const std::vector<strus::analyzer::DocumentAttribute>& attributes) const
	{
		strus::analyzer::DocumentAttribute rt( m_attributename, strus::string_format( m_formatstring.c_str(), m_current));
		m_current += m_incr;
		return rt;
	}
private:
	std::string m_attributename;
	std::string m_formatstring;
	mutable int m_current;
	int m_incr;
};

class TagAttributeMarkupMap:
	public strus::TagAttributeMarkupInterface
{
public:
	explicit TagAttributeMarkupMap( const strus::TextProcessorInterface* textproc, const std::string& attributename_, const std::string& parameter, int instanceidx, int nofinstances)
		:m_attributename(attributename_),m_formatstring(),m_map(),m_current(0)
	{
		MapFormat param( parameter);
		m_formatstring = param.format;
		m_current = param.start;
		std::vector<strus::utils::FunctionDef>::const_iterator ni = param.normalizers.begin(), ne = param.normalizers.end();
		for (; ni != ne; ++ni)
		{
			const strus::NormalizerFunctionInterface* normalizerType = textproc->getNormalizer( ni->first);
			if (!normalizerType) throw strus::runtime_error(_TXT("undefined normalizer '%s'"), ni->first.c_str());
			strus::Reference<strus::NormalizerFunctionInstanceInterface> normalizer( normalizerType->createInstance( ni->second, textproc));
			if (!normalizer.get()) throw strus::runtime_error(_TXT("failed to create normalizer '%s': %s"), ni->first.c_str(), g_errorBuffer->fetchError());
			m_normalizers.push_back( normalizer);
		}
	}
	virtual ~TagAttributeMarkupMap(){}

	virtual strus::analyzer::DocumentAttribute synthesizeAttribute( const std::string& tagname, const std::vector<strus::analyzer::DocumentAttribute>& attributes) const
	{
		std::string content( tagname);
		std::vector<strus::analyzer::DocumentAttribute>::const_iterator ai = attributes.begin(), ae = attributes.end();
		for (; ai != ae; ++ai)
		{
			if (ai->name() != m_attributename)
			{
				content.push_back( ' ');
				content.append( ai->name());
				content.push_back( '=');
				std::string val = ai->value();
				std::vector<strus::Reference<strus::NormalizerFunctionInstanceInterface> >::const_iterator ni = m_normalizers.begin(), ne = m_normalizers.end();
				for (; ni != ne; ++ni)
				{
					val = (*ni)->normalize( val.c_str(), val.size());
				}
				content.append( val);
			}
		}
		int idx;
		std::map<std::string,int>::const_iterator mi = m_map.find( content);
		if (mi == m_map.end())
		{
			idx = m_current++;
			m_map[ content] = idx;
		}
		else
		{
			idx = mi->second;
		}
		strus::analyzer::DocumentAttribute rt( m_attributename, strus::string_format( m_formatstring.c_str(), idx));
		return rt;
	}

private:
	std::string m_attributename;
	std::string m_formatstring;
	std::vector<strus::Reference<strus::NormalizerFunctionInstanceInterface> > m_normalizers;
	mutable std::map<std::string,int> m_map;
	mutable int m_current;
};


static void writeTagMarkup(
		const std::string& inputPath,
		const std::string& outputPath,
		strus::FileCrawlerInterface* crawler,
		const strus::DocumentClassDetectorInterface* dclassdetector,
		const strus::TextProcessorInterface* textproc,
		const strus::analyzer::DocumentClass& dclass,
		const std::vector<strus::DocumentTagMarkupDef>& markups,
		strus::ErrorBufferInterface* errorhnd)
{
	std::vector<std::string> ar = crawler->fetch();
	for (; !ar.empty(); ar = crawler->fetch())
	{
		std::vector<std::string>::const_iterator ai = ar.begin(), ae = ar.end();
		for (; ai != ae; ++ai)
		{
			if (!strus::stringStartsWith( *ai, inputPath)) throw strus::runtime_error(_TXT("internal: input path '%s' does not have prefix '%s"), ai->c_str(), inputPath.c_str());

			std::string content;
			std::string output;
			int ec = strus::readFile( *ai, content);
			if (ec) throw strus::runtime_error(_TXT("failed to read input file '%s': %s"), ai->c_str(), ::strerror(ec));

			strus::analyzer::DocumentClass documentClass;
			if (dclass.defined())
			{
				output = strus::markupDocumentTags( dclass, content, markups, textproc, errorhnd);
			}
			else
			{
				if (!dclassdetector->detect( documentClass, content.c_str(), content.size(), true/*is complete*/))
				{
					const char* errormsg = g_errorBuffer->fetchError();
					if (!errormsg) errormsg = "unsupported content type";
					throw strus::runtime_error(_TXT("failed to detect document class of file '%s': %s"), ai->c_str(), errormsg);
				}
				if (documentClass.mimeType() != "application/xml")
				{
					throw strus::runtime_error(_TXT("failed to process document of type '%s', tag markup not implemented for this document type"), documentClass.mimeType().c_str());
				}
				output = strus::markupDocumentTags( documentClass, content, markups, textproc, errorhnd);

			}
			if (outputPath.empty())
			{
				std::string ext;
				ec = strus::getFileExtension( inputPath, ext);
				if (ec) throw strus::runtime_error(_TXT("failed to get extension of input file '%s': %s"), ai->c_str(), ::strerror(ec));
				std::string outputFile = *ai;
				outputFile.resize( outputFile.size() - ext.size());
				outputFile.append( ".tag");
				outputFile.append( ext);
				ec = strus::writeFile( outputFile, output);
			}
			else if (outputPath == inputPath)
			{
				ec = strus::writeFile( *ai, output);
			}
			else if (outputPath == "-")
			{
				std::cout << output << std::endl;
			}
			else
			{
				std::string outputFile = strus::joinFilePath( outputPath, ai->c_str() + inputPath.size());
				std::string outputFileDir;
				ec = strus::getParentPath( outputFile, outputFileDir);
				if (ec) throw strus::runtime_error(_TXT("failed to get parent path of output '%s': %s"), outputFile.c_str(), ::strerror(ec));
				ec = strus::mkdirp( outputFileDir);
				if (ec) throw strus::runtime_error(_TXT("failed to create (mkdir -p) parent path of output '%s': %s"), outputFileDir.c_str(), ::strerror(ec));
				ec = strus::writeFile( outputFile, output);
				if (ec) throw strus::runtime_error(_TXT("failed to write output file '%s': %s"), outputFile.c_str(), ::strerror(ec));
			}
		}
	}
}

class WorkerBase
{
public:
	virtual ~WorkerBase(){}
	virtual void run()=0;
};

class TagMarkupWorker
	:public WorkerBase
{
public:
	virtual ~TagMarkupWorker(){}
	TagMarkupWorker(
			int threadid_,
			strus::FileCrawlerInterface* crawler_,
			const strus::DocumentClassDetectorInterface* dclassdetector_,
			const strus::TextProcessorInterface* textproc_,
			const strus::analyzer::DocumentClass& documentClass_,
			const std::vector<strus::DocumentTagMarkupDef>& markups_,
			const std::string& inputPath_,
			const std::string& outputPath_,
			strus::ErrorBufferInterface* errorhnd_)
		:m_errorhnd(errorhnd_)
		,m_threadid(threadid_),m_inputPath(inputPath_),m_outputPath(outputPath_),m_crawler(crawler_)
		,m_dclassdetector(dclassdetector_),m_textproc(textproc_),m_documentClass(documentClass_),m_markups(markups_)
	{
		if (m_documentClass.defined() && m_documentClass.mimeType() != "application/xml")
		{
			throw strus::runtime_error(_TXT("failed to process document of type '%s', tag markup not implemented for this document type"), m_documentClass.mimeType().c_str());
		}
	}

	virtual void run()
	{
		try
		{
			writeTagMarkup( m_inputPath, m_outputPath, m_crawler, m_dclassdetector, m_textproc, m_documentClass, m_markups, m_errorhnd);
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
	strus::ErrorBufferInterface* m_errorhnd;
	int m_threadid;
	std::string m_inputPath;
	std::string m_outputPath;
	strus::FileCrawlerInterface* m_crawler;
	const strus::DocumentClassDetectorInterface* m_dclassdetector;
	const strus::TextProcessorInterface* m_textproc;
	strus::analyzer::DocumentClass m_documentClass;
	std::vector<strus::DocumentTagMarkupDef> m_markups;
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
				errorBuffer.get(), argc, argv, 18,
				"h,help", "v,version", "V,verbose",
				"license", "G,debug:", "m,module:",
				"M,moduledir:", "r,rpc:", "T,trace:", "R,resourcedir:",
				"C,contenttype:", "x,extension:",
				"e,expression:", "a,attribute:", "k,markup:", "P,parameter:",
				"t,threads:", "f,fetch:");
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

		strus::local_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
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
		if (opt( "verbose"))
		{
			g_verbose = true;
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
			if (opt.nofargs() < 1)
			{
				std::cerr << _TXT("error too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusTagMarkup [options] <docpath> [<outpath>]" << std::endl;
			std::cout << "<docpath> = " << _TXT("path of input file/directory") << std::endl;
			std::cout << "<outpath> = " << _TXT("path of output") << std::endl;
			std::cout << "            " << _TXT("if equal to \"-\", then the outputs are written to stdout") << std::endl;
			std::cout << "            " << _TXT("if equal to <docpath>, then the input files are replaced") << std::endl;
			std::cout << "            " << _TXT("if empty, then the output files are written where the") << std::endl;
			std::cout << "            " << _TXT("  input files are with a filename having the extension") << std::endl;
			std::cout << "            " << _TXT("  .tag.xml instead of .xml") << std::endl;
			std::cout << _TXT("description: Adds an attribute to the tags selected in the input files.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-V,--verbose" << std::endl;
			std::cout << "    " << _TXT("Verbose output of actions to stderr") << std::endl;
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
			std::cout << "-C|--contenttype <CT>" << std::endl;
			std::cout << "    " << _TXT("forced definition of the document class of the document processed.") << std::endl;
			std::cout << "-x|--extension <EXT>" << std::endl;
			std::cout << "    " << _TXT("extension of the input files processed.") << std::endl;
			std::cout << "-e|--expression <XPATH>" << std::endl;
			std::cout << "    " << _TXT("Use <XPATH> as expression (abbreviated syntax of XPath)") << std::endl;
			std::cout << "    " << _TXT("to select the tags to add attributes to.") << std::endl;
			std::cout << "    " << _TXT("This option is mandatory.") << std::endl;
			std::cout << "-a|--attribute <NAME>" << std::endl;
			std::cout << "    " << _TXT("Use <NAME> as the name of attribute added to the selected tags.") << std::endl;
			std::cout << "    " << _TXT("If not specified, 'id'' is used.") << std::endl;
			std::cout << "-k|--markup <NAME>" << std::endl;
			std::cout << "    " << _TXT("Specify the class <NAME> for markup.") << std::endl;
			std::cout << "    " << _TXT("If not specified, 'count' is used.") << std::endl;
			std::cout << "    " << _TXT("Possible values:") << std::endl;
			std::cout << "    " << _TXT("  - count   :count the tags and add a unique attribute with the counter as value") << std::endl;
			std::cout << "-P|--parameter <VAL>" << std::endl;
			std::cout << "    " << _TXT("The string <VAL> is used as argument to instantiate the markup") << std::endl;
			std::cout << "    " << _TXT("specified with option -k|--markup.") << std::endl;
			std::cout << "    " << _TXT("The interpretation of the parameter depends on the markup class.") << std::endl;
			std::cout << "-t|--threads <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of threads to use") << std::endl;
			std::cout << "-f|--fetch <N>" << std::endl;
			std::cout << "    " << _TXT("Set <N> as number of files fetched in each iteration") << std::endl;
			std::cout << "    " << _TXT("Default is 100") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string contenttype;
		std::string fileext;
		std::vector<std::string> expressions;
		std::string attribute = "id";
		std::string markup = "count";
		std::string parameter;
		enum {MaxNofThreads=1024};
		int threads = opt( "threads") ? opt.asUint( "threads") : 0;
		if (threads > MaxNofThreads) threads = MaxNofThreads;
		int fetchSize = opt( "fetch") ? opt.asUint( "fetch") : 100;
		if (!fetchSize) fetchSize = 1;

		if (opt( "contenttype"))
		{
			contenttype = opt[ "contenttype"];
		}
		if (opt( "extension"))
		{
			fileext = opt[ "extension"];
			if (!fileext.empty() && fileext[0] != '.') fileext = std::string(".") + fileext;
		}
		if (opt( "expression"))
		{
			expressions = opt.list( "expression");
		}
		if (opt( "attribute"))
		{
			attribute = opt[ "attribute"];
		}
		if (opt( "markup"))
		{
			markup = opt[ "markup"];
		}
		if (opt( "parameter"))
		{
			parameter = opt[ "parameter"];
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
		std::string outputpath;
		if (opt.nofargs() > 1)
		{
			outputpath = opt[1];
			if (!outputpath.empty() && outputpath != "-")
			{
				ec = strus::resolveUpdirReferences( outputpath);
				if (ec) throw strus::runtime_error( _TXT("failed to resolve updir references of path '%s': %s"), outputpath.c_str(), ::strerror(ec));
			}
		}
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

		// Get the document class if specified:
		strus::analyzer::DocumentClass documentClass;
		if (!contenttype.empty())
		{
			documentClass = strus::parse_DocumentClass( contenttype, errorBuffer.get());
			if (!documentClass.defined() && errorBuffer->hasError())
			{
				throw std::runtime_error( _TXT("failed to parse document class"));
			}
		}

		// Initialize the file crawler:
		strus::local_ptr<strus::FileCrawlerInterface> fileCrawler( strus::createFileCrawlerInterface( docpath, fetchSize, fileext, errorBuffer.get()));
		if (!fileCrawler.get()) throw std::runtime_error( errorBuffer->fetchError());
		strus::local_ptr<strus::DocumentClassDetectorInterface> documentClassDetector( analyzerBuilder->createDocumentClassDetector());
		if (!documentClassDetector.get()) throw std::runtime_error( errorBuffer->fetchError());

		// Define entity expression segmenter if selector expressions for entities are defined:
		if (expressions.empty())
		{
			throw std::runtime_error( _TXT("not expressions specified, option -e|--expression is mandatory"));
		}

		int nofInstances = threads ? threads : 1;
		int instanceIdx = 0;
		typedef std::vector<strus::DocumentTagMarkupDef> DocumentTagMarkupDefArray;
		std::vector<DocumentTagMarkupDefArray> markupDefInstanceAr;
		for (; instanceIdx < nofInstances; ++instanceIdx)
		{
			strus::Reference<strus::TagAttributeMarkupInterface> hnd;
			if (strus::caseInsensitiveEquals( markup, "count"))
			{
				hnd.reset( new TagAttributeMarkupCount( attribute, parameter, instanceIdx, nofInstances));
			}
			else if (strus::caseInsensitiveEquals( markup, "map"))
			{
				hnd.reset( new TagAttributeMarkupMap( textproc, attribute, parameter, instanceIdx, nofInstances));
			}
			else
			{
				throw strus::runtime_error( _TXT("unknown markup %s"), markup.c_str());
			}
			markupDefInstanceAr.push_back( DocumentTagMarkupDefArray());

			std::vector<std::string>::const_iterator ei = expressions.begin(), ee = expressions.end();
			for (int eidx=1; ei != ee; ++ei,++eidx)
			{
				markupDefInstanceAr.back().push_back( strus::DocumentTagMarkupDef( hnd, *ei));
			}
		}

		// Build the worker data:
		typedef strus::Reference<WorkerBase> WorkerBaseReference;
		std::vector<WorkerBaseReference> workers;
		for (instanceIdx=0; instanceIdx < nofInstances; ++instanceIdx)
		{
			workers.push_back(
				new TagMarkupWorker(
					(instanceIdx+1)/*threadid*/, fileCrawler.get(), documentClassDetector.get(),
					textproc, documentClass, markupDefInstanceAr[ instanceIdx], docpath, outputpath, g_errorBuffer));
		}
		if (g_errorBuffer->hasError())
		{
			throw strus::runtime_error( _TXT("error in instantiation of workers: %s"), g_errorBuffer->fetchError());
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
		if (g_errorBuffer->hasError())
		{
			throw strus::runtime_error( _TXT("error in tag markup: %s"), g_errorBuffer->fetchError());
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



