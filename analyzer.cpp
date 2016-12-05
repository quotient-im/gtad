#include "analyzer.h"

#include <iostream>
#include <locale>
#include <regex>

#include <yaml-cpp/yaml.h>

#include <QtCore/QFileInfo>
#include <QtCore/QDir>

#include "exception.h"

using namespace std;

enum {
    InfoLevel = AnalyzerCodes, OK = 0, WarningLevel = 0x100, ErrorLevel = 0x200,
    CannotReadFromInput, YamlFailsSchema, CannotResolveClassName,
    UnknownParameterType,
};

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
        pos = s.find_first_of("/_ ", pos);
        if (pos == string::npos)
            break;
        s.erase(pos, 1);
    }
    return QString::fromStdString(s);
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

    cerr << "Couldn't create a class name for path " << path << ", verb: " << verb;
    fail(CannotResolveClassName);
}

using YAML::Node;
using YAML::NodeType;

Node Analyzer::loadYaml() const
{
    try {
        cout << "Loading from " << fileName << endl;
        return YAML::LoadFile(fileName);
    }
    catch (YAML::BadFile &)
    {
        fail(CannotReadFromInput, "Couldn't read YAML from input");
    }
}

static const char* typenames[] = { "Undefined", "Null", "Scalar", "Sequence", "Map" };

const Node& Analyzer::assert(const Node& node, NodeType::value checkedType) const
{
    if (node && node.Type() == checkedType)
        return node;

    cerr << fileName << ":" << node.Mark().line + 1 << ": the node ";
    if (node)
        cerr << "has a wrong type (expected "
             << typenames[checkedType] << ", got " << typenames[node.Type()] << endl;
    else
        cerr << "is undefined" << endl;
    fail(YamlFailsSchema);
}

Node Analyzer::get(const Node& node, const string& subnodeName,
                      NodeType::value checkedType) const
{
    if (node)
    {
        YAML::Node subnode = node[subnodeName];
        if (subnode && subnode.Type() == checkedType)
            return subnode;

        cerr << fileName << ":" << node.Mark().line + 1 << ": " << subnodeName;
        if (subnode)
            cerr << " has a wrong type (expected " << typenames[checkedType]
                 << ", got " << typenames[subnode.Type()] << endl;
        else
            cerr << " is undefined" << endl;
    }
    else
        cerr << fileName
             << ": checkNode() on undefined parent node, check your parsing code"
             << endl;
    fail(YamlFailsSchema);
}

pair<QString, QString> Analyzer::getTypename(const Node& node) const
{
    if (node["$ref"])
    {
        // The referenced file's path is relative to the current file's path;
        // we have to prepend a path to the current file's directory so that
        // YAML-Cpp finds the file.
        QFileInfo currentFileInfo { QString::fromStdString(fileName) };
        string currentFileDirPath = currentFileInfo.dir().path().toStdString() + "/";
        string localFilePath = getString(node, "$ref");

        Model m = Analyzer(currentFileDirPath + localFilePath).getModel();
        if (m.dataModels.empty())
        {
            cerr << "File " << localFilePath
                 << " doesn't have data definitions" << endl;
            fail(YamlFailsSchema);
        }
        if (m.dataModels.size() > 1)
        {
            cerr << "File " << localFilePath
                 << " has more than one data structure definition" << endl;
            fail(YamlFailsSchema);
        }

        QString qFilePath = dropYamlExtension(QString::fromStdString(localFilePath));
        return { m.dataModels.back().name, "\"" % qFilePath % ".h\"" };
    }

    string yamlType = getString(node, "type");
    if (yamlType == "array")
    {
        auto arrayElType = get(node, "items", NodeType::Map);

        auto innerType = getString(arrayElType, "type");
        if (innerType == "string")
            return { "QStringList", "<QtCore/QStringList>" };
        // TODO: items can have [properties]; we'll have to create a separate struct
        // to describe such type
    }
    pair<QString,QString> retval =
            yamlType == "string" ? make_pair("QString", "") :
            yamlType == "integer" || yamlType == "number" ? make_pair("int", "") :
            yamlType == "boolean" ? make_pair("bool", "") :
            yamlType == "array" ? make_pair("QVariantList", "<QtCore/QVariantList>") :
            yamlType == "object" ? make_pair("QVariant", "<QtCore/QVariant>") :
            make_pair("", "");
    if (!retval.first.isEmpty())
        return retval;

    cerr << fileName << ":" << node.Mark().line + 1 << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

QString Analyzer::dropYamlExtension(QString qFilePath) const
{
    if (qFilePath.endsWith(".yaml", Qt::CaseInsensitive))
        qFilePath.chop(5);
    else if (qFilePath.endsWith(".yml", Qt::CaseInsensitive))
        qFilePath.chop(4);
    return qFilePath;
}

void Analyzer::addParameter(string name, const Node& node, vector<QString>& includes,
                            CallConfigModel::CallOverload& callOverload) const
{
    auto typeDef = getTypename(node);
    callOverload.params.emplace_back(typeDef.first, QString::fromStdString(name));
    cout << "  Added input parameter: "
         << callOverload.params.back().toString().toStdString();

    if (!typeDef.second.isEmpty() &&
            find(includes.begin(), includes.end(), typeDef.second) == includes.end())
    {
        includes.emplace_back(typeDef.second);
        cout << "(with #include " << typeDef.second.toStdString() << ")";
    }
    cout << endl;
}

Model Analyzer::getModel() const
{
    Node yaml = loadYaml();

    Model model;
    // Detect which file we have, with a call, or just with a data definition
    if (auto paths = yaml["paths"])
    {
        assert(paths, NodeType::Map);

        auto produces = yaml["produces"];
        bool allCallsReturnJson = produces.size() == 1 &&
                                  produces.begin()->as<string>() ==
                                  "application/json";

        for (auto yaml_path: paths)
        {
            auto path = yaml_path.first.as<string>();
            while (*path.rbegin() == ' ' || *path.rbegin() == '/')
                path.erase(path.size() - 1);

            assert(yaml_path.second, NodeType::Map);
            for (auto yaml_call_pair: yaml_path.second)
            {
                string verb = yaml_call_pair.first.as<string>();
                {
                    QString className = makeClassName(path, verb);

                    if (model.callModels.empty() ||
                            className != model.callModels.back().className)
                        model.callModels.emplace_back(className);
                }
                auto& cm = model.callModels.back();

                cm.callOverloads.emplace_back();
                auto& cp = cm.callOverloads.back();

                auto yaml_call = assert(yaml_call_pair.second, NodeType::Map);
                {
                    auto s = yaml_call["security"];
                    cp.needsToken =
                            s.IsSequence() && s[0]["accessToken"].IsDefined();
                }
                cp.path = QString::fromStdString(regex_replace(
                        regex_replace(path, regex("\\{"), "\" % "), regex("\\}"),
                        " % \""));
                cp.verb = QString::fromStdString(capitalize(verb));

                cout << path << " - " << verb << endl;

                for (Node yaml_param: yaml_call["parameters"])
                {
                    assert(yaml_param, NodeType::Map);

                    if (yaml_param["type"])
                    {
                        // Got a simple type
                        auto name = getString(yaml_param, "name");
                        addParameter(name, yaml_param, model.includes, cp);
                        continue;
                    }
                    // Got a complex type
                    auto schema = get(yaml_param, "schema", NodeType::Map);
                    Node properties = schema["properties"];
                    if (!properties)
                    {
                        // Got a complex type without inner schema details
                        auto name = getString(yaml_param, "name");
                        addParameter(name, schema, model.includes, cp);
                        continue;
                    }

                    assert(properties, NodeType::Map);
                    for (auto property: properties)
                    {
                        auto name = property.first.as<string>();
                        addParameter(name, property.second, model.includes, cp);
                    }
                }
            }
        }
    } else {
        assert(yaml, NodeType::Map);
        model.dataModels.emplace_back();
        DataModel& dm = model.dataModels.back();
        dm.name = convertMultiword(
                yaml["title"] ? getString(yaml, "title") :
                dropYamlExtension(QFileInfo(QString::fromStdString(fileName)).fileName()).toStdString());
    }
    return model;
}
