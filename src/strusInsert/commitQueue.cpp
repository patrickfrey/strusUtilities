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
#include "commitQueue.hpp"
#include <iostream>

using namespace strus;

void CommitQueue::push(
	StorageTransactionInterface* transaction,
	const Index& minDocno,
	const Index& nofDocuments)
{
	bool myCommit = false;
	{
		boost::mutex::scoped_lock( m_mutex);
		if (m_minDocno == minDocno)
		{
			myCommit = true;
		}
		else
		{
			m_openTransactions.insert(
				OpenTransaction( transaction, minDocno, nofDocuments));
		}
	}
	if (myCommit)
	{
		Index nofDocs = nofDocuments;
		do
		{
			transaction->commit();
			std::cerr << "inserted " << (m_minDocno + nofDocs) << " documents " << std::endl;
			delete transaction;
			{
				boost::mutex::scoped_lock( m_mutex);
				m_minDocno += nofDocs;
			}
			transaction = getNextTransaction( nofDocs);
		}
		while (transaction);
	}
}

StorageTransactionInterface* CommitQueue::getNextTransaction( Index& nofDocs)
{
	StorageTransactionInterface* rt = 0;
	boost::mutex::scoped_lock( m_mutex);

	std::set<OpenTransaction>::reverse_iterator
		ti = m_openTransactions.rbegin();

	if (ti != m_openTransactions.rend())
	{
		if (ti->minDocno() == m_minDocno)
		{
			rt = ti->transaction();
			nofDocs = ti->nofDocuments();
			m_openTransactions.erase( --ti.base());
		}
	}
	return rt;
}

