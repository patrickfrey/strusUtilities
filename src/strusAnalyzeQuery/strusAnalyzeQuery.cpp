/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/lib/module.hpp"
#include "strus/lib/error.hpp"
#include "strus/lib/rpc_client.hpp"
#include "strus/lib/rpc_client_socket.hpp"
#include "strus/rpcClientInterface.hpp"
#include "strus/rpcClientMessagingInterface.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/metaDataRestrictionInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionModule.hpp"
#include "strus/versionRpc.hpp"
#include "strus/versionTrace.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/versionBase.hpp"
#include "private/version.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/reference.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/cmdLineOpt.hpp"
#include "private/programOptions.hpp"
#include "private/inputStream.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/traceUtils.hpp"
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <iomanip>
#include <inttypes.h>

static void print_number( char* buf, unsigned int bufsize, strus::GlobalCounter num)
{
	unsigned int bi = 0, be = bufsize;
	if (num == 0)
	{
		buf[++bi] = '0';
	}
	else for (; bi != be && num > 0; ++bi)
	{
		buf[bi] = (num % 10) + '0';
		num /= 10;
	}
	buf[ bi] = '\0';
	for (bi=be/2; bi > 0; --bi)
	{
		char tmp = buf[bi-1];
		buf[bi-1] = buf[be-bi-1];
		buf[be-bi-1] = tmp;
	}
}

#undef STRUS_LOWLEVEL_DEBUG

class Query
	:public strus::QueryInterface
{
public:
	Query()
		:m_maxNofRanks(20),m_minRank(0),m_evalset_defined(false){}

	virtual ~Query(){}

	virtual void pushTerm( const std::string& type_, const std::string& value_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT("called pushTerm %s '%s'"), type_.c_str(), value_.c_str()) << std::endl;
		printState( std::cerr);
#endif
		m_stack.push_back( m_tree.size());
		m_tree.push_back( TreeNode( (int)m_terms.size()));
		m_terms.push_back( Term( type_, value_));
	}

	virtual void pushExpression(
				const strus::PostingJoinOperatorInterface* operation,
				std::size_t argc, int range, unsigned int cardinality)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT("called pushExpression 0x%lx args %u range %d cardinality %u"), (uintptr_t)operation, (unsigned int)argc, range, cardinality) << std::endl;
		printState( std::cerr);
#endif
		int expridx = m_tree.size();
		m_tree.push_back( TreeNode( operation, range));
		if (argc > 0)
		{
			if (argc == 1)
			{
				m_tree.back().child = m_stack.back();
			}
			else
			{
				m_tree.back().child = m_tree.size();
			}
		}
		if (argc > m_stack.size())
		{
			throw strus::runtime_error( _TXT("illegal expression (more arguments than on stack)"));
		}
		std::size_t stkidx = m_stack.size() - argc;
		for (;stkidx < m_stack.size(); ++stkidx)
		{
			int node = m_stack[ stkidx];
			if (m_tree[ node].left >= 0)
			{
				throw strus::runtime_error( _TXT("corrupt tree data structure"));
			}
			m_tree.push_back( m_tree[ node]);
			if (stkidx+1 < m_stack.size())
			{
				m_tree.back().left = m_tree.size();
			}
		}
		m_stack.resize( m_stack.size() - stkidx);
		m_stack.push_back( expridx);
	}

	virtual void attachVariable( const std::string& name_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT("called attachVariable %s"), name_.c_str()) << std::endl;
		printState( std::cerr);
#endif
		if (m_stack.empty()) throw strus::runtime_error( _TXT("illegal definition of variable assignment without term or expression defined"));
		m_variables[ m_stack.back()] = name_;
	}

	virtual void defineFeature( const std::string& set_, double weight_=1.0)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT("called defineFeature %s %.3f"), set_.c_str(), weight_) << std::endl;
		printState( std::cerr);
#endif
		if (m_stack.empty()) throw strus::runtime_error( _TXT("illegal definition of feature without term or expression defined"));
		m_features.push_back( Feature( set_, weight_, m_stack.back()));
		m_stack.pop_back();
	}

	virtual void addMetaDataRestrictionCondition(
			strus::MetaDataRestrictionInterface::CompareOperator opr, const std::string& name,
			const strus::NumericVariant& operand, bool newGroup=true)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::string operandstr = numericVariantToString( operand);

		const char* ng = newGroup?"new group":"";
		std::cerr
			<< strus::utils::string_sprintf(_TXT("called addMetaDataRestrictionCondition %s %s %s %s"), 
					name.c_str(), Restriction::compareOperatorName(opr), operandstr.c_str(), ng)
			<< std::endl;
		printState( std::cerr);
#endif
		m_restrictions.push_back( Restriction( opr, name, operand, newGroup));
	}

	virtual void addDocumentEvaluationSet(
			const std::vector<strus::Index>& docnolist_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT("called addDocumentEvaluationSet %u"), (unsigned int)docnolist_.size()) << std::endl;
#endif
		m_evalset_docnolist.insert( m_evalset_docnolist.end(), docnolist_.begin(), docnolist_.end());
		std::sort( m_evalset_docnolist.begin(), m_evalset_docnolist.end());
		m_evalset_defined = true;
	}

	virtual void defineTermStatistics(
			const std::string& type,
			const std::string& value,
			const strus::TermStatistics& stats_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		char valbuf[ 64];
		print_number( valbuf, sizeof(valbuf), stats_.documentFrequency());
		std::cerr << strus::utils::string_sprintf( _TXT("called defineTermStatistics %s '%s' = %s"), type.c_str(), value.c_str(), valbuf) << std::endl;
#endif
		m_termstats[ Term( type, value)] = stats_;
	}

	virtual void defineGlobalStatistics(
			const strus::GlobalStatistics& stats_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		char valbuf[ 64];
		print_number( valbuf, sizeof(valbuf), stats_.nofDocumentsInserted());
		std::cerr << strus::utils::string_sprintf( _TXT("called defineGlobalStatistics %s"), valbuf) << std::endl;
#endif
		m_globstats = stats_;
	}

	virtual void setMaxNofRanks( std::size_t maxNofRanks_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT("called setMaxNofRanks %u"), (unsigned int)maxNofRanks_) << std::endl;
#endif
		m_maxNofRanks = maxNofRanks_;
	}

	virtual void setMinRank( std::size_t minRank_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT( "called setMinRank %u"), (unsigned int)minRank_) << std::endl;
#endif
		m_minRank = minRank_;
	}

	virtual void addUserName( const std::string& username_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT( "called addUserName %s"), username_.c_str()) << std::endl;
#endif
		m_users.push_back( username_);
	}

	virtual void setWeightingVariableValue( const std::string& name, double value)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << strus::utils::string_sprintf( _TXT( "called setWeightingFormulaVariableValue %s %g"), name.c_str(), value) << std::endl;
#endif
	}

	virtual strus::QueryResult evaluate()
	{
		return strus::QueryResult();
	}

	virtual std::string tostring() const
	{
		return std::string();
	}

	void check() const
	{
		if (m_stack.size() > 0)
		{
			throw strus::runtime_error( _TXT("query definition not complete, stack not empty"));
		}
	}

	void print( std::ostream& out) const
	{
		std::vector<Feature>::const_iterator fi = m_features.begin(), fe = m_features.end();
		if (fi != fe)
		{
			out << _TXT("Features:") << std::endl;
			for (; fi != fe; ++fi)
			{
				out << strus::utils::string_sprintf( _TXT("feature '%s' weight=%.4f"), fi->set.c_str(), fi->weight) << std::endl;
				print_expression( out, 1, fi->expression);
			}
		}
		std::vector<Restriction>::const_iterator ri = m_restrictions.begin(), re = m_restrictions.end();
		if (ri != re)
		{
			out << _TXT("Restrictions:") << std::endl;
			for (; ri != re; ++ri)
			{
				out << strus::utils::string_sprintf( _TXT("restriction %s %s '%s'"), ri->name.c_str(), ri->oprname(), ri->operand.tostring().c_str()) << std::endl;
			}
		}
		std::vector<std::string>::const_iterator ui = m_users.begin(), ue = m_users.end();
		if (ui != ue)
		{
			out << _TXT("Allowed:") << std::endl;
			for (; ui != ue; ++ui)
			{
				out << strus::utils::string_sprintf(_TXT("user '%s'"), ui->c_str()) << std::endl;
			}
		}
		if (m_evalset_defined)
		{
			std::vector<strus::Index>::const_iterator vi = m_evalset_docnolist.begin(), ve = m_evalset_docnolist.end();
			out << _TXT("Evalation document docno set:") << std::endl;
			for (; vi != ve; ++vi)
			{
				out << " " << *vi;
			}
			out << std::endl;
		}
		std::map<Term,strus::TermStatistics>::const_iterator ti=m_termstats.begin(), te=m_termstats.end();
		if (ti != te)
		{
			out << _TXT("Term statistics:") << std::endl;
			for (; ti != te; ++ti)
			{
				char valbuf[ 64];
				print_number( valbuf, sizeof(valbuf), ti->second.documentFrequency());
				out << strus::utils::string_sprintf( _TXT("stats %s '%s' = %s"), ti->first.type.c_str(), ti->first.value.c_str(), valbuf) << std::endl;
			}
		}
		if (m_globstats.nofDocumentsInserted() >= 0)
		{
			out << _TXT("Global statistics:") << std::endl;
			if (m_globstats.nofDocumentsInserted() >= 0)
			{
				out << _TXT("Global statistics:") << std::endl;
				char valbuf[ 64];
				print_number( valbuf, sizeof(valbuf), m_globstats.nofDocumentsInserted());
				out << strus::utils::string_sprintf( _TXT("nof documents inserted: %s"), valbuf) << std::endl;
			}
		}
	}

#ifdef STRUS_LOWLEVEL_DEBUG
	void printState( std::ostream& out) const
	{
		out << _TXT("Stack:") << std::endl;
		std::vector<int>::const_iterator si = m_stack.begin(), se = m_stack.end();
		for (int sidx=0; si != se; ++si,++sidx)
		{
			out << strus::utils::string_sprintf(_TXT("address [%d]"), -(int)(m_stack.size() - sidx)) << std::endl;
			print_expression( out, 1, *si);
		}
		out << _TXT("Features:") << std::endl;
		std::vector<Feature>::const_iterator fi = m_features.begin(), fe = m_features.end();
		for (; fi != fe; ++fi)
		{
			out << strus::utils::string_sprintf(_TXT("feature '%s' weight=%.4f"), fi->set.c_str(), fi->weight) << std::endl;
			print_expression( out, 1, fi->expression);
		}
	}
#endif

private:
	void print_expression( std::ostream& out, std::size_t indent, int treeidx) const
	{
		std::string indentstr( indent*3, ' ');
		while (treeidx >= 0)
		{
			std::string attr;
			std::map<int, std::string>::const_iterator vi = m_variables.find( treeidx);
			if (vi != m_variables.end())
			{
				attr.append( vi->second);
				attr.append( " = ");
			}
			const TreeNode& nod = m_tree[ treeidx];
			if (nod.func)
			{
				out << indentstr << attr << "func[0x" << std::hex << (uintptr_t)nod.func << std::dec << "] range=" << nod.arg << std::endl;
				print_expression( out, indent+1, nod.child);
			}
			else
			{
				const Term& term = m_terms[ nod.arg];
				out << indentstr << attr << "term " << term.type << " '" << term.value << "'" << std::endl;
			}
			treeidx = nod.left;
		}
	}

private:
	class Term
	{
	public:
		Term(){}
		Term( const std::string& type_, const std::string& value_)
			:type(type_),value(value_){}
		Term( const Term& o)
			:type(o.type),value(o.value){}

		bool operator<( const Term& o) const
		{
			int cmpres;
			if (type.size() != o.type.size()) return type.size() < o.type.size();
			if ((cmpres = std::strcmp( type.c_str(), o.type.c_str())) < 0) return cmpres;
			if (value.size() != o.value.size()) return value.size() < o.value.size();
			if ((cmpres = std::strcmp( value.c_str(), o.value.c_str())) < 0) return cmpres;
			return 0;
		}

	public:
		std::string type;
		std::string value;
	};

	class TreeNode
	{
	public:
		TreeNode( const strus::PostingJoinOperatorInterface* func_, int range)
			:func(func_),arg(range),left(-1),child(-1){}
		TreeNode( int term)
			:func(0),arg(term),left(-1),child(-1){}
		TreeNode()
			:func(0),arg(-1),left(-1),child(-1){}
		TreeNode( const TreeNode& o)
			:func(o.func),arg(o.arg),left(o.left),child(o.child){}

	public:
		const strus::PostingJoinOperatorInterface* func;
		int arg;
		int left;
		int child;
	};

	class Feature
	{
	public:
		Feature( const std::string& set_, float weight_, int expression_)
			:set(set_),weight(weight_),expression(expression_){}
		Feature( const Feature& o)
			:set(o.set),weight(o.weight),expression(o.expression){}

	public:
		std::string set;
		float weight;
		int expression;
	};

	class Restriction
	{
	public:
		typedef strus::MetaDataRestrictionInterface::CompareOperator CompareOperator;
		Restriction( CompareOperator opr_, const std::string& name_, strus::NumericVariant operand_, bool newGroup_)
			:opr(opr_),name(name_),operand(operand_),newGroup(newGroup_){}
		Restriction( const Restriction& o)
			:opr(o.opr),name(o.name),operand(o.operand),newGroup(o.newGroup){}

		static const char* compareOperatorName( CompareOperator i)
		{
			static const char* ar[] = {"<","<=","==","!=",">",">="};
			return ar[i];
		}
		const char* oprname() const
		{
			return compareOperatorName( opr);
		}

		CompareOperator opr;
		std::string name;
		strus::NumericVariant operand;
		bool newGroup;
	};

	std::vector<Term> m_terms;
	std::vector<TreeNode> m_tree;
	std::vector<int> m_stack;
	std::map<int, std::string> m_variables;
	std::vector<Feature> m_features;
	std::vector<Restriction> m_restrictions;
	std::size_t m_maxNofRanks;
	std::size_t m_minRank;
	std::vector<std::string> m_users;
	std::vector<strus::Index> m_evalset_docnolist;
	strus::GlobalStatistics m_globstats;
	std::map<Term,strus::TermStatistics> m_termstats;
	bool m_evalset_defined;
};


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
				"h,help", "v,version", "m,module:", "r,rpc:",
				"M,moduledir:", "R,resourcedir:", "T,trace:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << _TXT("Strus utilities version ") << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus module version ") << STRUS_MODULE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus rpc version ") << STRUS_RPC_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus trace version ") << STRUS_TRACE_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus analyzer version ") << STRUS_ANALYZER_VERSION_STRING << std::endl;
			std::cout << _TXT("Strus base version ") << STRUS_BASE_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 2)
			{
				std::cerr << _TXT("too many arguments") << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 2)
			{
				std::cerr << _TXT("too few arguments") << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer.get()));
		if (!moduleLoader.get()) throw strus::runtime_error(_TXT("failed to create module loader"));
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options --moduledir and --rpc"));
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
			if (opt("rpc")) throw strus::runtime_error( _TXT("specified mutual exclusive options --module and --rpc"));
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
			std::cout << _TXT("usage:") << " strusAnalyze [options] <program> <queryfile>" << std::endl;
			std::cout << "<program>   = " << _TXT("path of analyzer program") << std::endl;
			std::cout << "<queryfile> = " << _TXT("path of query content to analyze ('-' for stdin)") << std::endl;
			std::cout << _TXT("description: Analyzes a query and dumps the result to stdout.") << std::endl;
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
			std::cout << "-T|--trace <CONFIG>" << std::endl;
			std::cout << "    " << _TXT("Print method call traces configured with <CONFIG>") << std::endl;
			return rt;
		}
		// Parse arguments:
		std::string analyzerprg = opt[0];
		std::string querypath = opt[1];

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
		std::string resourcepath;
		if (0!=strus::getParentPath( analyzerprg, resourcepath))
		{
			throw strus::runtime_error( _TXT("failed to evaluate resource path"));
		}
		if (!resourcepath.empty())
		{
			moduleLoader->addResourcePath( resourcepath);
		}

		// Create objects for analyzer:
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

		std::auto_ptr<strus::QueryAnalyzerInterface>
			analyzer( analyzerBuilder->createQueryAnalyzer());
		if (!analyzer.get()) throw strus::runtime_error(_TXT("failed to create query analyzer"));

		// Load analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec)
		{
			strus::runtime_error( _TXT("failed to load analyzer program %s (errno %u)"), analyzerprg.c_str(), ec);
		}
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		if (!textproc) throw strus::runtime_error(_TXT("failed to get text processor"));
		if (!strus::loadQueryAnalyzerProgram( *analyzer, textproc, analyzerProgramSource, errorBuffer.get()))
		{
			throw strus::runtime_error( _TXT("failed to load query analyze program %s"), analyzerprg.c_str());
		}
		// Load the query source:
		strus::InputStream input( querypath);
		enum {AnalyzerBufSize=8192};
		char buf[ AnalyzerBufSize];
		bool eof = false;
		std::string querysource;

		while (!eof)
		{
			std::size_t readsize = input.read( buf, sizeof(buf));
			if (!readsize)
			{
				eof = true;
				continue;
			}
			querysource.append( buf, readsize);
		}

		// Load and print the query:
		Query query;
		const strus::QueryProcessorInterface* queryproc = storageBuilder->getQueryProcessor();
		if (!queryproc) throw strus::runtime_error(_TXT("failed to get query processor"));
		if (!strus::loadQuery( query, analyzer.get(), queryproc, querysource, errorBuffer.get()))
		{
			throw strus::runtime_error( _TXT("failed to load query %s"), querypath.c_str());
		}

		query.check();
		query.print( std::cout);
		if (errorBuffer->hasError())
		{
			throw strus::runtime_error(_TXT("error in analyze query"));
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


