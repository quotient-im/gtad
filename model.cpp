#include "model.h"

#include <iostream>
#include <locale>
#include <regex>

#include "exception.h"
#include "scope.h"

enum {
    CannotResolveClassName = InternalErrors,
    NoCallOverloadsForClass, ConflictingOverloads,
};

using namespace std;

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
        return "ChangeAccountPassword";
    if (path == "/account/deactivate")
        return "DeactivateAccount";
    if (path == "/pushers/set")
        return "SetPusher";
    if (path == "/sync")
        return "Sync";
    if (path.find("/initialSync") != string::npos)
    {
        cerr << "Warning: initialSync endpoints are deprecated" << endl;
        return "InitialSync";
    }
    if (regex_match(path, makeRegex("/join/{}")))
        return "JoinByAlias";
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
        return "GetCas" + capitalizedCopy(m[1]);

    // [...]/smth1/{}/{txnId} -> Smth1Event
    //     /rooms/{id}/redact/{}/{txnId} -> RedactEvent
    //     /rooms/{id}/send/{}/{txnId} -> SendEvent
    if (regex_search(path, m, makeRegex("/#/{}/\\{txnId\\}")))
        return capitalizedCopy(m[1]) + "Event";

    // The following conversions will use altered verbs
    string adjustedVerb = capitalizedCopy(verb);

    // /smth1/smth2[/{}] -> VerbSmth1Smth2
    //     /presence/list/{userId}, get|post -> Get|PostPresenceList
    //     /account/3pid|password, get|post -> Get|PostAccount3pid|Password
    if (regex_search(path, m, makeRegex("^/#/#(?:/{})?")))
        return adjustedVerb + capitalizedCopy(m[1]) + capitalizedCopy(m[2]);

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

    // /smth[s/[{}]] - note non-greedy matching before the smth's "s"
    //   all -> VerbSmth:
    //     /upload, /createRoom, /register -> Upload, CreateRoom, Register
    //     /devices, /publicRooms -> GetDevices, GetPublicRooms
    //     /devices/{deviceId} -> Get|Set|DeleteDevice
    if (regex_match(path, m, makeRegex("/#?(s?/{})?")))
        return adjustedVerb + capitalizedCopy(m[1]);

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

using namespace CppPrinting;

DataModel::DataModel(const string& typeName)
    : name(convertMultiword(typeName))
{ }

void DataModel::printTo(ostream& s) const
{
    s << offset << "struct " << name;
    Scope _scope(s, "{", "};");
}

const char* const ParamDecl::in_str[] =
    { "!Undefined!", "path", "query", "header", "body" };

ParamDecl::In ParamDecl::inFromStr(const string& s)
{
    for (auto it = begin(in_str); it != end(in_str); ++it)
        if (s == *it)
            return In(it - begin(in_str));
    return Undefined;
}

CallConfigModel::CallConfigModel(const Model& parent,
                                 const string& callName,
                                 const string& responseTypeName,
                                 const string& replyFormatType)
    : topModel(parent), className(callName)
    , replyFormatVar(replyFormatType, "reply")
    , responseType(responseTypeName)
{ }

// Supported signatures:
// in .h:
//retval name
//className
//
// in .cpp:
//retval nsName::className
//retval className::name
//className::name

void printSignature(ostream& s, const string& retType,
                    const string& name,
                    const vector<ParamDecl>& params,
                    const string& qualifier = {})
{
    static const Scope::size_type WRAPMARGIN = 80;

    ostringstream lineS;
    Scope::setOffset(lineS, Scope::getOffset(s));
    {
        bool header = qualifier.empty();
        string leader =
                (retType.empty() ? "" : retType + " ") +
                (header ? "" : qualifier + "::") + name + "(";

        Scope o(lineS, leader, header ? ");" : ")", Scope::NoNewLines);
        for (auto p = params.begin(); p != params.end(); ++p)
        {
            string param = p->toString(/*header*/);
            if (p != params.begin())
            {
                // Wrap lines
                if (lineS.tellp() <= WRAPMARGIN - param.size())
                    lineS << ", ";
                else
                {
                    s << lineS.str() << ",\n";
                    lineS.str({}); // Clear the string under lineS
                    lineS << offset;
                }
            }
            lineS << param;
        }
    }
    s << lineS.str();
}

void CallConfigModel::printSignatures(ostream& hS,
                                      ostream& cppS,
                                      const vector<ParamDecl>& params,
                                      const string& returnType) const
{
    printSignature(hS, returnType, className, params);
    printSignature(cppS, returnType, className, params,
                   responseType.name.empty() ? topModel.nsName : className);
}

void CallConfigModel::printBody(ostream& s, const CallOverload& call,
                                bool asFunction) const
{
    // Instead of having a full-fledged class that only has a constructor
    // (or several) but no parseReply(), generate a function that
    // returns a CallConfig. This also allows to nicely avoid a problem
    // with Invite calls, which are defined twice in different Swagger files
    // but have different sets of parameters.
    Scope body(s, asFunction ? "{" : ": ", asFunction ? "}" : "", asFunction);
    Scope initializer(s, asFunction ? "return {" : "CallConfig(",
                      asFunction ? "};" : ")", Scope::NoNewLines);
    s << "\"" << className << "\", HttpVerb::" << call.verb << ",\n"
      << offset << "ApiPath(" << call.quotedPath << ") ";
}

void CallConfigModel::printOverloads(ostream& hS, ostream& cppS) const
{
    bool unsupportedCalls = false;

    for (auto call: callOverloads)
    {
        cppS << endl;
        printSignatures(hS, cppS, call.params, "CallConfigNoReplyBody");
        printBody(cppS, call, responseType.name.empty());

        for (auto p: call.params)
            if (p.in != ParamDecl::Path)
                unsupportedCalls = true;
    }
    if (unsupportedCalls)
    {
        cerr << "Warning: " << className << " has one or more "
                "call overloads with parameters outside the path; "
                "these are not supported at the moment" << endl;
    }

}

void CallConfigModel::printTo(ostream& hText, ostream& cppText) const
{
    if (callOverloads.empty())
    {
        cerr << "Couldn't find any parameter set for the call "
             << className << std::endl;
        fail(NoCallOverloadsForClass);
    }

    if (!responseType.fields.empty())
    {
        // Complex response type; define the structure(s) before the class
        hText << offset << "struct " << responseType.name << "\n";
        Scope sdef(hText, "{", "};");
        for (const auto& field: responseType.fields)
            hText << offset << field.toString() << ";\n";
    }

    if (responseType.name.empty())
        printOverloads(hText, cppText);
    else
    {
        hText << offset << "class " << className << " : public CallConfig\n";
        Scope cl(hText, "{", "};");
        Scope p(hText, "public:");
        printOverloads(hText, cppText);
        printSignature(hText, "Result<" + responseType.name + ">",
                       "parseReply", {}, className);

        hText << "\n" << offset << "Result<" << responseType.name
              << "> parseReply(" << replyFormatVar.toString() << ");" << endl;
        // TODO: dump parseReply() implementation
    }

}

CallOverload& Model::addCall(const string& path, const string& verb,
                             const string& responseTypename)
{
    string className = makeClassName(path, verb);
    if (callModels.empty() || className != callModels.back().className)
    {
        if (!callModels.empty() &&
                callModels.back().responseType.name != responseTypename)
            fail(ConflictingOverloads, "Call overloads return different types");

        callModels.emplace_back(*this, className, responseTypename);
    }

    auto& cm = callModels.back();

    cm.callOverloads.emplace_back();
    CallOverload& call = cm.callOverloads.back();
    call.verb = capitalizedCopy(verb);
    call.quotedPath = "\"" +
            regex_replace(path, makeRegex("\\{#\\}"), "\" % $1 % \"") + "\"";
    // Remove an excess ' % ""' sequence in case when the last character
    // in path is a closing brace
    if (call.quotedPath[call.quotedPath.size() - 4] == '%')
        call.quotedPath.erase(call.quotedPath.size() - 5);
    return call;
}
