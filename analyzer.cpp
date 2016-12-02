#include "analyzer.h"

#include <yaml-cpp/yaml.h>

#include <iostream>
#include <locale>
#include <regex>

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

    cerr << "Couldn't create a class name for path " << path << ", verb: " << verb;
    fail(CannotResolveClassName);
}

using YAML::Node;
using YAML::NodeType;

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

QString Analyzer::getTypename(const Node& node) const
{
    string yamlType = getString(node, "type");
    if (yamlType == "array")
    {
        auto arrayElType = get(node, "items", NodeType::Map);

        auto innerType = getString(arrayElType, "type");
        if (innerType == "string")
            return "QStringList";
        // TODO: items can have [properties], too;
        // we'll have to create a separate struct
        // to describe such type
    }
    QString qtype =
            yamlType == "string" ? "QString" :
            yamlType == "integer" || yamlType == "number" ? "int" :
            yamlType == "boolean" ? "bool" :
            yamlType == "array" ? "QVariantList" :
            yamlType == "object" ? "QVariant" : "";
    if (!qtype.isEmpty())
        return qtype;

    cerr << fileName << ":" << node.Mark().line + 1 << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

std::vector<CallConfigModel> Analyzer::getModels() const
{
    Node yaml;
    try {
        yaml = YAML::LoadFile(fileName);
    }
    catch (YAML::BadFile &)
    {
        fail(CannotReadFromInput, "Couldn't read YAML from input");
    }

    std::vector<CallConfigModel> models;
    auto produces = yaml["produces"];
    bool allCallsReturnJson = produces.size() == 1 &&
                              produces.begin()->as<string>() == "application/json";
    auto paths = get(yaml, "paths", NodeType::Map);
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

                if (models.empty() || className != models.back().className)
                    models.emplace_back(className);
            }
            auto& model = models.back();

            model.callOverloads.emplace_back();
            auto& cp = model.callOverloads.back();

            auto yaml_call = assert(yaml_call_pair.second, NodeType::Map);
            {
                auto s = yaml_call["security"];
                cp.needsToken = s.IsSequence() && s[0]["accessToken"].IsDefined();
            }

            cout << path << " - " << verb << endl;

            for (auto yaml_param: yaml_call["parameters"])
            {
                assert(yaml_param, NodeType::Map);
                auto name = getString(yaml_param, "name");

                if (yaml_param["type"])
                {
                    // Got a simple type
                    cp.addParam(getTypename(yaml_param), name);
                    continue;
                }
                // Got a complex type
                auto schema = get(yaml_param, "schema", NodeType::Map);
                Node properties = schema["properties"];
                if (!properties)
                {
                    // Got a complex type without inner schema details
                    cp.addParam(getTypename(schema), name);
                    continue;
                }

                assert(properties, NodeType::Map);
                for (auto property: properties)
                {
                    name = property.first.as<string>();
                    if (property.second["type"])
                        cp.addParam(getTypename(property.second), name);
                    else
                        cp.addParam("QVariant", name);
                }
            }
        }
    }
    return models;
}

