/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_UTILITIES_PROGRAM_OPTIONS_HPP_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_OPTIONS_HPP_INCLUDED
#include "private/utils.hpp"
#include "private/internationalization.hpp"
#include <cstring>
#include <stdexcept>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstdarg>
#include <iostream>

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
						throw strus::runtime_error( _TXT("one character option expected before comma ',' in option definition string"));
					}
					alias = *arg;
					longnamestart = aa+1;
				}
				if (*aa == ':')
				{
					if (longnameend - aa != 1)
					{
						throw strus::runtime_error( _TXT("colon expected only at end of option definition string"));
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
					throw strus::runtime_error( _TXT("empty option definition"));
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
						if (oo == argv+1) throw strus::runtime_error( _TXT("unknown option '-%c'"), *oo);
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
				if (li == optmapdef.longnamemap.end()) throw strus::runtime_error( _TXT("unknown option '--%s'"), oi->c_str());
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
			throw strus::runtime_error( _TXT("option '%s' specified more than once"), optname.c_str());
		}
		std::map<std::string,std::string>::const_iterator
			oi = m_opt.find( optname);
		if (oi == m_opt.end()) return 0;
		return oi->second.c_str();
	}

	int asInt( const std::string& optname) const
	{
		if (m_opt.count( optname) > 1)
		{
			throw strus::runtime_error( _TXT("option '%s' specified more than once"), optname.c_str());
		}
		std::map<std::string,std::string>::const_iterator
			oi = m_opt.find( optname);
		if (oi == m_opt.end()) return 0;
		try
		{
			return utils::toint( oi->second);
		}
		catch (const std::runtime_error&)
		{
			throw strus::runtime_error( _TXT("option '%s' has not the requested value type"), optname.c_str());
		}
	}

	unsigned int asUint( const std::string& optname) const
	{
		int rt = asInt( optname);
		if (rt < 0) throw strus::runtime_error( _TXT("non negative value expected for option '%s'"), optname.c_str());
		return (unsigned int)rt;
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
