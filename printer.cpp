#include "printer.h"

#include <iostream>
#include <functional>

#include "exception.h"
#include "scope.h"

enum {
    CannotWriteToFile = PrinterCodes,
    ClassHasNoCalls, UnbalancedBracesInPath, ParameterHasNoInTarget,
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

    // TODO: Move all #include generation here from Analyzer
    if ([&] {
                for (const auto& cm: model.callClasses)
                    for (const auto& cp: cm.callOverloads)
                        if (cp.path.find('{') != string::npos)
                            return true;
                return false;
            }())
        cppS << "#include <QtCore/QStringBuilder>\n\n";

    // Make a nested structure of namespaces (C++11 doesn't allow to write
    // namespace NS1::NS2 { } unless NS1 is previously defined).
    Scope ns(hS, "namespace ", model.nsName, "{", "}", "::");
    cppS << "using namespace " << model.nsName << ";\n";

    for_each(model.types.begin(), model.types.end(),
             bind(&Printer::printDataDef, this, _1));

    for_each(model.callClasses.begin(), model.callClasses.end(),
             bind(&Printer::printCall, this, model.nsName, _1));
}

void Printer::printDataDef(const Type& dm)
{
    hS << offset << "struct " << dm.name;
    Scope _scope(hS, "{", "};");
    for(const auto& field: dm.fields)
        hS << offset << field.toString() << ";\n";
    hS << endl;
}

void printSignature(ostream& s, const string& returnType,
                    const string& name,
                    const Call::params_type& params,
                    const string& qualifier = "")
{
    static const Scope::size_type WRAPMARGIN = 80;
    ostringstream lineS;
    Offset _o(lineS, s);
    {
        bool header = qualifier.empty();
        string beforeParams =
                (returnType.empty() ? "" : returnType + " ") +
                (qualifier.empty() ? "" : qualifier + "::") + name + "(";

        Scope o(lineS, beforeParams, header ? ");\n" : ")\n", Scope::NoNewLines);
        for (auto p = params.begin(); p != params.end(); ++p)
        {
            string param = p->toString(header);
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

void Printer::printCall(const string& ns, const CallClass& cm)
{
    if (cm.callOverloads.empty())
    {
        cerr << "Couldn't find any parameter set for the call "
             << cm.className << std::endl;
        fail(ClassHasNoCalls);
    }

//    if (!cm.responseType.fields.empty())
//    {
//        // Complex response type; define the structure(s) before the class
//        hS << offset << "struct " << cm.responseType.name << "\n";
//        Scope sdef(hS, "{", "};");
//        for (const auto& field: cm.responseType.fields)
//            hS << offset << field.toString() << ";\n";
//    }

    // Instead of having a full-fledged class that only has a constructor
    // (or several) but no parseReply(), generate a function that
    // returns a CallConfig for each such a would-be constructor. This also
    // allows to nicely avoid a problem with Invite calls, which are defined
    // twice in different Swagger files but have different sets of parameters.
    if (cm.responseType.name.empty())
        printConstructors(cm, ns);
    else
    {
        hS << offset << "class " << cm.className << " : public CallConfig\n";
        Scope cl(hS, "{", "};");
        Offset p(hS, "public:");
        printConstructors(cm);
        printSignature(hS, "Result<" + cm.responseType.name + ">",
                       "parseReply", {}, cm.className);

        hS << "\n" << offset << "Result<" << cm.responseType.name
           << "> parseReply(" << cm.replyFormatVar.toString() << ");" << endl;
        // TODO: dump parseReply() implementation
    }

}

void Printer::printConstructors(const CallClass& cm, const string& ns)
{
    bool asFunction = !ns.empty();

    bool unsupportedCalls = false;
    for (const auto& call: cm.callOverloads)
    {
        cppS << endl;
        Call::params_type allParams = call.collateParams();
        if (asFunction)
        {
            string returnType = "CallConfigNoReplyBody";
            printSignature(hS, returnType, cm.className, allParams);
            printSignature(cppS, returnType, cm.className, allParams, ns);
            Scope body(cppS, "{", "}");
            cppS << offset << "return { ";
            {
                Offset initializer(cppS, 1);
                printInitializer(cm.className, call);
            }

            cppS << offset << (call.needsToken ? "};" : ", false };") << endl;
        }
        else
        {
            printSignature(hS, "", cm.className, allParams);
            printSignature(cppS, "", cm.className, allParams, cm.className);
            {
                Offset initializerOffset(cppS, 1);
                cppS << offset << ": CallConfig(";
                Offset initializer(cppS, 1);
                printInitializer(cm.className, call);
            }
            cppS << offset << (call.needsToken ? ")" : ", false )") << '\n'
                 << offset << "{ }" << endl;
        }

        for (const auto& p: {call.queryParams, call.headerParams, call.bodyParams})
            if (!p.empty())
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

void Printer::printInitializer(const string& callName, const Call& callOverload)
{
    cppS << '"' << callName << "\", HttpVerb::" << callOverload.verb << '\n';
    {
        Scope apiPathScope(cppS, ", ApiPath(\"", ")\n", Scope::NoNewLines);

        const auto& path = callOverload.path;
        for (string::size_type i = 0; i < path.size();)
        {
            auto i1 = path.find('{', i);
            auto i2 = path.find('}', i1);
            if (i1 == string::npos)
            {
                cppS << path.substr(i) << "\"";
                break;
            }
            if (i2 == string::npos)
                fail(UnbalancedBracesInPath, "The path has '{' without matching '}'");

            cppS << path.substr(i, i1 - i) << "\" % "
                 << path.substr(i1 + 1, i2 - i1 - 1);
            if (i2 != path.size() - 1)
                cppS << " % \"";
            i = i2 + 1;
        }
    }

    if (!callOverload.needsToken
            || !callOverload.bodyParams.empty()
            || !callOverload.queryParams.empty())
        printParamInitializer(callOverload.queryParams, ", Query");

    if (!callOverload.bodyParams.empty() || !callOverload.needsToken)
        printParamInitializer(callOverload.bodyParams, ", Data");
}

inline string dumpJsonPair(const std::string& name)
{
    return "{ \"" + name + "\", " + name + " }";
}

void Printer::printParamInitializer(const Call::params_type& params,
                                    const string& containerName)
{
    Scope paramInitializer(cppS, containerName + " {", "}\n", params.size() > 1);
    switch (params.size())
    {
        case 0: cppS << " "; break;
        case 1: cppS << " " << dumpJsonPair(params.front().name) << " "; break;
        default:
            cppS << offset << dumpJsonPair(params.front().name);
            for_each(params.begin() + 1, params.end(), [&](const VarDecl& p) {
                    cppS << ",\n" << offset << dumpJsonPair(p.name);
                });
            cppS << '\n';
    }
}
