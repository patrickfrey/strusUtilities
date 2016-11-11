/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Query structure used by program loader
/// \file queryStruct.hpp
#ifndef _STRUS_UTILITIES_PROGRAM_QUERY_STRUCT_HPP_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_QUERY_STRUCT_HPP_INCLUDED
#include "queryStruct_private.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/reference.hpp"
#include "strus/programLoader.hpp"
#include <string>
#include <vector>

/// \brief strus toplevel namespace
namespace strus {

/// \brief Forward declaration
class QueryAnalyzerInterface;
/// \brief Forward declaration
class QueryProcessorInterface;
/// \brief Forward declaration
class QueryInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

/// \brief Helper structure that intermediates between query analyzer and the core query interface
class QueryStruct
{
public:
	/// \brief Copy constructor
	QueryStruct( const QueryStruct& o);

	/// \brief Constructor
	explicit QueryStruct( const QueryAnalyzerInterface* qai);

	/// \brief Define a variable
	void defineVariable( const std::string& name);

	/// \brief Define a feature
	void defineFeature( const std::string& featureSet, float weight);

	/// \brief Define a query field
	void defineField( const std::string& fieldType, const std::string& fieldContent, bool isSelection);

	/// \brief Define a meta data restriction field
	void defineMetaDataRestriction( const std::string& metaDataName, const MetaDataRestrictionInterface::CompareOperator& cmp, const std::string& fieldType, const std::string& fieldContent);

	/// \brief Define a query expression node on the top elements of the current stack
	void defineExpression( const PostingJoinOperatorInterface* function, unsigned int arg, int range, unsigned int cardinality);

	/// \brief Define the implicit selection features
	void defineSelectionFeatures( const QueryProcessorInterface* queryproc, const QueryDescriptors& qdescr);

	/// \brief Analyzes the query fields and translates the query structure defined on the analyzed query terms and fills the query interface
	void translate( QueryInterface& query, const QueryProcessorInterface* queryproc, ErrorBufferInterface* errorhnd) const;

private:
	std::vector<QueryExpressionStruct> m_expressions;
	std::vector<QueryFeatureStruct> m_features;
	std::vector<QueryMetaDataStruct> m_metadata;
	std::vector<std::string> m_variables;
	std::vector<QueryGroupStruct> m_groups;
	std::vector<std::pair<std::string,std::string> > m_selectionFeatures;
	strus::Reference<QueryAnalyzerContextInterface> m_analyzer;
	std::vector<unsigned int> m_fieldNoStack;
	unsigned int m_fieldNo;
};

} //namespace
#endif

