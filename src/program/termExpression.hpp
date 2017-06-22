/*
 * Copyright (c) 2017 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_BINDING_IMPL_TERM_EXPRESSION_HPP_INCLUDED
#define _STRUS_BINDING_IMPL_TERM_EXPRESSION_HPP_INCLUDED
#include "strus/analyzer/queryTermExpression.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/queryAnalyzerContextInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "queryAnalyzerStruct.hpp"
#include "private/internationalization.hpp"
#include <vector>
#include <string>

namespace strus {

/// \brief Analyzed term expression structure
class TermExpression
{
public:
	enum {VariableOfs=1<<31,FeatureOfs=1<<30,MaxValue=1<<28};

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

	/// \brief Constructor
	TermExpression( const QueryAnalyzerStruct* analyzerStruct_, const QueryAnalyzerInterface* analyzer_, ErrorBufferInterface* errorhnd_)
		:m_errorhnd(errorhnd_),m_analyzerStruct(analyzerStruct_),m_analyzer(analyzer_->createContext()),m_fieldno_stack(),m_fieldno_cnt(0)
		,m_expr(),m_operators(),m_variables()
	{
		if (!m_analyzer) throw strus::runtime_error(_TXT("failed to create analyzer context: %s"), m_errorhnd->fetchError());
	}
	~TermExpression()
	{
		delete m_analyzer;
	}

	/// \brief Reference to query structure
	const analyzer::QueryTermExpression& expression() const
	{
		return m_expr;
	}
	static bool isVariable( unsigned int groupidx)
	{
		return groupidx >= (unsigned int)VariableOfs
			&& groupidx < (unsigned int)VariableOfs + (unsigned int)MaxValue;
	}
	static bool isFeature( unsigned int groupidx)
	{
		return groupidx >= (unsigned int)FeatureOfs
			&& groupidx < (unsigned int)FeatureOfs + (unsigned int)MaxValue;
	}
	static bool isOperator( unsigned int groupidx)
	{
		return groupidx
			&& groupidx < (unsigned int)MaxValue;
	}
	const std::string& variableName( unsigned int groupidx) const
	{
		return m_variables[ groupidx-(unsigned int)VariableOfs-1];
	}
	struct Feature
	{
		std::string name;
		double weight;

		Feature( const std::string& name_, double weight_)
			:name(name_),weight(weight_){}
		Feature( const Feature& o)
			:name(o.name),weight(o.weight){}
	};
	const Feature& feature( unsigned int groupidx) const
	{
		return m_features[ groupidx-(unsigned int)FeatureOfs-1];
	}
	const Operator& operatorStruct( unsigned int groupidx) const
	{
		return m_operators[ groupidx-1];
	}
	unsigned int newVariable( const std::string& name)
	{
		m_variables.push_back( name);
		if (m_variables.size() >= (unsigned int)MaxValue) throw std::bad_alloc();
		return m_variables.size() + TermExpression::VariableOfs;
	}
	unsigned int newFeature( const std::string& name, double weight)
	{
		m_features.push_back( Feature( name, weight));
		if (m_features.size() >= (unsigned int)MaxValue) throw std::bad_alloc();
		return m_features.size() + TermExpression::FeatureOfs;
	}
	unsigned int newOperator( std::string name, int range, unsigned int cardinality)
	{
		m_operators.push_back( Operator( name, range, cardinality));
		if (m_operators.size() >= (unsigned int)MaxValue) throw std::bad_alloc();
		return m_operators.size();
	}

	void pushField( const std::string& fieldtype, const std::string& value);

	void pushExpression( const std::string& op, unsigned int argc, int range, unsigned int cardinality);

	void attachVariable( const std::string& name);

	void assignFeature( const std::string& name, double weight=1.0);

	void analyze()
	{
		m_expr = m_analyzer->analyze();
		if (m_errorhnd->hasError()) throw strus::runtime_error(_TXT("failed to analyze term expression: %s"), m_errorhnd->fetchError());
	}

	void translate( QueryInterface& query, const QueryProcessorInterface* queryproc);

	unsigned int nofExpressionsDefined() const
	{
		return m_fieldno_stack.size();
	}

private:
	TermExpression( const TermExpression&){}		//... non copyable
	void operator=( const TermExpression&){}		//... non copyable

private:
	ErrorBufferInterface* m_errorhnd;
	const QueryAnalyzerStruct* m_analyzerStruct;
	QueryAnalyzerContextInterface* m_analyzer;
	std::vector<unsigned int> m_fieldno_stack;
	unsigned int m_fieldno_cnt;
	analyzer::QueryTermExpression m_expr;
	std::vector<Operator> m_operators;
	std::vector<std::string> m_variables;
	std::vector<Feature> m_features;
};

}//namespace
#endif

