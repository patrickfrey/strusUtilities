/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_THREAD_HPP_INCLUDED
#define _STRUS_THREAD_HPP_INCLUDED
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp> 

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
		m_thread_group.add_thread( new boost::thread( boost::bind( &Task::run, task_)));
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
	const char* m_name;
};

}//namespace
#endif
