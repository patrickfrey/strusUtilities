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
#ifndef _STRUS_THREAD_HPP_INCLUDED
#define _STRUS_THREAD_HPP_INCLUDED
#include "private/utils.hpp"

namespace strus {

template <class Task>
class Thread
{
private:
	Thread( const Thread&){}		//... non copyable
	Thread& operator=(const Thread&){}	//... non copyable

public:
	explicit Thread( Task* task_, const char* name_)
		:m_task(task_),m_thread(0),m_name(name_)
	{
	}

	~Thread()
	{
		if (m_thread)
		{
			m_task->sigStop();
			wait_termination();
		}
		delete m_task;
	}

	void start()
	{
		if (m_thread) throw std::logic_error( "called subsequent start without wait termination in Thread");
		m_thread = new utils::Thread<Task>( m_task);
	}

	void stop()
	{
		m_task->sigStop();
	}

	void wait_termination()
	{
		m_thread->join();
		delete m_thread;
		m_thread = 0;
	}

	Task* task() const
	{
		return m_task;
	}

private:
	Task* m_task;
	utils::Thread<Task>* m_thread;
	const char* m_name;
};

template <class Task>
class ThreadGroup
{
private:
	ThreadGroup( const ThreadGroup&){}		//... non copyable
	ThreadGroup& operator=(const ThreadGroup&){}	//... non copyable
public:
	ThreadGroup( const char* name_)
		:m_name(name_){}

	~ThreadGroup()
	{
		typename std::vector<Task*>::const_iterator ti = m_task.begin(), te = m_task.end();
		for (; ti != te; ++ti) delete *ti;
	}

	void start( Task* task_)
	{
		m_task.push_back( task_);
		m_thread_group.add_thread( new utils::Thread<Task>( task_));
	}

	void stop()
	{
		typename std::vector<Task*>::const_iterator ti = m_task.begin(), te = m_task.end();
		for (; ti != te; ++ti) (*ti)->sigStop();
	}

	void wait_termination()
	{
		m_thread_group.join_all();
	}

private:
	std::vector<Task*> m_task;
	utils::ThreadGroup m_thread_group;
	const char* m_name;
};

}//namespace
#endif
