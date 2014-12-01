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
#include <algorithm>
#include <stdexcept>
#include <cstdarg>
#include <iostream>
#include <boost/lexical_cast.hpp>

namespace strus {

class ProgramOptions
	:public std::map<std::string,std::string>
{
private:
	typedef std::map<std::string,std::string> Parent;

public:
	ProgramOptions()
		:m_argc(0),m_argv(0){}

	ProgramOptions( const ProgramOptions& o)
		:Parent(o),m_argc(o.m_argc),m_argv(o.m_argv){}

	ProgramOptions(
			int argc_, const char** argv_,
			int nofopt, ...)

		:m_argc(argc_-1),m_argv(argv_+1)
	{
		std::set<std::string> defoptmap;
		std::map<char,std::string> aliasmap;
		va_list ap;
		va_start( ap, nofopt);
		for (int ai=0; ai<nofopt; ai++)
		{
			const char* av = va_arg( ap, const char*);
			const char* sp = std::strchr( av, ',');
			if (sp)
			{
				if (sp-av > 1)
				{
					throw std::runtime_error( "one character option expected before comma ',' in option definition string");
				}
				aliasmap[ av[0]] = std::string( sp+1);
				defoptmap.insert( std::string( sp+1));
			}
			else
			{
				defoptmap.insert( std::string( av));
			}
		}
		va_end(ap);

		for (; m_argc && m_argv[0][0] == '-'; --m_argc,++m_argv)
		{
			if (m_argv[0][1] != '-')
			{
				unsigned int oi = 1;
				for (;m_argv[0][oi]; ++oi)
				{
					std::map<char,std::string>::const_iterator
						ai = aliasmap.find( m_argv[0][oi]);
					if (ai == aliasmap.end())
					{
						throw std::runtime_error( std::string( "unknown option '-") + m_argv[0][oi] + "'");
					}
					std::string optname( ai->second);
					if (!m_argv[0][oi+1] && m_argc>1 && m_argv[1][0] != '-')
					{
						Parent::operator[]( optname)
							= std::string( m_argv[1]);
						m_argc -= 1;
						m_argv += 1;
						break;
					}
					else
					{
						Parent::operator[]( optname)
							= std::string();
					}
				}
			}
			else
			{
				std::string optname( m_argv[0]+2);
				if (defoptmap.find( optname) == defoptmap.end())
				{
					throw std::runtime_error( std::string( "unknown option '--") + optname + "'");
				}

				if (m_argc>1 && m_argv[1][0] != '-')
				{
					Parent::operator[]( optname)
						= std::string( m_argv[1]);
					m_argc -= 1;
					m_argv += 1;
				}
				else
				{
					Parent::operator[]( optname) = std::string();
				}
			}
		}
	}

	bool operator()( const std::string& opt, const std::string& alt="") const
	{
		if (Parent::find( opt) != Parent::end()) return true;
		if (alt.size() && Parent::find( alt) != Parent::end()) return true;
		return false;
	}

	const char* operator[]( std::size_t idx) const
	{
		if (idx >= m_argc) return 0;
		return m_argv[ idx];
	}

	const char* operator[]( const std::string& optname) const
	{
		const_iterator oi = find( optname);
		if (oi == end()) return 0;
		return oi->second.c_str();
	}

	template <typename ValueType>
	int as( const std::string& optname)
	{
		const_iterator oi = find( optname);
		if (oi == end()) return 0;
		try
		{
			return boost::lexical_cast<ValueType>( oi->second);
		}
		catch (const boost::bad_lexical_cast&)
		{
			throw std::runtime_error( std::string("option '") + optname + "' has not the requested value type");
		}
	}

	int nofargs() const
	{
		return m_argc;
	}

private:
	std::size_t m_argc;
	char const** m_argv;
};

}//namespace
#endif
