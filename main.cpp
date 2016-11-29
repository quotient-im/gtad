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

#include <iostream>
using std::cerr;
using std::string;
#include <regex>

#include "yaml-cpp/yaml.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>

#include <QtCore/QDir>
#include <QtCore/QStringBuilder>

#include "scope.h"

using namespace std;

enum ErrorCode {
    OK = 0,
    CannotCreateOutputDir, CannotWriteToFile, CannotReadFromInput,
    CannotResolveClassName, CannotResolveOverloadParameters,
    YamlParsingError,
};

void stop(ErrorCode retval, const string& msg)
{
    cerr << msg << endl;
    exit(retval);
}

string inputFileName;

static const char* typenames[] = { "Undefined", "Null", "Scalar", "Sequence", "Map" };

const YAML::Node& checkNode(const YAML::Node& node, YAML::NodeType::value checked_type)
{
    if (node && node.Type() == checked_type)
        return node;

    cerr << inputFileName << ":" << node.Mark().line + 1 << ": the node ";
    if (node)
        cerr << "has a wrong type (expected "
             << typenames[checked_type] << ", got " << typenames[node.Type()] << endl;
    else
        cerr << "is undefined" << endl;
    exit(YamlParsingError);
}

YAML::Node checkNode(const YAML::Node& node, const string& subnode_name,
                     YAML::NodeType::value checked_type)
{
    if (node)
    {
        YAML::Node subnode = node[subnode_name];
        if (subnode && subnode.Type() == checked_type)
            return subnode;

        cerr << inputFileName << ":" << node.Mark().line + 1 << ": " << subnode_name;
        if (subnode)
            cerr << " has a wrong type (expected " << typenames[checked_type]
                 << ", got " << typenames[subnode.Type()] << endl;
        else
            cerr << " is undefined" << endl;
    }
    else
        cerr << inputFileName
             << ": checkNode() on undefined parent node, check your parsing code"
             << endl;
    exit(YamlParsingError);
}

template <typename T>
T checkScalar(const YAML::Node& node, const string& subnode_name)
{
    return checkNode(node, subnode_name, YAML::NodeType::Scalar).as<T>();
}

namespace ApiGenerator
{
    struct VariableDefinition
    {
        QString type;
        QString name;

        QString toString() const { return type % " " % name; }
    };

    struct CustomResponseType
    {
        QString name;

        std::vector<VariableDefinition> fields;
    };

    struct CallConfigModel
    {
        QString className;

        struct CallOverload
        {
            vector<VariableDefinition> params;
            bool needsToken;
        };
        std::vector<CallOverload> callOverloads;
        VariableDefinition replyFormatVar;
        CustomResponseType responseType;

        CallConfigModel(const QString& n) : className(n) { }
    };
    std::vector<CallConfigModel> models;

    string& capitalize(string& s, string::size_type pos = 0)
    {
        if (pos < s.size())
            s[pos] = toupper(s[pos], locale("C"));
        return s;
    }

    QString convert(string s)
    {
        return QString::fromStdString(capitalize(s));
    }

    QString convertMultiword(string s)
    {
        string::size_type pos = 0;
        while (pos < s.size())
        {
            capitalize(s, pos);
            pos = s.find_first_of("/_", pos);
            if (pos == string::npos)
                break;
            s.erase(pos, 1);
        }
        return QString::fromStdString(s);
    }

    QString convertType(string s)
    {
        return
            s == "string" ? "QString" :
            s == "integer" || s == "number" ? "int" :
            s == "boolean" ? "bool" :
            s == "stringarray" ? "QStringList" :
            s == "array" ? "QVariantList" :
            s == "object" ? "QVariant" : "";
    }

    regex makeRegex(const string& pattern)
    {
        // Prepare a regex using regexes.
        static const regex braces_re("{}", regex::basic);
        static const regex pound_re("#(\\?)?"); // # with optional ? (non-greediness)
        return regex(
            regex_replace(
                // Escape and expand double-brace to {\w+} ({value} etc.)
                regex_replace(pattern, braces_re, string("\\{\\w+\\}")),
                // Then replace # with word-matching sub-expr; if it's #? then
                // insert ? so that we have a non-greedy matching
                pound_re, string("(\\w+$1)"))
        );
    }

    QString makeClassName(string path, string verb)
    {
        using namespace std;

        // Special cases
        if (path == "/account/password")
            return "ChangeAccountPassword";
        if (path == "/account/deactivate")
            return "DeactivateAccount";
        if (path == "/pushers/set")
            return "SetPusher";
        if (path == "/sync")
            return "Sync";
        if (regex_match(path, makeRegex("/room/{}")))
        {
            if (verb == "get")
                return "ResolveRoom";
            else if (verb == "put")
                return "SetRoomAlias";
        }
        if (regex_match(path, makeRegex("/download/{}/{}(/{})?")))
            return "Download";
        if (regex_match(path, makeRegex("/sendToDevice/{}/{}")))
            return "SendToDevice";
        if (regex_match(path, makeRegex("/admin/whois/{}")))
            return "WhoIs";
        if (regex_match(path, makeRegex("/presence/{}/status")))
            return "SetPresence";
        if (regex_match(path, makeRegex("/rooms/{}/receipt/{}/{}")))
            return "PostReceipt";

        smatch m;

        // /smth1[/smth2]/email/requestToken -> RequestTokenToSmth
        //   /account/3pid/email/requestToken -> RequestTokenToAccount3pid
        //   /register/email/requestToken -> RequestTokenToRegister
        //   /account/password/email/requestToken -> RequestTokenToAccountPassword
        if (regex_match(path, m, makeRegex("/(#(?:/#)?)/email/requestToken")))
            return "RequestTokenTo" + convertMultiword(m[1]);

        // /login/cas/smth -> VerbCasSmth (GetCasTicket|Redirect) (should it be in the API at all?)
        if (regex_search(path, m, makeRegex("^/login/cas/#")))
            return "GetCas" + convert(m[1]);

        // [...]/smth1/{}/{txnId} -> Smth1Event
        //     /rooms/{id}/redact/{}/{txnId} -> RedactEvent
        //     /rooms/{id}/send/{}/{txnId} -> SendEvent
        if (regex_search(path, m, makeRegex("/#/{}/\\{txnId\\}")))
            return convert(m[1]) + "Event";

        // /smth1/smth2[/{}] -> VerbSmth1Smth2
        //     /presence/list/{userId}, get|post -> Get|PostPresenceList
        //     /account/3pid|password, get|post -> Get|PostAccount3pid|Password
        if (regex_search(path, m, makeRegex("^/#/#(?:/{})?")))
            return convert(verb) % convert(m[1]) % convert(m[2]);

        // The following conversions will use altered verbs
        if (verb == "put")
            verb = "set";

        // /user/{val1}/[/smth1.5/{val1.5}]/smth2[s] -> VerbSmth1Smth2s
        // /user/{val1}/[/smth1.5/{val1.5}]/smth2[s]/{val2} -> VerbSmth1Smth2
        //     /user/{id}/rooms/{id}/tags -> GetUserTags
        //     /user/{id}/rooms/{id}/account_data/{} -> SetUserAccountData
        //     /user/{id}/account_data/{} -> SetUserAccountData (overload)
        //     /user/{id}/rooms/{id}/tags/{tag} -> Set|DeleteUserTag
        //     /user/{id}/filter/{id} -> GetUserFilter
        //     /user/{id}/filter -> PostUserFilter
        if (regex_match(path, m, makeRegex("/user/{}(?:/#/{})?/#?(s?/{})?")))
            return convert(verb) % "User" % convertMultiword(m[2]);

        if (verb == "post")
            verb.clear();

        // /smth[s/[{}]] - note non-greedy matching before the smth's "s"
        //   all -> VerbSmth:
        //     /upload, /createRoom, /register -> Upload, CreateRoom, Register
        //     /devices, /publicRooms -> GetDevices, GetPublicRooms
        //     /devices/{deviceId} -> Get|Set|DeleteDevice
        if (regex_match(path, m, makeRegex("/#?(s?/{})?")))
            return convert(verb) + convert(m[1]);

        // /smth1s/{}/{}/[{}[/smth2]] -> VerbSmth1[Smth2]
        //     /thumbnail/{}/{}
        //     /pushrules/{}/{}/{id}[/...] -> Get|Set|DeletePushrule|...
        if (regex_match(path, m, makeRegex("/#?s?/{}/{}(?:/{}(?:/#)?)?")))
            return convert(verb) % convert(m[1]) % convert(m[2]);

        // /smth1/{val1}/smth2[/{val2}[/{}]] -> VerbSmth2
        //   VerbSmth2
        //     /rooms/{id}/invite|join, post; |messages|state, get -> Invite, Join, GetMessages, GetState
        //     /rooms/{id}/smth/{} -> Get|SetSmth
        //     /profile/{}/display_name|avatar_url -> Get|SetDisplayName
        if (regex_match(path, m, makeRegex("/#/{}/#(/{}){0,2}")))
            return convert(verb) + convertMultiword(m[2]);

        cerr << "Cannot created a class name for path " << path << ", verb: " << verb;
        return {};
    }

    void addParam(const string& type, const string& name,
                  vector<VariableDefinition>& params)
    {
        QString qtype = convertType(type);
        if (qtype.isEmpty())
        {
            cerr << inputFileName
                 << ": parameter " << name << " has unknown type: " << type;
            exit(YamlParsingError);
        }
        cout << "  Adding input parameter: "
             << qtype.toStdString() << " " << name << endl;
        params.emplace_back(VariableDefinition{
                qtype, QString::fromStdString(name)
        });
    }

    void fillParams(const YAML::Node& source,
                    vector<VariableDefinition>& params)
    {
        using YAML::NodeType;
        checkNode(source, NodeType::Map);
        auto name = checkScalar<string>(source, "name");
        YAML::Node typeNode;
        if (source["type"])
            typeNode = source["type"];
        else
        {
            YAML::Node schema = checkNode(source, "schema", NodeType::Map);
            YAML::Node properties = schema["properties"];
            if (properties && properties.IsMap())
            {
                for (auto property: properties)
                {
                    name = property.first.as<string>();
                    checkNode(property.second, NodeType::Map);
                    YAML::Node innerTypeNode = property.second["type"];
                    string typeName;
                    if (innerTypeNode && innerTypeNode.IsScalar())
                    {
                        typeName = innerTypeNode.as<string>();
                        if (typeName == "array")
                        {
                            YAML::Node arrayElType = property.second["items"];
                            if (arrayElType && arrayElType.IsMap())
                            {
                                auto innerType = checkScalar<string>(arrayElType,
                                                                     "type");
                                if (innerType == "string")
                                    typeName = "stringarray";
                            }
                        }
                    } else
                        typeName = "object";
                    addParam(typeName, name, params);
                }
                return;
            }
            typeNode = checkNode(schema["type"], NodeType::Scalar);
        }
        addParam(typeNode.as<string>(), name, params);
    }
}

int main( int argc, char* argv[] )
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Quaternion");
    QCoreApplication::setApplicationName("qmatrixclient-api-generator");
    QCoreApplication::setApplicationVersion("0.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main",
        "Client-server API source files generator for libqmatrixclient"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption inputFileOption("in",
        QCoreApplication::translate("main", "File with API definition in Swagger format."),
        "inputfile");
    parser.addOption(inputFileOption);

    QCommandLineOption outputDirOption("out",
        QCoreApplication::translate("main", "Write generated files to <outputdir>."),
        "outputdir");
    parser.addOption(outputDirOption);

    parser.process(app);

    QDir outputDir { parser.value(outputDirOption) };
    if (!outputDir.exists() && !outputDir.mkpath("."))
        stop(CannotCreateOutputDir, "Cannot create output directory");

    QFileInfo inputFileInfo { parser.value(inputFileOption) };
    inputFileName = inputFileInfo.filePath().toStdString();

    QString bareFilename = inputFileInfo.fileName();
    if (bareFilename.endsWith(".yaml", Qt::CaseInsensitive))
        bareFilename.chop(5);
    else if (bareFilename.endsWith(".yml", Qt::CaseInsensitive))
        bareFilename.chop(4);

    QFile hFile(outputDir.absolutePath() % "/" % bareFilename % ".h");
    if (!hFile.open(QIODevice::WriteOnly|QIODevice::Text))
        stop(CannotWriteToFile, "Couldn't open .h file for writing");

    QFile cppFile(outputDir.absolutePath() % "/" % bareFilename % ".cpp");
    if (!cppFile.open(QIODevice::WriteOnly|QIODevice::Text))
        stop(CannotWriteToFile, "Couldn't open .cpp file for writing");

    // Read the input file into the model

    using YAML::Node;
    using YAML::NodeType;
    Node yaml;
    try {
        yaml = YAML::LoadFile(inputFileName);
    }
    catch (YAML::BadFile &e)
    {
        stop(CannotReadFromInput, "Couldn't read YAML from input");
    }

    using namespace ApiGenerator;

    auto produces = yaml["produces"];
    bool allCallsReturnJson = produces.size() == 1 &&
                produces.begin()->as<string>() == "application/json";
    auto paths = checkNode(yaml, "paths", NodeType::Map);
    for (auto yaml_path: paths)
    {
        auto path = yaml_path.first.as<string>();
        while (*path.rbegin() == ' ' || *path.rbegin() == '/')
            path.erase(path.size() - 1);

        checkNode(yaml_path.second, NodeType::Map);
        for (auto yaml_call_pair: yaml_path.second)
        {
            string verb = yaml_call_pair.first.as<string>();
            {
                QString className = makeClassName(path, verb);
                if (className.isEmpty())
                    return CannotResolveClassName;

                if (models.empty() || className != models.back().className)
                    models.emplace_back(className);
            }
            auto& model = models.back();

            model.callOverloads.emplace_back();
            auto& cp = model.callOverloads.back();

            auto yaml_call = checkNode(yaml_call_pair.second, NodeType::Map);
            {
                auto s = yaml_call["security"];
                cp.needsToken = s.IsSequence() && s[0]["accessToken"].IsDefined();
            }

            cout << path << " - " << verb << endl;

            for (auto yaml_param: yaml_call["parameters"])
            {
                fillParams(yaml_param, cp.params);
                // TODO: Fill in constructor parameters
            }
        }
    }

    // Dump the model to the C++ files

    QTextStream hText(&hFile);
    QTextStream cppText(&cppFile);
    {
        QString s = "// This is an auto-generated file; don't edit!\n\n";
        hText << s;
        cppText << s;
    }
    cppText << "#include \"" << bareFilename << ".h\"\n";

    hText << "#pragma once\n\n"
             "#include \"../servercallsetup.h\"\n\n";

    {
        hText << "namespace QMatrixClient\n";
        Scope ns1(hText, "{", "}");
        hText << offset << "namespace ServerApi\n";
        Scope ns2(hText, "{", "}");

        for (auto model: models)
        {
            // A very special case - because we have two identical invite calls in two
            // different YAML files, we create two Invite _function_ overloads (also,
            // in different files) that return plain CallConfigs. Fortunately, these
            // two calls have standard responses so we don't need to generate parseReply
            // for them.
            if (model.className == "Invite")
            {
                if (model.callOverloads.empty())
                {
                    cerr << "Couldn't resolve parameters for Invite" << endl;
//                    return CannotResolveOverloadParameters;
                    continue;
                }
                QString c = Scope::offsetString(hText) % "CallConfig Invite(";
                for (auto param: model.callOverloads.back().params)
                {
                    c += param.toString();
                    // TODO: Got to do something with commas and wrapping
                }
                hText << c << ");\n";
            }
            if (!model.responseType.fields.empty())
            {
                hText << offset << "struct " << model.responseType.name << "\n";
                Scope sdef(hText, "{", "};");
                for (auto field: model.responseType.fields)
                    hText << offset << field.toString() << ";\n";
            }
            hText << offset << "class " << model.className
                  << " : public CallConfig\n";
            Scope cl(hText, "{", "};");
            Scope p(hText, "public:");

            for (auto constructor: model.callOverloads)
            {
                QString c = Scope::offsetString(hText) % model.className % "(";
                for (auto param: constructor.params)
                {
                    c += param.toString() + ", ";
                }
                // FIXME: Got to do something with commas and wrapping
                c += "int = 0";
                hText << c << ");\n";
            }
            if (!model.responseType.name.isEmpty())
            {
                hText << "\n" << offset << "Result<" << model.responseType.name
                      << "> parseReply(" << model.replyFormatVar.toString()
                      << ");\n";
            }

            // TODO: dump the cpp file
        }
    }

    hFile.close();
    cppFile.close();

    return OK;
}
