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
#ifndef _STRUS_THREAD_HPP_INCLUDED
#define _STRUS_THREAD_HPP_INCLUDED
#include <boost/thread.hpp>

namespace strus {

template <class Task>
class Thread
{
public:
	Thread( Task* task_)
		:m_task(task_),m_thread(0){}

	~Thread()
	{
		delete m_task;
	}

	void start()
	{
		if (m_thread) throw std::logic_error( "called subsequent start without wait termination in Thread");
		m_thread = new boost::thread( boost::bind( &Task::run, m_task));
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
	boost::thread* m_thread;
};

template <class Task>
class ThreadGroup
{
public:
	ThreadGroup(){}

	~ThreadGroup()
	{
		typename std::vector<Task*>::const_iterator ti = m_task.begin(), te = m_task.end();
		for (; ti != te; ++ti) delete *ti;
	}

	void start( Task* task_)
	{
		m_task.push_back( task_);
		m_thread_group.add_thread( 
			new boost::thread( boost::bind( &Task::run, task_)));
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
	boost::thread_group m_thread_group;
};

}//namespace
#endif
