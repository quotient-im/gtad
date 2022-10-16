/******************************************************************************
 * Copyright (C) 2016 Kitsune Ral <kitsune-ral@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "analyzer.h"
#include "printer.h"
#include "translator.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Quotient");
    QCoreApplication::setApplicationName("GTAD");
    QCoreApplication::setApplicationVersion("0.9");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main",
        "Matrix API source files generator"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configPathOption("config",
        QCoreApplication::translate("main", "API generator configuration in YAML format"),
        "configfile");
    parser.addOption(configPathOption);

    QCommandLineOption outputDirOption("out",
        QCoreApplication::translate("main", "Write generated files to <outputdir>."),
        "outputdir");
    parser.addOption(outputDirOption);

    QCommandLineOption schemaRoleOption("role",
        QCoreApplication::translate("main",
            "For JSON Schema, generate code assuming <role>, one of:"
            " i (input), o (output); all other values mean both directions"),
        "role", "io");
    parser.addOption(schemaRoleOption);

    QCommandLineOption messagesRoleOption("messages",
        QCoreApplication::translate("main",
            "Configure the verbosity, one of: quiet, basic, and debug"),
        "verbosity", "basic");

    parser.addPositionalArgument("files",
        QCoreApplication::translate("main",
            "Files or directories with API definition in Swagger format."
            " Append a hyphen to exclude a file/directory."),
        "files...");

    parser.process(app);

    try {
        using namespace std;
        namespace fs = filesystem;

        const auto& verbosityArg = parser.value(messagesRoleOption);
        const auto verbosity = verbosityArg == "quiet"   ? Verbosity::Quiet
                               : verbosityArg == "debug" ? Verbosity::Debug
                                                         : Verbosity::Basic;
        Translator translator {parser.value(configPathOption).toStdString(),
                               parser.value(outputDirOption).toStdString(),
                               verbosity};

        vector<fs::path> paths, exclusions;
        const auto& pathArgs = parser.positionalArguments();
        for (const auto& path: pathArgs) {
            if (path.endsWith('-'))
                exclusions.emplace_back(path.left(path.size() - 1).toStdString());
            else
                paths.emplace_back(path.toStdString());
        }
        const auto& roleValue = parser.value(schemaRoleOption);
        const auto role =
            roleValue == "i" ? OnlyIn : roleValue == "o" ? OnlyOut : InAndOut;

        for(const auto& path: paths) {
            auto ftype = fs::status(path).type();
            if (ftype == fs::file_type::regular)
                Analyzer{translator, path.parent_path()}.loadModel(path.filename().string(), role);

            if (ftype != fs::file_type::directory)
                continue;

            Analyzer a{translator, path};
            for (const auto& f: fs::directory_iterator(
                     path, fs::directory_options::skip_permission_denied)) {
                if (!f.is_regular_file())
                    continue;
                auto&& fName = f.path().filename();
                if (find(exclusions.begin(), exclusions.end(), fName)
                    == exclusions.cend())
                    a.loadModel(fName.string(), role);
            }
        }

        using namespace literals;
        const char* clangFormatPath = getenv("CLANG_FORMAT");
        string clangFormatCommand {clangFormatPath ? clangFormatPath
                                                   : "clang-format"sv};
        clangFormatCommand += " -i -sort-includes"sv;
        const char* clangFormatArgs = getenv("CLANG_FORMAT_ARGS");
        if (clangFormatArgs)
            clangFormatCommand += clangFormatArgs;

        size_t filesCounter = 0;
        for (const auto& [pathBase, model]: Analyzer::allModels()) {
            if (model.empty() || model.trivial())
                continue;

            auto targetDir = (translator.outputBaseDir() / pathBase)
                                 .parent_path()
                                 .lexically_normal();
            fs::create_directories(targetDir);
            if (!fs::exists(targetDir))
                throw Exception{"Cannot create output directory "
                                + targetDir.string()};
            const auto fileNames = translator.printer().print(pathBase, model);
            for (const auto& fName : fileNames)
                clangFormatCommand += ' ' + fName;
            filesCounter += fileNames.size();
        }
        cout << "Formatting " << filesCounter << " files\n";
        system(clangFormatCommand.c_str());
    }
    catch (Exception& e)
    {
        std::cerr << e.message << std::endl;
        return 3;
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 3;
    }

    return 0;
}
