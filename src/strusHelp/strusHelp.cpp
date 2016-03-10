/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2015 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 3 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
*/
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/index.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/tokenizerFunctionInterface.hpp"
#include "strus/normalizerFunctionInterface.hpp"
#include "strus/aggregatorFunctionInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionStorage.hpp"
#include "private/version.hpp"
#include "private/programOptions.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include <iostream>
#include <sstream>
#include <memory>
#include <cstring>
#include <stdexcept>

static void printTextProcessorDescription( const strus::TextProcessorInterface* textproc, strus::TextProcessorInterface::FunctionType type, const char* name)
{
	const char* label = "";
	switch (type)
	{
		case strus::TextProcessorInterface::TokenizerFunction: label = _TXT("Tokenizer "); break;
		case strus::TextProcessorInterface::NormalizerFunction: label = _TXT("Normalizer "); break;
		case strus::TextProcessorInterface::AggregatorFunction: label = _TXT("Aggregator "); break;
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
	fi = funcs.begin(), fe = funcs.end();
	for (; fi != fe; ++fi)
	{
		std::cout << label << "'" << *fi << "' :" << std::endl;
		const char* descr = 0;
		switch (type)
		{
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
		};
		if (descr && *descr)
		{
			std::cout << "* " << descr << std::endl << std::endl;
		}
	}
}

template <class Description>
static void printFunctionDescription( std::ostream& out, const Description& descr)
{
	typedef typename Description::Param Param;
	out << "* " << descr.text() << std::endl;
	typename std::vector<Param>::const_iterator pi = descr.param().begin(), pe = descr.param().end();
	for (; pi != pe; ++pi)
	{
		out << "\t" << pi->name() << " [" << pi->typeName();
		if (!pi->domain().empty())
		{
			out << " (" << pi->domain() << ")";
		}
		out << "] " << pi->text() << std::endl;
	}
	out << std::endl;
}

static void printQueryProcessorDescription( const strus::QueryProcessorInterface* queryproc, strus::QueryProcessorInterface::FunctionType type, const char* name)
{
	const char* label = "";
	switch (type)
	{
		case strus::QueryProcessorInterface::PostingJoinOperator: label = _TXT("Posting join operator"); break;
		case strus::QueryProcessorInterface::WeightingFunction: label = _TXT("Weighting function"); break;
		case strus::QueryProcessorInterface::SummarizerFunction: label = _TXT("Summarizer"); break;
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
	fi = funcs.begin(), fe = funcs.end();
	for (; fi != fe; ++fi)
	{
		std::cout << label << " '" << *fi << "' :" << std::endl;
		switch (type)
		{
			case strus::QueryProcessorInterface::PostingJoinOperator:
			{
				const strus::PostingJoinOperatorInterface* opr = queryproc->getPostingJoinOperator( *fi);
				if (opr) std::cout << "* " << opr->getDescription().text() << std::endl;
				break;
			}
			case strus::QueryProcessorInterface::WeightingFunction: 
			{
				const strus::WeightingFunctionInterface* func = queryproc->getWeightingFunction( *fi);
				if (func) printFunctionDescription( std::cout, func->getDescription());
				break;
			}
			case strus::QueryProcessorInterface::SummarizerFunction:
			{
				const strus::SummarizerFunctionInterface* func = queryproc->getSummarizerFunction( *fi);
				if (func) printFunctionDescription( std::cout, func->getDescription());
				break;
			}
		};
	}
}

int main( int argc_, const char* argv_[])
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
				argc_, argv_, 6,
				"h,help", "v,version", "m,module:",
				"M,moduledir:", "R,resourcedir:", "r,rpc:");

		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus storage version ") << STRUS_STORAGE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
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
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("failed to create module loader"));
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

		if (printUsageAndExit)
		{
			std::cout << _TXT("usage:") << " strusHelp [options] <what> <name>" << std::endl;
			std::cout << "<what> = " << _TXT("specifies what type of item to retrieve (default all):") << std::endl;
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
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    " << _TXT("Load components from module <MOD>") << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search modules to load first in <DIR>") << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    " << _TXT("Search resource files for analyzer first in <DIR>") << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    " << _TXT("Execute the command on the RPC server specified by <ADDR>") << std::endl;
			return rt;
		}
		std::string what;
		std::string item;
		if (opt.nofargs() > 0)
		{
			what = opt[0];
			if (what.empty()) throw strus::runtime_error(_TXT("illegal empty item type as program argument"));
		}
		if (opt.nofargs() > 1)
		{
			item = opt[1];
			if (item.empty()) throw strus::runtime_error(_TXT("illegal empty item value as program argument"));
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
		// Create objects:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;
		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer.get()));
			if (!messaging.get()) throw strus::runtime_error(_TXT("failed to create rpc client messaging"));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer.get()));
			if (!rpcClient.get()) throw strus::runtime_error(_TXT("failed to create rpc client"));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create rpc analyzer object builder"));
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create rpc storage object builder"));
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			if (!analyzerBuilder.get()) throw strus::runtime_error(_TXT("failed to create analyzer object builder"));
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
			if (!storageBuilder.get()) throw strus::runtime_error(_TXT("failed to create storage object builder"));
		}
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error(_TXT("failed to get text processor"));

		const strus::QueryProcessorInterface* queryproc = storageBuilder->getQueryProcessor();
		if (!queryproc) throw strus::runtime_error(_TXT("failed to get query processor"));

		if (what.empty())
		{
			printTextProcessorDescription( textproc, strus::TextProcessorInterface::TokenizerFunction, 0);
			printTextProcessorDescription( textproc, strus::TextProcessorInterface::NormalizerFunction, 0);
			printTextProcessorDescription( textproc, strus::TextProcessorInterface::AggregatorFunction, 0);
			printQueryProcessorDescription( queryproc, strus::QueryProcessorInterface::PostingJoinOperator, 0);
			printQueryProcessorDescription( queryproc, strus::QueryProcessorInterface::WeightingFunction, 0);
			printQueryProcessorDescription( queryproc, strus::QueryProcessorInterface::SummarizerFunction, 0);
		}
		else if (strus::utils::caseInsensitiveEquals( what, "tokenizer"))
		{
			printTextProcessorDescription( textproc, strus::TextProcessorInterface::TokenizerFunction, item.empty()?0:item.c_str());
		}
		else if (strus::utils::caseInsensitiveEquals( what, "normalizer"))
		{
			printTextProcessorDescription( textproc, strus::TextProcessorInterface::NormalizerFunction, item.empty()?0:item.c_str());
		}
		else if (strus::utils::caseInsensitiveEquals( what, "aggregator"))
		{
			printTextProcessorDescription( textproc, strus::TextProcessorInterface::AggregatorFunction, item.empty()?0:item.c_str());
		}
		else if (strus::utils::caseInsensitiveEquals( what, "join"))
		{
			printQueryProcessorDescription( queryproc, strus::QueryProcessorInterface::PostingJoinOperator, item.empty()?0:item.c_str());
		}
		else if (strus::utils::caseInsensitiveEquals( what, "weighting"))
		{
			printQueryProcessorDescription( queryproc, strus::QueryProcessorInterface::WeightingFunction, item.empty()?0:item.c_str());
		}
		else if (strus::utils::caseInsensitiveEquals( what, "summarizer"))
		{
			printQueryProcessorDescription( queryproc, strus::QueryProcessorInterface::SummarizerFunction, item.empty()?0:item.c_str());
		}
		else
		{
			throw strus::runtime_error(_TXT("unknown item type '%s'"), what.c_str());
		}
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error( errorBuffer->fetchError());
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


