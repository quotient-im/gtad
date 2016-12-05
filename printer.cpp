#include "printer.h"

#include <memory>

#include <QtCore/QFileInfo>

#include "scope.h"

Printer::Printer(QFile* h, QFile* cpp, const QString& nameSpace)
    : hFile(h), cppFile(cpp), namespaces(nameSpace.split("::"))
{

}

void Printer::print(const Model& model)
{
    QTextStream hS(hFile);
    QTextStream cppS(cppFile);
    {
        QString s = "// This is an auto-generated file; don't edit!\n\n";
        hS << s;
        cppS << s;
    }

    using namespace CppPrinting;

    cppS << "#include \"" << QFileInfo(*hFile).fileName() << "\"\n\n";

    hS << "#pragma once\n\n"
            "#include \"serverapi/servercallsetup.h\"\n\n";

    for (auto header: model.includes)
        hS << "#include " << header << "\n";

    hS << "\n";
    {
        std::vector< std::unique_ptr<Scope> > nsScopes;
        for (auto ns: namespaces)
        {
            hS << "namespace " << ns << "\n";
            nsScopes.emplace_back(new Scope(hS, "{", "}"));
        }

        cppS << "using namespace " << namespaces.join("::") << ";\n\n";

        for (auto dataModel: model.dataModels)
            dataModel.printTo(hS);

        for (auto callModel: model.callModels)
            callModel.printTo(hS, cppS);
    }
}
