/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/reference.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/index.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/tokenizerFunctionInterface.hpp"
#include "strus/normalizerFunctionInterface.hpp"
#include "strus/aggregatorFunctionInterface.hpp"
#include "strus/patternLexerInterface.hpp"
#include "strus/patternMatcherInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/programOptions.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/string_conv.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionStorage.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "strus/functionDescription.hpp"
#include "private/versionUtilities.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <stdexcept>

static bool g_html_output = false;

static void print_header( std::ostream& out)
{
	if (g_html_output)
	{
		out << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 2.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
			<< "<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\" xml:lang=\"en\">\n"
			<< "<head>\n"
			<< "<link rel=\"icon\" type=\"image/ico\" href=\"images/strus.ico\" />\n"
			<< "<meta http-equiv=\"content-type\" content=\"text/html; charset=utf-8\" />\n"
			<< "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
			<< "<meta name=\"description\" content=\"Documentation of the built-in functions of strus, a collection of C++ libraries for building a full-text search engine.\" />\n"
			<< "<meta name=\"keywords\" content=\"fulltext search engine C++\" />\n"
			<< "<meta name=\"author\" content=\"Patrick Frey &lt;patrickpfrey (a) yahoo (dt) com&gt;\" />\n"
			<< "<link rel=\"stylesheet\" type=\"text/css\" href=\"text-profile.css\" title=\"Text Profile\" media=\"all\" />\n"
			<< "<title>Strus built-in functions</title>\n"
			<< "</head>\n"
			<< "<body>\n"
			<< "<div id=\"wrap\">\n"
			<< "<div id=\"content\">\n"
			<< "<p><font color=green><i>This document is the output of </i><b>strusHelp --html -m analyzer_pattern -m storage_vector_std</b></font></p>\n"
			<< "<h1>Strus built-in functions</h1>\n";
	}
}

static void print_trailer( std::ostream& out)
{
	if (g_html_output)
	{
		out << "</div>\n</div>\n</body>\n</html>\n" << std::endl;
		
	}
}

static void print_title( std::ostream& out, const std::string& title, const std::string& description)
{
	if (g_html_output)
	{
		out << "<h2>" << title << "</h2>" << std::endl;
		out << "<p>" << description << "</p>" << std::endl;
	}
	else
	{
		out << std::endl;
		out << title << std::endl << std::string( title.size(), '=') << std::endl;
		out << "  " << description << ":" << std::endl;
	}
}

static void print_subtitle( std::ostream& out, const std::string& subtitle, const std::string& description)
{
	if (g_html_output)
	{
		out << "<h3>" << subtitle << "</h3>" << std::endl;
		out << "<p>" << description << "</p>" << std::endl;
	}
	else
	{
		out << std::endl;
		out << subtitle << std::endl << std::string( subtitle.size(), '-') << std::endl;
		out << "  " << description << ":" << std::endl;
	}
}

static void print_startlist( std::ostream& out, const char* listdescr)
{
	if (g_html_output)
	{
		if (listdescr)
		{
			out << "<p>" << listdescr << "<p>" << std::endl;
		}
		out << "<ul>" << std::endl;
	}
	else
	{
		if (listdescr) out << listdescr << ":" << std::endl;
	}
}

static void print_endlist( std::ostream& out)
{
	if (g_html_output)
	{
		out << "</ul>" << std::endl;
	}
}

static void print_function_description( std::ostream& out, const std::string& name, const std::string& descr)
{
	if (g_html_output)
	{
		out << "<li><b>" << name << "</b>&nbsp;&nbsp;&nbsp;&nbsp;" << descr << "</li>" << std::endl;
	}
	else
	{
		out << "[" << name << "]" << std::endl << "  " << descr << std::endl;
	}
}

static void print_parameter_description( std::ostream& out, const std::string& name, const std::string& type, const std::string& domain, const std::string& text)
{
	if (g_html_output)
	{
		out << "<li><b>" << name << "</b>&nbsp;&nbsp;[" << type << "]&nbsp;&nbsp;";
		if (!domain.empty())
		{
			out << "(" << domain << ")&nbsp;&nbsp;";
		}
		out << text << "</li>" << std::endl;
	}
	else
	{
		out << "\t" << name << " [" << type << "] ";
		if (!domain.empty())
		{
			out << "(" << domain << ") ";
		}
		out << text << std::endl;
	}
}

static void printTextProcessorDescription( std::ostream& out, const strus::TextProcessorInterface* textproc, strus::TextProcessorInterface::FunctionType type, const char* name)
{
	const char* label = "";
	const char* label_descr = "";
	switch (type)
	{
		case strus::TextProcessorInterface::Segmenter: label = _TXT("Segmenter"); label_descr = _TXT("list of segmenters"); break;
		case strus::TextProcessorInterface::TokenizerFunction: label = _TXT("Tokenizer"); label_descr = _TXT("list of functions for tokenization"); break;
		case strus::TextProcessorInterface::NormalizerFunction: label = _TXT("Normalizer"); label_descr = _TXT("list of functions for token normalization"); break;
		case strus::TextProcessorInterface::AggregatorFunction: label = _TXT("Aggregator"); label_descr = _TXT("list of functions for aggregating values after document analysis, e.g. counting of words"); break;
		case strus::TextProcessorInterface::PatternLexer: label = _TXT("PatternLexer"); label_descr = _TXT("list of lexers for pattern matching"); break;
		case strus::TextProcessorInterface::PatternMatcher: label = _TXT("PatternMatcher"); label_descr = _TXT("list of modules for pattern matching"); break;
	};
	std::vector<std::string> funcs;
	std::vector<std::string>::const_iterator fi,fe;
	if (name)
	{
		funcs.push_back( name);
	}
	else
	{
		funcs = textproc->getFunctionList( type);
	}
	if (!funcs.empty())
	{
		print_subtitle( out, label, label_descr);
		print_startlist( out, 0);
	}
	fi = funcs.begin(), fe = funcs.end();
	for (; fi != fe; ++fi)
	{
		const char* descr = 0;
		switch (type)
		{
			case strus::TextProcessorInterface::Segmenter:
			{
				const strus::SegmenterInterface* func = textproc->getSegmenterByName( *fi);
				if (!func) break;
				descr = func->getDescription();
				break;
			}
			case strus::TextProcessorInterface::TokenizerFunction:
			{
				const strus::TokenizerFunctionInterface* func = textproc->getTokenizer( *fi);
				if (!func) break;
				descr = func->getDescription();
				break;
			}
			case strus::TextProcessorInterface::NormalizerFunction:
			{
				const strus::NormalizerFunctionInterface* func = textproc->getNormalizer( *fi);
				if (!func) break;
				descr = func->getDescription();
				break;
			}
			case strus::TextProcessorInterface::AggregatorFunction:
			{
				const strus::AggregatorFunctionInterface* func = textproc->getAggregator( *fi);
				if (!func) break;
				descr = func->getDescription();
				break;
			}
			case strus::TextProcessorInterface::PatternLexer:
			{
				const strus::PatternLexerInterface* func = textproc->getPatternLexer( *fi);
				if (!func) break;
				descr = func->getDescription();
				break;
			}
			case strus::TextProcessorInterface::PatternMatcher:
			{
				const strus::PatternMatcherInterface* func = textproc->getPatternMatcher( *fi);
				if (!func) break;
				descr = func->getDescription();
				break;
			}
		};
		if (descr && *descr)
		{
			print_function_description( out, *fi, descr);
		}
	}
	if (!funcs.empty())
	{
		print_endlist( out);
	}
}

static const char* functionDescriptionParameterTypeName( strus::FunctionDescription::Parameter::Type type_)
{
	static const char* ar[] = {"Feature","Attribute","Metadata","Numeric","String"};
	return ar[ (unsigned int)type_];
}

static void printFunctionDescription( std::ostream& out, const std::string& label, const std::string& name, const strus::FunctionDescription& descr)
{
	typedef strus::FunctionDescription::Parameter Param;
	std::ostringstream descrout;

	descrout << descr.text() << std::endl;
	print_startlist( descrout, _TXT("List of parameters"));
	std::vector<Param>::const_iterator pi = descr.parameter().begin(), pe = descr.parameter().end();
	for (; pi != pe; ++pi)
	{
		print_parameter_description( descrout, pi->name(), functionDescriptionParameterTypeName( pi->type()), pi->domain(), pi->text());
	}
	print_endlist( descrout);
	print_function_description( out, name, descrout.str());
}

static void printQueryProcessorDescription( std::ostream& out, const strus::QueryProcessorInterface* queryproc, strus::QueryProcessorInterface::FunctionType type, const char* name)
{
	const char* label = "";
	const char* label_descr = "";
	switch (type)
	{
		case strus::QueryProcessorInterface::PostingJoinOperator: label = _TXT("Posting join operator"); label_descr = _TXT("List of posting join operators"); break;
		case strus::QueryProcessorInterface::WeightingFunction: label = _TXT("Weighting function"); label_descr = _TXT("List of query evaluation weighting functions"); break;
		case strus::QueryProcessorInterface::SummarizerFunction: label = _TXT("Summarizer"); label_descr = _TXT("List of summarization functions for the presentation of a query evaluation result"); break;
		case strus::QueryProcessorInterface::ScalarFunctionParser: label = _TXT("Scalar function parser"); label_descr = _TXT("List of scalar function parsers"); break;
	}
	std::vector<std::string> funcs;
	std::vector<std::string>::const_iterator fi,fe;
	if (name)
	{
		funcs.push_back( name);
	}
	else
	{
		funcs = queryproc->getFunctionList( type);
	}
	if (!funcs.empty())
	{
		print_subtitle( out, label, label_descr);
		print_startlist( out, 0);
	}
	fi = funcs.begin(), fe = funcs.end();
	for (; fi != fe; ++fi)
	{
		switch (type)
		{
			case strus::QueryProcessorInterface::PostingJoinOperator:
			{
				const strus::PostingJoinOperatorInterface* opr = queryproc->getPostingJoinOperator( *fi);
				print_function_description( out, *fi, opr->getDescription().text());
				break;
			}
			case strus::QueryProcessorInterface::WeightingFunction: 
			{
				const strus::WeightingFunctionInterface* func = queryproc->getWeightingFunction( *fi);
				if (func) printFunctionDescription( out, label, *fi, func->getDescription());
				break;
			}
			case strus::QueryProcessorInterface::SummarizerFunction:
			{
				const strus::SummarizerFunctionInterface* func = queryproc->getSummarizerFunction( *fi);
				if (func) printFunctionDescription( out, label, *fi, func->getDescription());
				break;
			}
			case strus::QueryProcessorInterface::ScalarFunctionParser:
			{
				const strus::ScalarFunctionParserInterface* func = queryproc->getScalarFunctionParser( *fi);
				if (func)
				{
					const char* descr = func->getDescription();
					if (descr && *descr)
					{
						print_function_description( out, *fi, descr);
					}
				}
				break;
			}
		};
	}
	if (!funcs.empty())
	{
		print_endlist( out);
	}
}

int main( int argc_, const char* argv_[])
{
	int rt = 0;
	strus::local_ptr<strus::ErrorBufferInterface> errorBuffer( strus::createErrorBuffer_standard( 0, 2));
	if (!errorBuffer.get())
	{
		std::cerr << _TXT("failed to create error buffer") << std::endl;
		return -1;
	}
	try
	{
		bool printUsageAndExit = false;
		strus::ProgramOptions opt(
				errorBuffer.get(), argc_, argv_, 9,
				"h,help", "v,version", "license",
				"m,module:", "M,moduledir:", "R,resourcedir:", "r,rpc:",
				"T,trace:", "H,html");
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("failed to parse program arguments"));
		}

		if (opt( "help")) printUsageAndExit = true;
		strus::local_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error( "%s", _TXT("failed to create module loader"));
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error(_TXT("specified mutual exclusive options %s and %s"), "--moduledir" ,"--rpc");
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
			std::cout << _TXT("Strus module version ") << STRUS_MODULE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus rpc version ") << STRUS_RPC_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus trace version ") << STRUS_TRACE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
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
				std::cerr << _TXT( "too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
		}
		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusHelp [options] [ <what> <name> ]" << std::endl;
			std::cout << "<what> = " << _TXT("specifies what type of item to retrieve (default all):") << std::endl;
			std::cout << "         " << "segmenter     : " << _TXT("Get segmenter function description") << std::endl;
			std::cout << "         " << "tokenizer     : " << _TXT("Get tokenizer function description") << std::endl;
			std::cout << "         " << "normalizer    : " << _TXT("Get normalizer function description") << std::endl;
			std::cout << "         " << "aggregator    : " << _TXT("Get aggregator function description") << std::endl;
			std::cout << "         " << "join          : " << _TXT("Get iterator join operator description") << std::endl;
			std::cout << "         " << "weighting     : " << _TXT("Get weighting function description") << std::endl;
			std::cout << "         " << "summarizer    : " << _TXT("Get summarizer function description") << std::endl;
			std::cout << "<item> = " << _TXT("name of the item to retrieve (default all)") << std::endl;
			std::cout << _TXT("description: Get the description of a function.") << std::endl;
			std::cout << _TXT("options:") << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "    " << _TXT("Print this usage and do nothing else") << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    " << _TXT("Print the program version and do nothing else") << std::endl;
			std::cout << "-H|--html" << std::endl;
			std::cout << "    " << _TXT("Print output as html") << std::endl;
			std::cout << "--license" << std::endl;
			std::cout << "    " << _TXT("Print 3rd party licences requiring reference") << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			std::cout << "    " << strus::string_format( _TXT("Example: %s"), "-T \"log=dump;file=stdout\"") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string what;
		std::string item;
		if (opt.nofargs() > 0)
		{
			what = opt[0];
			if (what.empty()) throw strus::runtime_error( "%s", _TXT("illegal empty item type as program argument"));
		}
		if (opt.nofargs() > 1)
		{
			item = opt[1];
			if (item.empty()) throw strus::runtime_error( "%s", _TXT("illegal empty item value as program argument"));
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
		// Create root objects:
		strus::local_ptr<strus::RpcClientMessagingInterface> messaging;
		strus::local_ptr<strus::RpcClientInterface> rpcClient;
		strus::local_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;
		strus::local_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error( "%s", _TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw strus::runtime_error( "%s", _TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error( "%s", _TXT("failed to create rpc analyzer object builder"));
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error( "%s", _TXT("failed to create rpc storage object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error( "%s", _TXT("failed to create analyzer object builder"));
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error( "%s", _TXT("failed to create storage object builder"));
		}
		if (opt("html"))
		{
			g_html_output = true;
		}
		// Create proxy objects if tracing enabled:
		std::vector<TraceReference>::const_iterator ti = trace.begin(), te = trace.end();
		for (; ti != te; ++ti)
		{
			strus::AnalyzerObjectBuilderInterface* aproxy = (*ti)->createProxy( analyzerBuilder.get());
			analyzerBuilder.release();
			analyzerBuilder.reset( aproxy);
			strus::StorageObjectBuilderInterface* sproxy = (*ti)->createProxy( storageBuilder.get());
			storageBuilder.release();
			storageBuilder.reset( sproxy);
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error( "%s", _TXT("error in initialization"));
		}

		// Print help:
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error( "%s", _TXT("failed to get text processor"));

		const strus::QueryProcessorInterface* queryproc = storageBuilder->getQueryProcessor();
		if (!queryproc) throw strus::runtime_error( "%s", _TXT("failed to get query processor"));

		print_header( std::cout);
		if (what.empty())
		{
			print_title( std::cout, _TXT("Query Processor"), _TXT("List of functions and operators predefined in the storage query processor"));
			printQueryProcessorDescription( std::cout, queryproc, strus::QueryProcessorInterface::PostingJoinOperator, 0);
			printQueryProcessorDescription( std::cout, queryproc, strus::QueryProcessorInterface::WeightingFunction, 0);
			printQueryProcessorDescription( std::cout, queryproc, strus::QueryProcessorInterface::SummarizerFunction, 0);

			print_title( std::cout, _TXT("Analyzer"), _TXT("List of functions and operators predefined in the analyzer text processor"));
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::Segmenter, 0);
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::TokenizerFunction, 0);
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::NormalizerFunction, 0);
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::AggregatorFunction, 0);
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::PatternLexer, 0);
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::PatternMatcher, 0);
		}
		else if (strus::caseInsensitiveEquals( what, "segmenter"))
		{
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::Segmenter, item.empty()?0:item.c_str());
		}
		else if (strus::caseInsensitiveEquals( what, "tokenizer"))
		{
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::TokenizerFunction, item.empty()?0:item.c_str());
		}
		else if (strus::caseInsensitiveEquals( what, "normalizer"))
		{
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::NormalizerFunction, item.empty()?0:item.c_str());
		}
		else if (strus::caseInsensitiveEquals( what, "aggregator"))
		{
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::AggregatorFunction, item.empty()?0:item.c_str());
		}
		else if (strus::caseInsensitiveEquals( what, "patternlexer"))
		{
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::PatternLexer, item.empty()?0:item.c_str());
		}
		else if (strus::caseInsensitiveEquals( what, "patternmatcher"))
		{
			printTextProcessorDescription( std::cout, textproc, strus::TextProcessorInterface::PatternMatcher, item.empty()?0:item.c_str());
		}
		else if (strus::caseInsensitiveEquals( what, "join"))
		{
			printQueryProcessorDescription( std::cout, queryproc, strus::QueryProcessorInterface::PostingJoinOperator, item.empty()?0:item.c_str());
		}
		else if (strus::caseInsensitiveEquals( what, "weighting"))
		{
			printQueryProcessorDescription( std::cout, queryproc, strus::QueryProcessorInterface::WeightingFunction, item.empty()?0:item.c_str());
		}
		else if (strus::caseInsensitiveEquals( what, "summarizer"))
		{
			printQueryProcessorDescription( std::cout, queryproc, strus::QueryProcessorInterface::SummarizerFunction, item.empty()?0:item.c_str());
		}
		else
		{
			throw strus::runtime_error(_TXT("unknown item type '%s'"), what.c_str());
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error( "%s", errorBuffer->fetchError());
		}
		print_trailer( std::cout);
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


