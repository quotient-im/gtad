#include "model.h"

#include <iostream>
#include <locale>
#include <regex>

#include "exception.h"

enum {
    CannotResolveClassName = InternalErrors,
    ConflictingOverloads, UnknownInValue,
};

using namespace std;

string VarDecl::setupDefault(string type, string defaultValue)
{
    return !defaultValue.empty() ? defaultValue :
        type == "bool" ? "false" :
        type == "int" ? "0" :
        "{}";
}

void capitalize(string& s, string::size_type pos = 0)
{
    if (pos < s.size())
        s[pos] = toupper(s[pos], locale("C"));
}

string capitalizedCopy(string s)
{
    capitalize(s);
    return s;
}

string convertMultiword(string s)
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
    return s;
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

string makeClassName(const string& path, const string& verb)
{
    using namespace std;

    // Special cases
    if (path == "/account/password")
        return "ChangePassword";
    if (path == "/account/deactivate")
        return "DeactivateAccount";
    if (path == "/pushers/set")
        return "PostPusher";
    if (path == "/sync")
        return "Sync";
    if (path == "/publicRooms" && verb == "post")
        return "SearchPublicRooms";
    if (path.find("/initialSync") != string::npos)
    {
        cerr << "Warning: initialSync endpoints are deprecated" << endl;
        return "InitialSync";
    }
    if (regex_match(path, makeRegex("/join/{}")))
        return "JoinByAlias";
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
    if (regex_search(path, regex("/invite$"))) // /rooms/{id}/invite
        return "InviteUser";

    std::smatch m;

    // /smth1[/smth2]/email/requestToken -> RequestTokenToSmth
    //   /account/3pid/email/requestToken -> RequestTokenToAccount3pid
    //   /register/email/requestToken -> RequestTokenToRegister
    //   /account/password/email/requestToken -> RequestTokenToAccountPassword
    if (regex_match(path, m, makeRegex("/(#(?:/#)?)/email/requestToken")))
        return "RequestTokenTo" + convertMultiword(m[1]);

    // /login/cas/smth -> VerbCasSmth (GetCasTicket|Redirect) (should it be in the API at all?)
    if (regex_search(path, m, makeRegex("^/login/cas/#")))
        return "GetCas" + capitalizedCopy(m[1]);

    // [...]/smth1/{}/{txnId} -> Smth1Event
    //     /rooms/{id}/redact/{}/{txnId} -> RedactEvent
    //     /rooms/{id}/send/{}/{txnId} -> SendEvent
    if (regex_search(path, m, makeRegex("/#/{}/\\{txnId\\}")))
        return capitalizedCopy(m[1]) + "Event";

    // The following conversions will use altered verbs
    string adjustedVerb = capitalizedCopy(verb);

    // /smth1/smth2[/{}] -> VerbSmth1Smth2
    //     /presence/list/{userId}, get|post -> Get|ModifyPresenceList
    //     /account/3pid|password, get|post -> Get|PostAccount3pid|Password
    if (regex_search(path, m, makeRegex("^/#/#(?:/{})?")))
    {
        if (m[1] == "presence" && verb == "post")
            return "ModifyPresenceList";
        return adjustedVerb + capitalizedCopy(m[1]) + capitalizedCopy(m[2]);
    }

    if (adjustedVerb == "Put")
        adjustedVerb = "Set";

    // /user/{val1}/[/smth1.5/{val1.5}]/smth2[s] -> VerbSmth1Smth2s
    // /user/{val1}/[/smth1.5/{val1.5}]/smth2[s]/{val2} -> VerbSmth1Smth2
    //     /user/{id}/rooms/{id}/tags -> GetUserTags
    //     /user/{id}/rooms/{id}/account_data/{} -> SetUserAccountData
    //     /user/{id}/account_data/{} -> SetUserAccountData (overload)
    //     /user/{id}/rooms/{id}/tags/{tag} -> Set|DeleteUserTag
    //     /user/{id}/filter/{id} -> GetUserFilter
    //     /user/{id}/filter -> PostUserFilter
    if (regex_match(path, m, makeRegex("/user/{}(?:/#/{})?/#?(s?/{})?")))
        return adjustedVerb + "User" + convertMultiword(m[2]);

    if (adjustedVerb == "Post")
        adjustedVerb.clear();

    if (regex_match(path, makeRegex("/room/{}")))
    {
        if (verb == "get")
            return "GetRoomIdByAlias";
        return adjustedVerb + "RoomAlias";
    }
    // /rooms/{id}/join, post; |messages|state, get -> Join, GetMessages, GetState
    if (regex_match(path, m, makeRegex("/rooms/{}/#")))
        return adjustedVerb + capitalizedCopy(m[1]) + "Room";

    // /smth[s/[{}]] - note non-greedy matching before the smth's "s"
    //   all -> VerbSmth:
    //     /upload, /createRoom, /register -> Upload, CreateRoom, Register
    //     /devices, /publicRooms -> GetDevices, GetPublicRooms
    //     /devices/{deviceId} -> Get|Set|DeleteDevice
    if (regex_match(path, m, makeRegex("/#?(s?/{})?")))
    {
        if (m[1] == "device" && verb == "set")
            return "UpdateDevice";
        return adjustedVerb + capitalizedCopy(m[1]);
    }

    // /smth1s/{}/{}/[{}[/smth2]] -> VerbSmth1[Smth2]
    //     /thumbnail/{}/{}
    //     /pushrules/{}/{}/{id}[/...] -> Get|Set|DeletePushrule|...
    if (regex_match(path, m, makeRegex("/#?s?/{}/{}(?:/{}(?:/#)?)?")))
        return adjustedVerb + capitalizedCopy(m[1]) + capitalizedCopy(m[2]);

    // /smth1/{val1}/smth2[/{val2}[/{}]] -> VerbSmth2
    //   VerbSmth2
    //     /rooms/{id}/invite|join, post; |messages|state, get -> Invite, Join, GetMessages, GetState
    //     /rooms/{id}/smth/{} -> Get|SetSmth
    //     /profile/{}/display_name|avatar_url -> Get|SetDisplayName
    if (regex_match(path, m, makeRegex("/#/{}/#(/{}){0,2}")))
        return adjustedVerb + convertMultiword(m[2]);

    cerr << "Couldn't create a class name for path " << path << ", verb: " << verb;
    fail(CannotResolveClassName);
}

Call& Model::addCall(string path, string verb, string name, bool needsToken,
                     string responseTypename)
{
    string className = makeClassName(path, verb);
    if (className != capitalizedCopy(name))
        cout << "Warning: className/operationId mismatch: "
             << className << " != " << name << endl;
    if (callClasses.empty() || name != callClasses.back().className)
    {
        if (!callClasses.empty() &&
                callClasses.back().responseType.name != responseTypename)
            fail(ConflictingOverloads, "Call overloads return different types");

        callClasses.emplace_back(name, std::move(responseTypename));
    }

    return callClasses.back()
        .addCall(std::move(path), capitalizedCopy(std::move(verb)),
                 std::move(name), needsToken);
}

void Call::addParam(const VarDecl& param, const string& in)
{
    static const char* const map[] { "path", "query", "header", "body" };
    for (params_type::size_type i = 0; i < 4; ++i)
        if (map[i] == in)
        {
            allParams[i].push_back(param);
            cout << "Added input parameter for " << in << ": "
                 << param.toString(true) << endl;
            return;
        }

    cerr << "Parameter " << param.toString()
         << " has unknown 'in' value: "<< in << endl;
    fail(UnknownInValue);
}

Call::params_type Call::collateParams() const
{
    params_type allCollated; allCollated.reserve(paramsTotalSize());
    for (auto c: allParams)
        copy(c.begin(), c.end(), back_inserter(allCollated));
    stable_partition(allCollated.begin(), allCollated.end(),
                     mem_fn(&VarDecl::isRequired));
    return allCollated;
}

