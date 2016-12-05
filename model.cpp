#include "model.h"

#include <iostream>

using std::string;
using std::cerr;
using std::cout;
//using std::endl; // Beware, Qt has its own endl

#include <QtCore/QTextStream>

#include "exception.h"
#include "scope.h"

using namespace CppPrinting;

void DataModel::printTo(QTextStream& s)
{
    s << offset << "struct " << name;
    Scope _scope(s, "{", "};");
}

void CallConfigModel::printFunctionSignature(QTextStream& s,
                   const QString& retType, const QString& name,
                   const CallConfigModel::CallOverload& call, bool header) const
{
    QString line; QTextStream lineS(&line);
    Scope::setOffset(lineS, Scope::getOffset(s));
    {
        QString qualifiedName = (header || !retType.isEmpty() ? name : className % "::" % name);
        QString retvalAndName =
                (retType.isEmpty() ? qualifiedName : retType % " " % qualifiedName);

        Scope o(lineS, retvalAndName + "(", header ? ");" : ")", Scope::NoNewLines);
        for (auto p = call.params.begin(); p != call.params.end(); ++p)
        {
            QString param = p->toString();
            if (p != call.params.begin())
            {
                if (line.size() + param.size() <= 79)
                    lineS << ", ";
                else
                {
                    s << line << ",\n";
                    line.clear();
                    lineS << offset;
                }
            }
            lineS << param;
        }
    }
    s << line;
}

void CallConfigModel::printTo(QTextStream& hText, QTextStream& cppText)
{
    if (callOverloads.empty())
    {
        cerr << "Couldn't find any parameter set for the call "
             << className.toStdString() << std::endl;
        fail(InternalError);
    }

    if (responseType.name.isEmpty())
    {
        // Instead of having a full-fledged class that only has a constructor
        // (or several) but no parseReply(), generate a function that
        // returns a CallConfig. This also allows to nicely avoid a problem
        // with Invite calls, which are defined twice in different Swagger files
        // but have different sets of parameters.
        for (auto call: callOverloads)
        {
            printFunctionSignature(hText, "CallConfig", className, call);
            printFunctionSignature(cppText, "CallConfig", className, call, false);
            Scope fnBody(cppText, "{", "}");
            Scope callconfigInitializer(cppText, "return { ", "};", Scope::NoNewLines);
            cppText << "\"" << className << "\", HttpVerb::" << call.verb << ",\n"
                    << offset << "\"" << call.path << "\" ";
        }

        return;
    }

    if (!responseType.fields.empty())
    {
        // Complex response type; define the structure(s) before the class
        hText << offset << "struct " << responseType.name << "\n";
        Scope sdef(hText, "{", "};");
        for (auto field: responseType.fields)
            hText << offset << field.toString() << ";\n";
    }
    hText << offset << "class " << className << " : public CallConfig\n";
    Scope cl(hText, "{", "};");
    Scope p(hText, "public:");

    for (auto constructor: callOverloads)
    {
        printFunctionSignature(hText, {}, className, constructor);
        printFunctionSignature(cppText, {}, className, constructor, false);
        {
            Scope semicolonOffset(cppText, "", "", Scope::NoNewLines);
            cppText << offset << ": ";
//            printCallConfigInitializer(cppText, constructor);
        }
    }

    hText << "\n" << offset << "Result<" << responseType.name
          << "> parseReply(" << replyFormatVar.toString()
          << ");\n";

    // TODO: dump the cpp file
}
