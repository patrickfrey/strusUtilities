/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Private structures for query of program loader
/// \file queryStruct_private.hpp
#ifndef _STRUS_UTILITIES_PROGRAM_QUERY_STRUCT_PRIVATE_HPP_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_QUERY_STRUCT_PRIVATE_HPP_INCLUDED
#include "strus/metaDataRestrictionInterface.hpp"
#include <string>
#include <vector>

/// \brief strus toplevel namespace
namespace strus {

/// \brief Forward declaration
class PostingJoinOperatorInterface;

struct QueryExpressionStruct
{
	QueryExpressionStruct( const PostingJoinOperatorInterface* function_, int arg_, int range_, unsigned int cardinality_)
		:function(function_),arg(arg_),range(range_),cardinality(cardinality_){}
	QueryExpressionStruct( const QueryExpressionStruct& o)
		:function(o.function),arg(o.arg),range(o.range),cardinality(o.cardinality){}
	QueryExpressionStruct()
		:function(0),arg(-1),range(0),cardinality(0){}

	const PostingJoinOperatorInterface* function;
	unsigned int arg;
	int range;
	unsigned int cardinality;
};

struct QueryFeatureStruct
{
	QueryFeatureStruct( const std::string& featureSet_, float weight_)
		:featureSet(featureSet_),weight(weight_){}
	QueryFeatureStruct( const QueryFeatureStruct& o)
		:featureSet(o.featureSet),weight(o.weight){}
	QueryFeatureStruct()
		:featureSet(),weight(0.0f){}

	std::string featureSet;
	float weight;
};

struct QueryMetaDataStruct
{
	std::string name;
	MetaDataRestrictionInterface::CompareOperator cmp;
	bool newGroup;

	QueryMetaDataStruct( const std::string& name_, const MetaDataRestrictionInterface::CompareOperator& cmp_, bool newGroup_)
		:name(name_),cmp(cmp_),newGroup(newGroup_){}
	QueryMetaDataStruct( const QueryMetaDataStruct& o)
		:name(o.name),cmp(o.cmp),newGroup(o.newGroup){}
};

struct QueryGroupStruct
{
	enum Type {
		QueryExpressionStructType,
		QueryFeatureStructType,
		QueryMetaDataStructType,
		QueryVariableDef
	};
	Type type;
	unsigned int idx;

	QueryGroupStruct( const Type& type_, unsigned int idx_)
		:type(type_),idx(idx_){}
	QueryGroupStruct( const QueryGroupStruct& o)
		:type(o.type),idx(o.idx){}
};

} //namespace
#endif

