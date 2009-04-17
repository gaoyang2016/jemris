/** @file Attribute.cpp
 *  @brief Implementation of Attribute
 *
 * Author: tstoecker
 * Date  : Mar 18, 2009
 */

/*
 *  JEMRIS Copyright (C) 2007-2009  Tony Stöcker, Kaveh Vahedipour
 *                                  Forschungszentrum Jülich, Germany
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include "Attribute.h"
#include "Prototype.h"
#include "AtomicSequence.h"
#include "ginac_functions.h"

/***********************************************************/
 Attribute::~Attribute (){
	if (m_datatype == typeid(  double*).name() ) delete ((  double*) m_backup);
	if (m_datatype == typeid(     int*).name() ) delete ((     int*) m_backup);
	if (m_datatype == typeid(    long*).name() ) delete ((    long*) m_backup);
	if (m_datatype == typeid(unsigned*).name() ) delete ((unsigned*) m_backup);
	if (m_datatype == typeid(    bool*).name() ) delete ((    bool*) m_backup);
	if (m_datatype == typeid(  string*).name() ) delete ((  string*) m_backup);
};

/***********************************************************/
void Attribute::AttachObserver (Attribute* attrib){

	if ( !IsObservable() ) return;
	m_symbol = GiNaC::realsymbol(m_prototype->GetName()+"_"+m_name);
	for (int i=0; i<m_observers.size(); i++) if ( attrib == m_observers.at(i) ) return;
	m_observers.push_back(attrib);
	attrib->AttachSubject(this);

};

/***********************************************************/
void Attribute::AttachSubject (Attribute* attrib){

	if ( !attrib->IsObservable() ) return;
	for (int i=0; i<m_subjects.size(); i++) if ( attrib == m_subjects.at(i) ) return;

	m_subjects.push_back(attrib);
	attrib->AttachObserver(this);

};


/***********************************************************/
void Attribute::UpdatePrototype (Prototype* prot){
	prot->Prepare(PREP_UPDATE);
	if (prot->GetType() == MOD_PULSE) ((AtomicSequence*) prot->GetParent())->CollectTPOIs();
};

/***********************************************************/
bool Attribute::SetMember (string expr, const vector<Attribute*>& obs_attribs, bool verbose){

	//set my own symbol
	m_symbol = GiNaC::realsymbol(m_prototype->GetName()+"_"+m_name);

	//attribute represents a string
	if (GetTypeID()==typeid(string*).name()) { WriteMember(expr); return true; }

	//attribute represents a PulseAxis
	if (GetTypeID()==typeid(PulseAxis*).name()) {
		if (expr=="RF") { WriteMember(AXIS_RF);   return true; }
		if (expr=="GX") { WriteMember(AXIS_GX);   return true; }
		if (expr=="GY") { WriteMember(AXIS_GY);   return true; }
		if (expr=="GZ") { WriteMember(AXIS_GZ);   return true; }
		else			{ WriteMember(AXIS_VOID); return true; }
	}

	//GiNaC expressions
	Prototype::ReplaceString(expr,"step","csgn");
	if (expr.find("I", 0)!=string::npos) m_complex = true;

	m_subjects.clear();
    GiNaC::lst symlist;
    //loop over all possibly observed subjects
	for (int i=0; i<obs_attribs.size() ; i++) {
		//convert string "a1","a2", ... to the matching symbol name
		Attribute* subject_attrib = obs_attribs.at(i);
		string  SymbolName = subject_attrib->GetPrototype()->GetName() + "_" + subject_attrib->GetName();
        stringstream key; key << "a" << i+1;
        if (!Prototype::ReplaceString(expr,key.str(),SymbolName)) continue;
        //still here? the attribute was in the expression
        AttachSubject( subject_attrib );
        symlist.append( subject_attrib->GetSymbol() );
	}

	m_expression = expr;

	//test the expression evaluation once
	try {

		EvalExpression ();

	} catch (exception &p) {

        if ( verbose ) {

                    cout   << "Warning in " << m_prototype->GetName() << ": attribute " << GetName()
                           << " can not evaluate its GiNaC expression E = " << expr
                           << ". Reason: " << p.what() << endl;

            }
        return false;
	}

    return true;
};

/***********************************************************/
void Attribute::EvalExpression () {

	if (m_expression.empty()) return;

	//collect symbols and corresponding member-values from observed attributes
	GiNaC::lst symlist;
	GiNaC::lst numlist;
	for (int i=0; i<m_subjects.size() ; i++) {
		Attribute* a = m_subjects.at(i);
		symlist.append( a->GetSymbol() );
		if (a->GetTypeID()==typeid(  double*).name()) { numlist.append(a->GetMember  <double>() ); continue; }
		if (a->GetTypeID()==typeid(     int*).name()) { numlist.append(a->GetMember     <int>() ); continue; }
		if (a->GetTypeID()==typeid(    long*).name()) { numlist.append(a->GetMember    <long>() ); continue; }
		if (a->GetTypeID()==typeid(unsigned*).name()) { numlist.append(a->GetMember<unsigned>() ); continue; }
		if (a->GetTypeID()==typeid(    bool*).name()) { numlist.append(a->GetMember    <bool>() ); continue; }
	}

	//numeric evaluation of GiNaC expression
	GiNaC::ex e(m_expression,symlist);
	//differentiation of expression?
	if (m_diff>0) {  e = e.diff(m_sym_diff,m_diff);  }
	e = e.subs(symlist,numlist);
	m_static_vector = m_prototype->GetVector(); // static pointer to evaluate the Vector function
	e = GiNaC::evalf(e);
	double d = 0.0;
	if (GiNaC::is_a<GiNaC::numeric>(e)) {
		if ( m_complex ) m_imaginary = -0.5 * GiNaC::ex_to<GiNaC::numeric>( (e-e.conjugate())*GiNaC::I ).to_double();
		d = GiNaC::ex_to<GiNaC::numeric>( e ).to_double();//default is real-part
	}

	//overwrite private member
	if (m_datatype==typeid(  double*).name() ) WriteMember((double)   d );
	if (m_datatype==typeid(     int*).name() ) WriteMember((int)      d );
	if (m_datatype==typeid(    long*).name() ) WriteMember((long)     d );
	if (m_datatype==typeid(unsigned*).name() ) WriteMember((unsigned) d );
	if (m_datatype==typeid(    bool*).name() ) WriteMember((bool)     d );

};

