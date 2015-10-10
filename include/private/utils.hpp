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
/// \brief Some utility functions that are centralized to control dependencies to boost
#ifndef _STRUS_UTILS_HPP_INCLUDED
#define _STRUS_UTILS_HPP_INCLUDED
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <stdint.h>	///... Needed by <boost/atomic.hpp> (!)
#include <boost/atomic.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp> 

namespace strus {
namespace utils {

std::string tolower( const std::string& val);
std::string trim( const std::string& val);
bool caseInsensitiveEquals( const std::string& val1, const std::string& val2);
bool caseInsensitiveStartsWith( const std::string& val, const std::string& prefix);
int toint( const std::string& val);
std::string tostring( int val);

template <class X>
class SharedPtr
	:public boost::shared_ptr<X>
{
public:
	SharedPtr( X* ptr)
		:boost::shared_ptr<X>(ptr){}
	SharedPtr( X* ptr, void (*Deleter)( X* ptr))
		:boost::shared_ptr<X>(ptr,Deleter){}
	SharedPtr( const SharedPtr& o)
		:boost::shared_ptr<X>(o){}
	SharedPtr()
		:boost::shared_ptr<X>(){}
};

template <class X>
class ScopedPtr
	:public boost::scoped_ptr<X>
{
public:
	ScopedPtr( X* ptr)
		:boost::scoped_ptr<X>(ptr){}
	ScopedPtr( const ScopedPtr& o)
		:boost::scoped_ptr<X>(o){}
	ScopedPtr()
		:boost::scoped_ptr<X>(){}
};

typedef boost::atomic<bool> AtomicBool;
typedef boost::mutex Mutex;
typedef boost::mutex::scoped_lock ScopedLock;
typedef boost::unique_lock<boost::mutex> UniqueLock;
typedef boost::condition_variable ConditionVariable;

template <class Task>
class Thread
	:public boost::thread
{
public:
	explicit Thread( Task* task)
		:boost::thread( boost::bind( &Task::run, task))
	{}
};

typedef boost::thread_group ThreadGroup;

std::string string_sprintf( const char* format, ...);


}} //namespace
#endif


