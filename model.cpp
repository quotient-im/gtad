#include "model.h"

#include "exception.h"
#include "scope.h"

#include <iostream>

using std::string;
using std::cerr;
using std::cout;

#include <QtCore/QTextStream>

using namespace CppPrinting;

void CallConfigModel::CallOverload::addParam(const QString& type, const string& name)
{
    params.emplace_back(VariableDefinition{ type, QString::fromStdString(name) });
    cout << "  Added input parameter: "
         << params.back().toString().toStdString() << std::endl;
}

void printFunction(QTextStream& s, const QString& rettype, const QString& name,
                   const CallConfigModel::CallOverload& call, bool declare = true)
{
    QString line; QTextStream lineS(&line);
    Scope::setOffset(lineS, Scope::getOffset(s));

    {
        Scope o(lineS, (rettype.isEmpty() ? name : rettype % " " % name) % "(",
                ");", Scope::NoNewLines);
        for (auto p = call.params.begin(); p != call.params.end(); ++p)
        {
            if (p != call.params.begin())
                lineS << ',';

            QString param = p->toString();
            if (line.size() + param.size() <= 79)
                lineS << " ";
            else
            {
                s << line << "\n";
                line.clear();
                lineS << offset;
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
             << className.toStdString() << endl;
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
            printFunction(hText, "CallConfig", className, call);

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
        printFunction(hText, "", className, constructor);

    hText << "\n" << offset << "Result<" << responseType.name
          << "> parseReply(" << replyFormatVar.toString()
          << ");\n";

    // TODO: dump the cpp file
}
