/*
 * Copyright (c) 2017 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_BINDING_QUERY_ANALYZER_STRUCT_HPP_INCLUDED
#define _STRUS_BINDING_QUERY_ANALYZER_STRUCT_HPP_INCLUDED
#include "strus/queryAnalyzerContextInterface.hpp"
#include <vector>
#include <string>
#include <map>
#include "private/utils.hpp"

namespace strus {

class QueryAnalyzerStruct
{
public:
	QueryAnalyzerStruct()
		:m_groupmap(){}
	QueryAnalyzerStruct( const QueryAnalyzerStruct& o)
		:m_groupmap(o.m_groupmap){}

	struct Operator
	{
		std::string name;
		int range;
		unsigned int cardinality;

		Operator( const std::string& name_, int range_, unsigned int cardinality_)
			:name(name_),range(range_),cardinality(cardinality_){}
		Operator( const Operator& o)
			:name(o.name),range(o.range),cardinality(o.cardinality){}
	};
	struct GroupOperator
	{
		Operator opr;
		QueryAnalyzerContextInterface::GroupBy groupBy;
		bool groupSingle;

		GroupOperator( const Operator& opr_, QueryAnalyzerContextInterface::GroupBy groupBy_, bool groupSingle_)
			:opr(opr_),groupBy(groupBy_),groupSingle(groupSingle_){}
		GroupOperator( const GroupOperator& o)
			:opr(o.opr),groupBy(o.groupBy),groupSingle(o.groupSingle){}
	};
	typedef std::vector<GroupOperator> GroupOperatorList;

	void autoGroupBy( const std::string& fieldtype_, const std::string& name, int range, unsigned int cardinality, QueryAnalyzerContextInterface::GroupBy groupBy, bool groupSingle)
	{
		std::string fieldtype = utils::tolower( fieldtype_);
		GroupOperator gop( Operator( name, range, cardinality), groupBy, groupSingle);
		GroupMap::iterator gi = m_groupmap.find( fieldtype);
		if (gi == m_groupmap.end())
		{
			m_groupmap[ fieldtype].push_back( gop);
		}
		else
		{
			gi->second.push_back( gop);
		}
	}

	const GroupOperatorList& autoGroupOperators( const std::string& fieldtype_) const
	{
		std::string fieldtype = utils::tolower( fieldtype_);
		static const std::vector<GroupOperator> empty;
		GroupMap::const_iterator gi = m_groupmap.find( fieldtype);
		if (gi == m_groupmap.end())
		{
			return empty;
		}
		else
		{
			return gi->second;
		}
	}

private:
	typedef std::map<std::string,GroupOperatorList> GroupMap;
	GroupMap m_groupmap;
};

}//namespace
#endif

