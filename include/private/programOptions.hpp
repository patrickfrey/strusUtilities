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
#ifndef _STRUS_UTILITIES_PROGRAM_OPTIONS_HPP_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_OPTIONS_HPP_INCLUDED
#include <cstring>
#include <stdexcept>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdarg>
#include <iostream>
#include <boost/lexical_cast.hpp>

namespace strus {

class ProgramOptions
{
private:
	struct OptMapDef
	{
		std::map<std::string,bool> longnamemap;
		std::map<char,std::string> aliasmap;

		OptMapDef(){}

		void add( const char* arg)
		{
			bool hasArg = false;
			char alias = '\0';
			char const* aa = arg;
			const char* longnamestart = arg;
			const char* longnameend = arg + std::strlen(arg);
			for (;*aa;++aa)
			{
				if (*aa == ',')
				{
					if (aa - arg != 1)
					{
						throw std::runtime_error( "one character option expected before comma ',' in option definition string");
					}
					alias = *arg;
					longnamestart = aa+1;
				}
				if (*aa == ':')
				{
					if (longnameend - aa != 1)
					{
						throw std::runtime_error( "colon expected only at end of option definition string");
					}
					longnameend = aa;
					hasArg = true;
				}
			}
			std::string longname( longnamestart, longnameend-longnamestart);
			if (longname.empty())
			{
				if (!alias)
				{
					throw std::runtime_error( "empty option definition");
				}
				longname.push_back( alias);
			}
			if (alias)
			{
				aliasmap[ alias] = longname;
			}
			longnamemap[ longname] = hasArg;
		}

		bool getOpt( const char* argv, std::vector<std::string>& optlist, std::string& optarg)
		{
			optlist.clear();
			optarg.clear();

			if (argv[0] != '-' || argv[1] == '\0') return false;

			if (argv[1] == '-')
			{
				const char* oo = argv+2;
				const char* aa = std::strchr( oo, '=');
				if (aa)
				{
					optlist.push_back( std::string( oo, aa-oo));
					optarg = std::string( aa+1);
				}
				else
				{
					optlist.push_back( std::string( oo));
				}
			}
			else
			{
				const char* oo = argv+1;
				for (;*oo; ++oo)
				{
					std::map<char,std::string>::const_iterator oi = aliasmap.find( *oo);
					if (oi == aliasmap.end())
					{
						if (oo == argv+1) throw std::runtime_error( std::string( "unknown option '-") + *oo +"'");
						optarg = std::string( oo);
						break;
					}
					optlist.push_back( std::string( oi->second));
				}
			}
			return true;
		}
	};

public:
	ProgramOptions()
		:m_argc(0),m_argv(0){}

	ProgramOptions( const ProgramOptions& o)
		:m_argc(o.m_argc),m_argv(o.m_argv),m_opt(o.m_opt){}

	ProgramOptions(
			int argc_, const char** argv_,
			int nofopt, ...)

		:m_argc(argc_-1),m_argv(argv_+1)
	{
		//[1] Initialize options map:
		OptMapDef optmapdef;
		va_list ap;
		va_start( ap, nofopt);

		for (int ai=0; ai<nofopt; ai++)
		{
			const char* av = va_arg( ap, const char*);
			optmapdef.add( av);
		}
		va_end(ap);

		//[2] Parse options and fill m_opt:
		std::vector<std::string> optlist;
		std::string optarg;

		for (; m_argc && optmapdef.getOpt( *m_argv, optlist, optarg); ++m_argv,--m_argc)
		{
			std::vector<std::string>::const_iterator oi = optlist.begin(), oe = optlist.end();
			for (; oi != oe; ++oi)
			{
				std::map<std::string,bool>::iterator li = optmapdef.longnamemap.find( *oi);
				if (li == optmapdef.longnamemap.end()) throw std::runtime_error( std::string( "unknown option '--") + *oi +"'");
				if (li->second && oi+1 == oe)
				{
					if (optarg.empty() && m_argc > 1 && m_argv[1][0] != '-')
					{
						if (m_argv[1][0] == '=')
						{
							if (!m_argv[1][1] && m_argc > 2)
							{
								--m_argc;
								++m_argv;
								m_opt.insert( OptMapElem( *oi, std::string( m_argv[1])));
							}
							else
							{
								m_opt.insert( OptMapElem( *oi, std::string( m_argv[1]+1)));
							}
						}
						else
						{
							m_opt.insert( OptMapElem( *oi, std::string( m_argv[1])));
						}
						--m_argc;
						++m_argv;
					}
					else
					{
						m_opt.insert( OptMapElem( *oi, optarg));
					}
				}
				else
				{
					m_opt.insert( OptMapElem( *oi, std::string()));
				}
			}
		}
	}

	bool operator()( const std::string& optname) const
	{
		return (m_opt.find( optname) != m_opt.end());
	}

	const char* operator[]( std::size_t idx) const
	{
		if (idx >= m_argc) return 0;
		return m_argv[ idx];
	}

	const char* operator[]( const std::string& optname) const
	{
		if (m_opt.count( optname) > 1)
		{
			throw std::runtime_error( std::string( "option '") + optname + "' specified more than once");
		}
		std::map<std::string,std::string>::const_iterator
			oi = m_opt.find( optname);
		if (oi == m_opt.end()) return 0;
		return oi->second.c_str();
	}

	template <typename ValueType>
	int as( const std::string& optname) const
	{
		if (m_opt.count( optname) > 1)
		{
			throw std::runtime_error( std::string( "option '") + optname + "' specified more than once");
		}
		std::map<std::string,std::string>::const_iterator
			oi = m_opt.find( optname);
		if (oi == m_opt.end()) return 0;
		try
		{
			return boost::lexical_cast<ValueType>( oi->second);
		}
		catch (const boost::bad_lexical_cast&)
		{
			throw std::runtime_error( std::string("option '") + optname + "' has not the requested value type");
		}
	}

	std::vector<std::string> list( const std::string& optname) const
	{
		std::vector<std::string> rt;
		std::pair<OptMap::const_iterator,OptMap::const_iterator>
			range = m_opt.equal_range( optname);
		OptMap::const_iterator ei = range.first, ee = range.second;
		for (; ei != ee; ++ei)
		{
			rt.push_back( ei->second);
		}
		return rt;
	}

	int nofargs() const
	{
		return m_argc;
	}

	const char** argv() const
	{
		return m_argv;
	}

	void print( std::ostream& out)
	{
		std::map<std::string,std::string>::const_iterator oi = m_opt.begin(), oe = m_opt.end();
		for (; oi != oe; ++oi)
		{
			out << "--" << oi->first << "=" << oi->second << std::endl;
		}
		std::size_t ai = 0, ae = m_argc;
		for (; ai != ae; ++ai)
		{
			out << "[" << ai << "] " << m_argv[ai] << std::endl;
		}
	}

private:
	std::size_t m_argc;
	char const** m_argv;
	typedef std::multimap<std::string,std::string> OptMap;
	typedef std::pair<std::string,std::string> OptMapElem;
	OptMap m_opt;
};

}//namespace
#endif
