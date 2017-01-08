#include "printer.h"

#include <iostream>
#include <algorithm>
#include <functional>

#include "exception.h"

enum {
    CannotWriteToFile = PrinterCodes,
    ClassHasNoCalls, UnbalancedBracesInPath,
};

using namespace std;
using namespace SrcFormatting;

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

    string s = "// This is an auto-generated file; don't edit!\n\n";
    hS << s;
    cppS << s << "#include \"" << filenameBase << ".h\"\n\n";
}

void Printer::print(const Model& model)
{
    hS << "#pragma once\n\n";
    if (!model.callClasses.empty())
        hS << "#include \"serverapi/servercallsetup.h\"\n\n";

    // TODO: Should go to the place that defines a new struct
    if (!model.types.empty() &&
            model.imports.find("<QtCore/QJsonValue>") == model.imports.end())
        hS << "#include <QtCore/QJsonValue>" << '\n';

    for (const auto& header: model.imports)
        if (header.front() == '<')
            hS << "#include " << header << '\n';
    for (const auto& header: model.imports)
        if (header.front() != '<')
            hS << "#include " << header << '\n';
    hS << '\n';

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

    using namespace std::placeholders;
    for_each(model.types.begin(), model.types.end(),
             bind(&Printer::printDataDef, this, _1));

    for_each(model.callClasses.begin(), model.callClasses.end(),
             bind(&Printer::printCall, this, model.nsName, _1));
}

void Printer::printDataDef(const StructDef& dm)
{
    Scope _scope(hS, "struct ", dm.name, "{", "};");
    for(const auto& field: dm.fields)
        hS << offset << field.toString() << ";\n";
    hS << '\n' << offset << "operator QJsonValue() const;\n";
}

void printSignature(Printer::stream_type& s, const string& returnType,
                    const string& name, const Call::params_type& params,
                    const string& qualifier = "")
{
    // Supported signatures:
    // in .h:
    //retval name
    //className
    //
    // in .cpp:
    //retval nsName::className
    //retval className::name
    //className::name

    bool header = qualifier.empty();
    WrappedLine lw { s };
    lw << (returnType.empty() ? "" : returnType + " ")
       << (qualifier.empty() ? "" : qualifier + "::")
       << name << '(' << soft_endl();
    if (!params.empty())
    {
        for (auto p = params.begin();;)
        {
            lw << p->toString(header);
            if (++p == params.end())
                break;
            lw << ',' << soft_endl(" ");
        }
    }
    lw << ')'; if (header) lw << ';';
}

void Printer::printCall(const string& ns, const CallClass& cm)
{
    if (cm.callOverloads.empty())
    {
        cerr << "Couldn't find any parameter set for the call "
             << cm.className << endl;
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
        Scope cl(hS, "class ", cm.className + " : public CallConfig", "{", "};");
        Offset p(hS, "public:");
        printConstructors(cm);
        printSignature(hS, "Result<" + cm.responseType.name + ">",
                       "parseReply", {}, cm.className);

        hS << "\n" << offset << "Result<" << cm.responseType.name
           << "> parseReply(" << cm.replyFormatVar.toString() << ");" << endl;
        // TODO: dump parseReply() implementation
    }

}

struct appendParam
{
    explicit appendParam(const char* paramString) : _s(paramString) { }

    const char* _s;
};

WrappedLine& operator<<(WrappedLine& lw, const appendParam& ap)
{
    return lw << "," << soft_endl(" ") << ap._s;
}

inline string dumpName(const std::string& name)
{
    return '"' + name + "\", " + name;
}

void Printer::printConstructors(const CallClass& cm, const string& ns)
{
    bool asFunction = !ns.empty();

    bool unsupportedCalls = false;
    for (const auto& call: cm.callOverloads)
    {
        cppS << endl;
        Call::params_type allParams = call.collateParams();
        const char* const returnType = asFunction ? "CallConfigNoReplyBody" : "";
        printSignature(hS, returnType, cm.className, allParams);
        printSignature(cppS, returnType, cm.className, allParams,
                       asFunction ? ns : cm.className);
        if (asFunction)
        {
            Scope body(cppS, "{", "}");
            printParamInitializers(call, false);
            WrappedLine lw { cppS };
            lw << "return " << returnType << "(" << soft_endl();
            printInitializer(lw, cm.className, call);
            if (!(call.queryParams.empty() && call.bodyParams.empty() && call.needsToken))
            {
                lw << appendParam(call.queryParams.empty() ? "Query()" : "q");
                if (!(call.bodyParams.empty() && call.needsToken))
                {
                    lw << appendParam(call.bodyParams.empty() ? "Data()" : "d");
                    if (!call.needsToken)
                        lw << appendParam("false");
                }
            }
            lw << ");";
        }
        else
        {
            {
                WrappedLine lw { cppS };
                Offset initializerOffset { cppS };
                lw << offset << ": CallConfig(" << soft_endl();
                printInitializer(lw, cm.className, call);
                cppS << offset << (call.needsToken ? ")" : ", false )") << '\n';
            }
            Scope body(cppS, "{", "}");
            printParamInitializers(call, true);
        }

        if (!call.headerParams.empty())
            unsupportedCalls = true;
    }
    if (unsupportedCalls)
    {
        cerr << "Warning: " << cm.className << " has one or more "
                "call overloads with parameters in HTTP headers; "
                "these are not supported at the moment" << endl;
    }
}

void Printer::printParamInitializers(const Call& call, bool withSetters)
{
    if (!call.queryParams.empty())
    {
        cppS << offset << "Query q;\n";
        for (const auto& qp: call.queryParams)
            cppS << offset << "q.addQueryItem(" << dumpName(qp.name) << ");\n";
        if (withSetters)
            cppS << offset << "setQuery(q);\n";
    }
    if (!call.bodyParams.empty())
    {
        cppS << offset << "Data d;\n";
        for (const auto& bp: call.bodyParams)
            cppS << offset << "d.insert(" << dumpName(bp.name) << ");\n";
        if (withSetters)
            cppS << offset << "setData(d);\n";
    }
}

void Printer::printInitializer(WrappedLine& lw, const std::string& callName,
                               const Call& callOverload)
{
    lw << '"' << callName << "\"," << soft_endl(" ")
       << "HttpVerb::" << callOverload.verb << ',' << soft_endl(" ");

    const auto& path = callOverload.path;
    if (path.find('{') == string::npos)
    {
        lw << '"' << path << '"';
        return;
    }

    lw << "ApiPath(\"";
    for (string::size_type i = 0; i < path.size();)
    {
        auto i1 = path.find('{', i);
        auto i2 = path.find('}', i1);
        if (i1 == string::npos)
        {
            lw << path.substr(i) << "\"";
            break;
        }
        if (i2 == string::npos)
            fail(UnbalancedBracesInPath, "The path has '{' without matching '}'");

        lw << path.substr(i, i1 - i) << "\" % "
           << path.substr(i1 + 1, i2 - i1 - 1);
        if (i2 != path.size() - 1)
            lw << " % \"";
        i = i2 + 1;
    }
    lw << ")";
}

