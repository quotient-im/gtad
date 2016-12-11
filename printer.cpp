#include "printer.h"

#include <iostream>
#include <functional>

#include "exception.h"
#include "scope.h"

enum {
    CannotWriteToFile = PrinterCodes, ClassHasNoCalls
};

using namespace std;
using namespace std::placeholders;
using namespace CppPrinting;

Printer::Printer(const string& basePath, const string& filenameBase)
    : hS(basePath + filenameBase + ".h", ios::out|ios::trunc)
    , cppS(basePath + filenameBase + ".cpp", ios::out|ios::trunc)
{
    if (!hS.good())
    {
        cerr << "Couldn't open " << basePath + filenameBase << ".h for writing" << endl;
        fail(CannotWriteToFile);
    }
    if (!cppS.good())
    {
        cerr << "Couldn't open " << basePath + filenameBase << ".cpp for writing" << endl;
        fail(CannotWriteToFile);
    }

    string s = "// This is an auto-generated file; don't edit!\n";
    hS << s << endl;
    cppS << s << "\n#include \"" << filenameBase << ".h\"\n" << endl;
}

void Printer::print(const Model& model)
{
    hS << "#pragma once\n\n"
          "#include \"serverapi/servercallsetup.h\"\n\n";

    for (const auto& header: model.includes)
        hS << "#include " << header << "\n";
    hS << "\n";

    if ([=] {
                for (const auto& cm: model.callModels)
                    for (const auto& cp: cm.callOverloads)
                        if (cp.quotedPath.find('%') != string::npos)
                            return true;
                return false;
            }())
        cppS << "#include <QtCore/QStringBuilder>\n\n";

    // Make a nested structure of namespaces (C++11 doesn't allow to write
    // namespace NS1::NS2 { } unless NS1 is previously defined). The below
    // clumsy statement simply splits model.nsName at ::
    Scope ns(hS, model.nsName, "::", "namespace ", "{", "}");
    cppS << "using namespace " << model.nsName << ";\n";

    for_each(model.dataModels.begin(), model.dataModels.end(),
             bind(&Printer::printDataDef, this, _1));

    for_each(model.callModels.begin(), model.callModels.end(),
             bind(&Printer::printCall, this, model.nsName, _1));
}

void Printer::printDataDef(const DataModel& dm)
{
    hS << offset << "struct " << dm.name;
    Scope _scope(hS, "{", "};");
}

void printSignature(ostream& s, const string& returnType,
                    const string& name,
                    const vector<ParamDecl>& params,
                    const string& qualifier = "")
{
    static const Scope::size_type WRAPMARGIN = 80;
    ostringstream lineS;
    Scope::setOffset(lineS, Scope::getOffset(s));
    {
        bool header = qualifier.empty();
        string leader =
                (returnType.empty() ? "" : returnType + " ") +
                (qualifier.empty() ? "" : qualifier + "::") + name + "(";

        Scope o(lineS, leader, header ? ");" : ")", Scope::NoNewLines);
        for (auto p = params.begin(); p != params.end(); ++p)
        {
            string param = p->toString(/*header*/);
            if (p != params.begin())
            {
                // Wrap lines
                if (lineS.tellp() <= WRAPMARGIN - param.size())
                    lineS << ", ";
                else
                {
                    s << lineS.str() << ",\n";
                    lineS.str({}); // Clear the string under lineS
                    lineS << offset;
                }
            }
            lineS << param;
        }
    }
    s << lineS.str();
}

void Printer::printCall(const string& ns, const CallConfigModel& cm)
{
    if (cm.callOverloads.empty())
    {
        cerr << "Couldn't find any parameter set for the call "
             << cm.className << std::endl;
        fail(ClassHasNoCalls);
    }

    if (!cm.responseType.fields.empty())
    {
        // Complex response type; define the structure(s) before the class
        hS << offset << "struct " << cm.responseType.name << "\n";
        Scope sdef(hS, "{", "};");
        for (const auto& field: cm.responseType.fields)
            hS << offset << field.toString() << ";\n";
    }

    if (cm.responseType.name.empty())
        printConstructors(cm, ns);
    else
    {
        hS << offset << "class " << cm.className << " : public CallConfig\n";
        Scope cl(hS, "{", "};");
        Scope p(hS, "public:");
        printConstructors(cm);
        printSignature(hS, "Result<" + cm.responseType.name + ">",
                       "parseReply", {}, cm.className);

        hS << "\n" << offset << "Result<" << cm.responseType.name
           << "> parseReply(" << cm.replyFormatVar.toString() << ");" << endl;
        // TODO: dump parseReply() implementation
    }

}

void Printer::printConstructors(const CallConfigModel& cm, const string& ns)
{
    bool asFunction = !ns.empty();

    bool unsupportedCalls = false;
    for (auto call: cm.callOverloads)
    {
        cppS << endl;
        string returnType = asFunction ? "CallConfigNoReplyBody" : "";
        printSignature(hS, returnType, cm.className, call.params);
        printSignature(cppS, returnType, cm.className, call.params,
                       asFunction ? ns : cm.className);
        printBody(cm.className, call, asFunction);

        for (auto p: call.params)
            if (p.in != ParamDecl::Path)
                unsupportedCalls = true;
    }
    if (unsupportedCalls)
    {
        cerr << "Warning: " << cm.className << " has one or more "
                "call overloads with parameters outside the path; "
                "these are not supported at the moment" << endl;
    }
}

// Supported signatures:
// in .h:
//retval name
//className
//
// in .cpp:
//retval nsName::className
//retval className::name
//className::name

void Printer::printBody(const string& callName, const CallOverload& callOverload,
                        bool asFunction)
{
    // Instead of having a full-fledged class that only has a constructor
    // (or several) but no parseReply(), generate a function that
    // returns a CallConfig. This also allows to nicely avoid a problem
    // with Invite calls, which are defined twice in different Swagger files
    // but have different sets of parameters.
    Scope body(cppS, asFunction ? "{" : ": ", asFunction ? "}" : "", asFunction);
    Scope initializer(cppS, asFunction ? "return {" : "CallConfig(",
                      asFunction ? "};" : ")", Scope::NoNewLines);
    cppS << "\"" << callName << "\", HttpVerb::" << callOverload.verb << ",\n"
      << offset << "ApiPath(" << callOverload.quotedPath << ") ";
}
