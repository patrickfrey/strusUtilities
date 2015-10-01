/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2013,2014 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
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
#include "strus/segmenterInterface.hpp"
#include "strus/programLoader.hpp"
#include "strus/versionAnalyzer.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/reference.hpp"
#include "strus/private/fileio.hpp"
#include "strus/private/cmdLineOpt.hpp"
#include "strus/documentClass.hpp"
#include "strus/private/arithmeticVariantAsString.hpp"
#include "private/programOptions.hpp"
#include "private/version.hpp"
#include "private/inputStream.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
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
		std::cerr << "called pushTerm " << type_ << " '" << value_ << "'" << std::endl;
		printState( std::cerr);
#endif
		m_stack.push_back( m_tree.size());
		m_tree.push_back( TreeNode( (int)m_terms.size()));
		m_terms.push_back( Term( type_, value_));
	}

	virtual void pushExpression(
				const strus::PostingJoinOperatorInterface* operation,
				std::size_t argc, int range)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "called pushExpression " << std::hex << "0x" << (uintptr_t)operation << std::dec << " args " << argc << " range " << range << std::endl;
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
			throw std::runtime_error("illegal expression (more arguments than on stack)");
		}
		std::size_t stkidx = m_stack.size() - argc;
		for (;stkidx < m_stack.size(); ++stkidx)
		{
			int node = m_stack[ stkidx];
			if (m_tree[ node].left >= 0)
			{
				throw std::runtime_error( "corrupt tree data structure");
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

	virtual void pushDuplicate()
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "called pushDuplicate" << std::endl;
		printState( std::cerr);
#endif
		if (m_stack.empty()) throw std::runtime_error( "illegal definition of duplicate without term or expression defined");
		m_stack.push_back( m_stack.back());
	}

	virtual void attachVariable( const std::string& name_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "called attachVariable " << name_ << std::endl;
		printState( std::cerr);
#endif
		if (m_stack.empty()) throw std::runtime_error( "illegal definition of variable assignment without term or expression defined");
		m_variables[ m_stack.back()] = name_;
	}

	virtual void defineFeature( const std::string& set_, float weight_=1.0)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::ostringstream ww;
		ww << std::fixed << std::setw(6) << std::setprecision(4) << weight_;
		std::cerr << "called defineFeature " << set_ << " " << ww.str() << std::endl;
		printState( std::cerr);
#endif
		if (m_stack.empty()) throw std::runtime_error( "illegal definition of feature without term or expression defined");
		m_features.push_back( Feature( set_, weight_, m_stack.back()));
		m_stack.pop_back();
	}

	virtual void defineMetaDataRestriction(
			CompareOperator opr, const std::string& name,
			const strus::ArithmeticVariant& operand, bool newGroup=true)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		const char* ng = newGroup?"new group":"";
		std::cerr << "called defineMetaDataRestriction " << name << " " << Restriction::compareOperatorName(opr) << " " << operand << " " << ng << std::endl;
		printState( std::cerr);
#endif
		m_restrictions.push_back( Restriction( opr, name, operand, newGroup));
	}

	virtual void addDocumentEvaluationSet(
			const std::vector<strus::Index>& docnolist_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "called addDocumentEvaluationSet " << docnolist_.size() << std::endl;
#endif
		m_evalset_docnolist.insert( m_evalset_docnolist.end(), docnolist_.begin(), docnolist_.end());
		std::sort( m_evalset_docnolist.begin(), m_evalset_docnolist.end());
		m_evalset_defined = true;
	}

	virtual void setMaxNofRanks( std::size_t maxNofRanks_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "called setMaxNofRanks " << maxNofRanks_ << std::endl;
#endif
		m_maxNofRanks = maxNofRanks_;
	}

	virtual void setMinRank( std::size_t minRank_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "called setMinRank " << minRank_ << std::endl;
#endif
		m_minRank = minRank_;
	}

	virtual void addUserName( const std::string& username_)
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "called addUserName " << username_ << std::endl;
#endif
		m_users.push_back( username_);
	}

	virtual std::vector<strus::ResultDocument> evaluate()
	{
		return std::vector<strus::ResultDocument>();
	}

	void check() const
	{
		if (m_stack.size() > 0)
		{
			throw std::runtime_error("query definition not complete, stack not empty");
		}
	}

	void print( std::ostream& out) const
	{
		std::vector<Feature>::const_iterator fi = m_features.begin(), fe = m_features.end();
		if (fi != fe)
		{
			out << "Features:" << std::endl;
			for (; fi != fe; ++fi)
			{
				out << "feature '" << fi->set << "' weight=" << fi->weight << std::endl;
				print_expression( out, 1, fi->expression);
			}
		}
		std::vector<Restriction>::const_iterator ri = m_restrictions.begin(), re = m_restrictions.end();
		if (ri != re)
		{
			out << "Restrictions:" << std::endl;
			for (; ri != re; ++ri)
			{
				out << "restriction " << ri->name << " " << ri->oprname() << " '" << ri->operand << "'" << std::endl;
			}
		}
		std::vector<std::string>::const_iterator ui = m_users.begin(), ue = m_users.end();
		if (ui != ue)
		{
			out << "Allowed:" << std::endl;
			for (; ui != ue; ++ui)
			{
				out << "user '" << *ui << "'" << std::endl;
			}
		}
		if (m_evalset_defined)
		{
			std::vector<strus::Index>::const_iterator vi = m_evalset_docnolist.begin(), ve = m_evalset_docnolist.end();
			out << "Evalation document docno set:" << std::endl;
			for (; vi != ve; ++vi)
			{
				out << " " << *vi;
			}
			out << std::endl;
		}
	}

#ifdef STRUS_LOWLEVEL_DEBUG
	void printState( std::ostream& out) const
	{
		out << "Stack:" << std::endl;
		std::vector<int>::const_iterator si = m_stack.begin(), se = m_stack.end();
		for (int sidx=0; si != se; ++si,++sidx)
		{
			out << "address [" << -(int)(m_stack.size() - sidx) << "]" << std::endl;
			print_expression( out, 1, *si);
		}
		out << "Features:" << std::endl;
		std::vector<Feature>::const_iterator fi = m_features.begin(), fe = m_features.end();
		for (; fi != fe; ++fi)
		{
			out << "feature '" << fi->set << "' weight=" << fi->weight << std::endl;
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
		Restriction( CompareOperator opr_, const std::string& name_, strus::ArithmeticVariant operand_, bool newGroup_)
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
		strus::ArithmeticVariant operand;
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
	bool m_evalset_defined;
};


int main( int argc, const char* argv[])
{
	int rt = 0;
	strus::ErrorBufferInterface* errorBuffer = 0;
	strus::ProgramOptions opt;
	bool printUsageAndExit = false;
	try
	{
		errorBuffer = strus::createErrorBuffer_standard( stderr, 2);
		if (!errorBuffer) throw strus::runtime_error( _TXT("failed to create error buffer"));

		opt = strus::ProgramOptions(
				argc, argv, 6,
				"h,help", "v,version", "m,module:", "r,rpc:",
				"M,moduledir:", "R,resourcedir:");
		if (opt( "help")) printUsageAndExit = true;
		if (opt( "version"))
		{
			std::cout << "Strus utilities version " << STRUS_UTILITIES_VERSION_STRING << std::endl;
			std::cout << "Strus analyzer version " << STRUS_ANALYZER_VERSION_STRING << std::endl;
			if (!printUsageAndExit) return 0;
		}
		else if (!printUsageAndExit)
		{
			if (opt.nofargs() > 2)
			{
				std::cerr << "ERROR too many arguments" << std::endl;
				printUsageAndExit = true;
				rt = 1;
			}
			if (opt.nofargs() < 2)
			{
				std::cerr << "ERROR too few arguments" << std::endl;
				printUsageAndExit = true;
				rt = 2;
			}
		}
		std::auto_ptr<strus::ModuleLoaderInterface> moduleLoader( strus::createModuleLoader( errorBuffer));
		if (opt("moduledir"))
		{
			if (opt("rpc")) throw std::runtime_error( "specified mutual exclusive options --moduledir and --rpc");
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
			if (opt("rpc")) throw std::runtime_error( "specified mutual exclusive options --module and --rpc");
			std::vector<std::string> modlist( opt.list("module"));
			std::vector<std::string>::const_iterator mi = modlist.begin(), me = modlist.end();
			for (; mi != me; ++mi)
			{
				moduleLoader->loadModule( *mi);
			}
		}

		if (printUsageAndExit)
		{
			std::cout << "usage: strusAnalyze [options] <program> <queryfile>" << std::endl;
			std::cout << "<program>   = path of analyzer program" << std::endl;
			std::cout << "<queryfile>  = path of query content to analyze ('-' for stdin)" << std::endl;
			std::cout << "description: Analyzes a query and dumps the result to stdout." << std::endl;
			std::cout << "options:" << std::endl;
			std::cout << "-h|--help" << std::endl;
			std::cout << "   Print this usage and do nothing else" << std::endl;
			std::cout << "-v|--version" << std::endl;
			std::cout << "    Print the program version and do nothing else" << std::endl;
			std::cout << "-m|--module <MOD>" << std::endl;
			std::cout << "    Load components from module <MOD>" << std::endl;
			std::cout << "-M|--moduledir <DIR>" << std::endl;
			std::cout << "    Search modules to load first in <DIR>" << std::endl;
			std::cout << "-R|--resourcedir <DIR>" << std::endl;
			std::cout << "    Search resource files for analyzer first in <DIR>" << std::endl;
			std::cout << "-r|--rpc <ADDR>" << std::endl;
			std::cout << "    Execute the command on the RPC server specified by <ADDR>" << std::endl;
			return rt;
		}
		std::string analyzerprg = opt[0];
		std::string querypath = opt[1];

		// Set paths for locating resources:
		if (opt("resourcedir"))
		{
			if (opt("rpc")) throw std::runtime_error( "specified mutual exclusive options --resourcedir and --rpc");
			std::vector<std::string> pathlist( opt.list("resourcedir"));
			std::vector<std::string>::const_iterator
				pi = pathlist.begin(), pe = pathlist.end();
			for (; pi != pe; ++pi)
			{
				moduleLoader->addResourcePath( *pi);
			}
		}
		moduleLoader->addResourcePath( strus::getParentPath( analyzerprg));

		// Create objects for analyzer:
		std::auto_ptr<strus::RpcClientMessagingInterface> messaging;
		std::auto_ptr<strus::RpcClientInterface> rpcClient;
		std::auto_ptr<strus::AnalyzerObjectBuilderInterface> analyzerBuilder;
		std::auto_ptr<strus::StorageObjectBuilderInterface> storageBuilder;

		if (opt("rpc"))
		{
			messaging.reset( strus::createRpcClientMessaging( opt[ "rpc"], errorBuffer));
			rpcClient.reset( strus::createRpcClient( messaging.get(), errorBuffer));
			(void)messaging.release();
			analyzerBuilder.reset( rpcClient->createAnalyzerObjectBuilder());
			storageBuilder.reset( rpcClient->createStorageObjectBuilder());
		}
		else
		{
			analyzerBuilder.reset( moduleLoader->createAnalyzerObjectBuilder());
			storageBuilder.reset( moduleLoader->createStorageObjectBuilder());
		}
		std::auto_ptr<strus::QueryAnalyzerInterface>
			analyzer( analyzerBuilder->createQueryAnalyzer());

		// Load analyzer program:
		unsigned int ec;
		std::string analyzerProgramSource;
		ec = strus::readFile( analyzerprg, analyzerProgramSource);
		if (ec)
		{
			std::ostringstream msg;
			std::cerr << "ERROR failed to load analyzer program " << analyzerprg << " (file system error " << ec << ")" << std::endl;
			return 4;
		}
		const strus::TextProcessorInterface* textproc = analyzerBuilder->getTextProcessor();
		strus::loadQueryAnalyzerProgram( *analyzer, textproc, analyzerProgramSource);

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
		strus::loadQuery( query, analyzer.get(), queryproc, querysource);

		query.check();
		query.print( std::cout);
		delete errorBuffer;
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << _TXT("ERROR ") << _TXT("out of memory") << std::endl;
	}
	catch (const std::runtime_error& e)
	{
		const char* errormsg = errorBuffer?errorBuffer->fetchError():0;
		if (errormsg)
		{
			std::cerr << _TXT("ERROR ") << errormsg << ": " << e.what() << std::endl;
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
	delete errorBuffer;
	return -1;
}


