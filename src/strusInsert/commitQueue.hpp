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
#ifndef _STRUS_INSERTER_COMMIT_QUEUE_HPP_INCLUDED
#define _STRUS_INSERTER_COMMIT_QUEUE_HPP_INCLUDED
#include "strus/storageInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/index.hpp"
#include "docnoAllocator.hpp"
#include <vector>
#include <string>
#include <set>
#include <boost/thread/mutex.hpp>
#include <boost/shared_ptr.hpp>

namespace strus {

class CommitQueue
{
public:
	CommitQueue(
			StorageInterface* storage_)
		:m_storage(storage_)
	{
		m_minDocno = m_storage->maxDocumentNumber() + 1;
	}

	void push(
		StorageTransactionInterface* transaction,
		const Index& minDocno,
		const Index& nofDocuments);

private:
	class OpenTransaction
	{
	public:
		OpenTransaction( StorageTransactionInterface* t, Index d, Index n)
			:m_transaction(t),m_minDocno(d),m_nofDocuments(n){}
		OpenTransaction( const OpenTransaction& o)
			:m_transaction(o.m_transaction)
			,m_minDocno(o.m_minDocno)
			,m_nofDocuments(o.m_nofDocuments){}

		StorageTransactionInterface* transaction() const
		{
			return m_transaction;
		}

		Index minDocno() const
		{
			return m_minDocno;
		}

		Index nofDocuments() const
		{
			return m_nofDocuments;
		}

		bool operator<( const OpenTransaction& o) const
		{
			return m_minDocno < o.m_minDocno;
		}

	private:
		StorageTransactionInterface* m_transaction;
		Index m_minDocno;
		Index m_nofDocuments;
	};

	StorageTransactionInterface* getNextTransaction( Index& nofDocs);

private:
	StorageInterface* m_storage;
	Index m_minDocno;
	std::set<OpenTransaction> m_openTransactions;
	boost::mutex m_mutex;
};

}//namespace
#endif
