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
#include "metadataExpression.hpp"
#include "termExpression.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/reference.hpp"
#include "private/programLoader.hpp"
#include <string>
#include <vector>

#error DEPRECATED

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
	void defineField( const std::string& fieldType, const std::string& fieldContent);

	/// \brief Define an implicitely defined selection feature with a structure
	void defineImplicitSelection( const std::string& fieldType, const std::string& fieldContent);

	/// \brief Define a meta data restriction field
	void defineMetaDataRestriction( const MetaDataRestrictionInterface::CompareOperator& cmp, const std::string& fieldType, const std::string& fieldContent, bool newGroup);

	/// \brief Define a query expression node on the top elements of the current stack
	void defineExpression( const PostingJoinOperatorInterface* function, unsigned int arg, int range, unsigned int cardinality);

	/// \brief Define the implicit selection features
	void defineSelectionFeatures( const QueryProcessorInterface* queryproc, const QueryDescriptors& qdescr);

	/// \brief Analyzes the query fields and translates the query structure defined on the analyzed query terms and fills the query interface
	void translate( QueryInterface& query, const QueryProcessorInterface* queryproc, ErrorBufferInterface* errorhnd);

private:
	strus::Reference<QueryAnalyzerStruct> m_analyzerStruct;
	strus::Reference<TermExpression> m_termExpression;
	strus::Reference<MetaDataExpression> m_metadataExpression;
};

} //namespace
#endif

