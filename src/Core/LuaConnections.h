#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

// Bounded static inspection only. This is not an interpreter or a full Lua parser.
namespace LuaConnections {
struct Token { std::string value; bool literal=false; };
struct Result { std::vector<std::string> categories,guids; size_t unresolved=0; };
inline std::vector<Token> Tokens(const std::string& source) {
    std::vector<Token> out;
    auto longEnd=[&](size_t pos)->size_t {
        if(pos>=source.size() || source[pos]!='[') return pos;
        size_t n=pos+1; while(n<source.size() && source[n]=='=') ++n;
        if(n>=source.size() || source[n]!='[') return pos;
        auto end=source.find("]"+std::string(n-pos-1,'=')+"]",n+1);
        return end==std::string::npos ? source.size() : end+n-pos+1;
    };
    for(size_t i=0;i<source.size() && out.size()<250000;) {
        if(std::isspace(static_cast<unsigned char>(source[i]))) {++i;continue;}
        if(source.compare(i,2,"--")==0) {
            auto end=longEnd(i+2);
            if(end!=i+2) i=end;
            else {auto nl=source.find('\n',i+2);i=nl==std::string::npos ? source.size() : nl+1;}
            continue;
        }
        const char c=source[i];
        if(c=='\'' || c=='"') {
            std::string value; ++i;
            while(i<source.size() && source[i]!=c) {
                if(source[i]=='\\' && i+1<source.size()) {
                    ++i; char e=source[i++];
                    value+=e=='n' ? '\n' : e=='r' ? '\r' : e=='t' ? '\t' : e;
                } else value+=source[i++];
            }
            if(i<source.size()) ++i;
            out.push_back({value,true});continue;
        }
        auto end=longEnd(i);
        if(end!=i) {
            size_t n=i+1;while(n<source.size() && source[n]=='=') ++n;
            const size_t delimiter=n-i+1;
            out.push_back({source.substr(n+1,end>=n+1+delimiter ? end-(n+1)-delimiter : 0),true});i=end;continue;
        }
        if(std::isalnum(static_cast<unsigned char>(c)) || c=='_') {
            size_t start=i++;
            while(i<source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i]=='_')) ++i;
            out.push_back({source.substr(start,i-start),false});continue;
        }
        if(source.compare(i,2,"..")==0) {out.push_back({"..",false});i+=2;}
        else {out.push_back({std::string(1,c),false});++i;}
    }
    return out;
}
inline Result Scan(const std::string& source) {
    auto t=Tokens(source);Result result;
    auto is=[&](size_t i,const char* v){return i<t.size() && !t[i].literal && t[i].value==v;};
    // Resolve finite table-backed return expressions: prefix .. row.field .. separator .. index.
    std::unordered_map<std::string,std::vector<std::string>> functions;
    size_t inspectedFunctions=0;
    for(size_t f=0;f<t.size();++f) if(is(f,"function") && inspectedFunctions++<32) {
        size_t n=f+1;std::string name;
        while(n<t.size() && !is(n,"(")) {name+=t[n++].value;if(n-f>12) break;}
        if(!is(n,"(")) continue;
        size_t limit=std::min(t.size(),n+256);
        for(size_t r=n+1;r<limit;++r) {
            if(is(r,"function")) break;
            if(!is(r,"return") || r+10>=t.size() || !t[r+1].literal || !is(r+2,"..") ||
                !is(r+4,".") || !is(r+6,"..") || !t[r+7].literal || !is(r+8,"..")) continue;
            const auto& row=t[r+3].value;const auto& field=t[r+5].value;const auto& index=t[r+9].value;
            std::string table;
            for(size_t a=n;a+5<r;++a)
                if(t[a].value==row && is(a+1,"=") && is(a+3,"[") && t[a+4].value==index && is(a+5,"]")) table=t[a+2].value;
            if(table.empty()) continue;
            for(size_t a=0;a+3<t.size();++a) if(t[a].value==table && is(a+1,"=") && is(a+2,"{")) {
                int depth=1;
                for(size_t b=a+3;b<t.size() && depth>0;++b) {
                    if(depth==1 && is(b,"[") && b+5<t.size() && is(b+2,"]") && is(b+3,"=") && is(b+4,"{")) {
                        const auto key=t[b+1].value;
                        for(size_t c=b+5;c<t.size() && c<b+128 && !is(c,"}");++c)
                            if(t[c].value==field && is(c+1,"=") && c+2<t.size() && t[c+2].literal)
                                functions[name].push_back(t[r+1].value+t[c+2].value+t[r+7].value+key);
                    }
                    if(is(b,"{")) ++depth;else if(is(b,"}")) --depth;
                }
            }
        }
    }
    for(size_t i=1;i+3<t.size();++i) {
        const bool category=is(i,"CategoryByType"),guid=is(i,"PathableByGuid") || is(i,"PathablesByGuid");
        if((!category && !guid) || (!is(i-1,":") && !is(i-1,".")) || !is(i+1,"(")) continue;
        if(i<2 || (!is(i-2,"World") && !is(i-2,"Packs"))) continue;
        std::vector<std::string> values;
        if(t[i+2].literal && is(i+3,")")) values.push_back(t[i+2].value);
        else if(is(i+3,")")) {
            const auto variable=t[i+2].value;
            // Follow a local value or a call to one of the finite-return functions above.
            for(size_t a=i;a-->0;) if(t[a].value==variable && is(a+1,"=")) {
                if(a+2<t.size() && t[a+2].literal) values.push_back(t[a+2].value);
                else {
                    size_t b=a+2;std::string name;
                    while(b<i && !is(b,"(")) {name+=t[b++].value;if(b-a>12) break;}
                    if(auto fn=functions.find(name);fn!=functions.end()) values=fn->second;
                }
                break;
            }
        }
        if(values.empty()) ++result.unresolved;
        else for(auto& value:values) if(!value.empty()) (category ? result.categories : result.guids).push_back(value);
    }
    return result;
}
}
