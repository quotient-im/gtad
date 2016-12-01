#include "generator.h"

#include "swagger_analyzer.h"
#include "scope.h"

#include <iostream>
#include <regex>

#include <yaml-cpp/yaml.h>

#include <QtCore/QStringBuilder>

using namespace std;

enum ErrorCode {
    OK = 0,
    CannotCreateOutputDir, CannotWriteToFile, CannotReadFromInput,
    CannotResolveClassName, CannotResolveOverloadParameters,
    YamlParsingError = SwaggerAnalyzer::Error,
};

[[noreturn]] void stop(ErrorCode retval, const string& msg)
{
    cerr << msg << endl;
    exit(retval);
}

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

using std::regex;
using std::regex_replace;
using std::regex_match;

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

    std::smatch m;

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

void addParam(const QString& type, const string& name,
              vector<VariableDefinition>& params)
{
    cout << "  Adding input parameter: "
         << type.toStdString() << " " << name << endl;
    params.emplace_back(VariableDefinition{
            type, QString::fromStdString(name)
    });
}

ApiGenerator::ApiGenerator(const QString& outputDirPath)
    : outputDir { outputDirPath }
{
    if (!outputDir.exists() && !outputDir.mkpath("."))
        stop(CannotCreateOutputDir, "Cannot create output directory");
}

void printFunction(QTextStream& s, const QString& rettype, const QString& name,
                   const CallConfigModel::CallOverload& call, bool declare = true)
{
    using namespace CppPrinting;
    
    QString line; QTextStream lineS(&line);
    Scope::setOffset(lineS, Scope::getOffset(s));
    lineS << offset;
    if (!rettype.isEmpty())
        lineS << rettype << " ";
    Scope o(lineS, (rettype.isEmpty() ? name : rettype % " " % name) % "(", ")",
            Scope::NoNewLines);
    for (auto p = call.params.begin(); p != call.params.end(); ++p)
    {
        if (p != call.params.begin())
            lineS << ',';
        
        QString param = p->toString();
        if (line.size() + param.size() <= 79)
            lineS << " " + param;
        else
        {
            s << line << "\n";
            line.clear();
            lineS << offset;
        }
    }

    s << line;
}

void ApiGenerator::operator()(const QString& filePath) const
{
    QFileInfo inputFileInfo { filePath };

    if (inputFileInfo.isDir())
    {
        QDir inputDir(filePath, {"*.yaml"}, QDir::Name, QDir::Readable|QDir::Files);
        auto filesList = inputDir.entryList();
        for(auto fn:filesList)
            (*this)(inputFileInfo.filePath() + fn);
        return;
    }

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
    using std::string;
    Node yaml;
    try {
        yaml = YAML::LoadFile(filePath.toStdString());
    }
    catch (YAML::BadFile &)
    {
        stop(CannotReadFromInput, "Couldn't read YAML from input");
    }

    std::vector<CallConfigModel> models;
    SwaggerAnalyzer swan(filePath.toStdString());
    auto produces = yaml["produces"];
    bool allCallsReturnJson = produces.size() == 1 &&
                produces.begin()->as<string>() == "application/json";
    auto paths = swan.get(yaml, "paths", NodeType::Map);
    for (auto yaml_path: paths)
    {
        auto path = yaml_path.first.as<string>();
        while (*path.rbegin() == ' ' || *path.rbegin() == '/')
            path.erase(path.size() - 1);

        swan.assert(yaml_path.second, NodeType::Map);
        for (auto yaml_call_pair: yaml_path.second)
        {
            string verb = yaml_call_pair.first.as<string>();
            {
                QString className = makeClassName(path, verb);
                if (className.isEmpty())
                    exit(CannotResolveClassName);

                if (models.empty() || className != models.back().className)
                    models.emplace_back(className);
            }
            auto& model = models.back();

            model.callOverloads.emplace_back();
            auto& cp = model.callOverloads.back();

            auto yaml_call = swan.assert(yaml_call_pair.second, NodeType::Map);
            {
                auto s = yaml_call["security"];
                cp.needsToken = s.IsSequence() && s[0]["accessToken"].IsDefined();
            }

            cout << path << " - " << verb << endl;

            for (auto yaml_param: yaml_call["parameters"])
            {
                swan.assert(yaml_param, NodeType::Map);
                auto name = swan.getString(yaml_param, "name");

                if (yaml_param["type"])
                {
                    // Got a simple type
                    addParam(swan.getTypename(yaml_param), name, cp.params);
                    continue;
                }
                // Got a complex type
                auto schema = swan.get(yaml_param, "schema", NodeType::Map);
                Node properties = schema["properties"];
                if (!properties)
                {
                    // Got a complex type without inner schema details
                    addParam(swan.getTypename(schema), name, cp.params);
                    continue;
                }

                swan.assert(properties, NodeType::Map);
                for (auto property: properties)
                {
                    name = property.first.as<string>();
                    if (property.second["type"])
                        addParam(swan.getTypename(property.second),
                                 name, cp.params);
                    else
                        addParam("QVariant", name, cp.params);
                }
            }
        }
    }

    // Dump the model to the C++ files

    using namespace CppPrinting;

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
            // A very special case - for inviting, there are two calls with
            // identical paths and verbs in two different YAML files. Since
            // having two namesake classes leads to conflicts when trying
            // to #include both files at the same time, we create Invite
            // function overloads (still, in different files) that return plain
            // CallConfigs. Fortunately, these two calls have standard
            // responses so we don't need to generate parseReply for them.
            if (model.className == "Invite")
            {
                if (model.callOverloads.empty())
                    stop (CannotResolveOverloadParameters,
                          "Couldn't resolve parameters for Invite");

                for (auto call: model.callOverloads)
                    printFunction(hText, "CallConfig", "Invite", call);
                QString c = Scope::offsetString(hText) % "CallConfig Invite(";
                for (auto param: model.callOverloads.back().params)
                {
                    c += param.toString();
                    // TODO: Got to do something with commas and wrapping
                }
                hText << c << ");\n";
                continue;
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
}
