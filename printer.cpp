#include "printer.h"

#include <iostream>
#include <fstream>

#include "exception.h"
#include "scope.h"

enum {
    CannotWriteToFile = PrinterCodes,
};

using namespace std;

Printer::Printer(const string& baseDirPath, const string& filenameBase)
    : baseDir(baseDirPath), filenameBase(filenameBase)
{ }

void Printer::print(const Model& model) const
{
    string hFilename = filenameBase + ".h";
    ofstream hS(baseDir + hFilename, ios::out|ios::trunc);
    if (!hS.good())
    {
        cerr << "Couldn't open " << baseDir + hFilename << " for writing" << endl;
        fail(CannotWriteToFile);
    }

    string cppFilename = filenameBase + ".cpp";
    ofstream cppS(baseDir + cppFilename, ios::out|ios::trunc);
    if (!cppS.good())
    {
        cerr << "Couldn't open " << baseDir + cppFilename << " for writing" << endl;
        fail(CannotWriteToFile);
    }

    {
        string s = "// This is an auto-generated file; don't edit!\n\n";
        hS << s;
        cppS << s;
    }

    cppS << "#include \"" << hFilename << "\"\n\n";

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
    CppPrinting::Scope ns(hS, model.nsName, "::", "namespace ", "{", "}");
    cppS << "using namespace " << model.nsName << ";\n";

    for (const auto& dataModel: model.dataModels)
        dataModel.printTo(hS);

    for (const auto& callModel: model.callModels)
        callModel.printTo(hS, cppS);
}
