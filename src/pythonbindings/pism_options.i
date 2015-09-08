%{
#include "base/util/pism_options.hh"
%}

%include "base/util/options.hh"
// instantiate templates used by option processing classes below
%template(OptionStdString) pism::options::Option<std::string>;
%template(OptionStdVectorStdString) pism::options::Option<std::vector<std::string> >;
%template(OptionStdSetStdString) pism::options::Option<std::set<std::string> >;
%template(OptionInt) pism::options::Option<int>;
%template(OptionStdVectorInt) pism::options::Option<std::vector<int> >;
%template(OptionDouble) pism::options::Option<double>;
%template(OptionStdVectorDouble) pism::options::Option<std::vector<double> >;

// rename classes in pism::options (SWIG flattens namespaces)
%rename(OptionString) pism::options::String;
%rename(OptionStringList) pism::options::StringList;
%rename(OptionStringSet) pism::options::StringSet;
%rename(OptionKeyword) pism::options::Keyword;
%rename(OptionInteger) pism::options::Integer;
%rename(OptionIntegerList) pism::options::IntegerList;
%rename(OptionReal) pism::options::Real;
%rename(OptionRealList) pism::options::RealList;
%rename(OptionBool) pism::options::Bool;

%ignore pism::options::StringList::operator[];
%ignore pism::options::IntegerList::operator[];
%ignore pism::options::RealList::operator[];

%include "base/util/pism_options.hh"
