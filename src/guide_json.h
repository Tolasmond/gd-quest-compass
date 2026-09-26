#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
// Small bounded JSON reader/writer for local guide documents. No executable data.
namespace guidejson {
struct Value {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false; double number = 0;
    std::string string; std::vector<Value> array; std::map<std::string, Value> object;
    Value() = default;
    Value(const char* s) : kind(String), string(s) {}
    Value(std::string s) : kind(String), string(std::move(s)) {}
    Value(double n) : kind(Number), number(n) {}
    Value(unsigned n) : Value(static_cast<double>(n)) {}
    Value(bool b) : kind(Bool), boolean(b) {}
    static Value list() { Value v; v.kind=Array; return v; }
    static Value dict() { Value v; v.kind=Object; return v; }
    Value& operator[](const std::string& key) { if(kind==Null) kind=Object; if(kind!=Object) throw std::runtime_error("Expected object"); return object[key]; }
    const Value& at(const std::string& key) const { auto i=object.find(key); if(kind!=Object || i==object.end()) throw std::runtime_error("Missing field: "+key); return i->second; }
    std::string str() const { if(kind!=String) throw std::runtime_error("Expected string"); return string; }
    double num() const { if(kind!=Number || !std::isfinite(number)) throw std::runtime_error("Expected finite number"); return number; }
    unsigned uid() const { double n=num(); if(n<1 || n>4294967295.0 || std::floor(n)!=n) throw std::runtime_error("Invalid numeric ID"); return static_cast<unsigned>(n); }
};
inline void utf8(std::string& out, unsigned c) {
    if(c<128) out+=static_cast<char>(c);
    else if(c<2048) { out+=static_cast<char>(192|(c>>6)); out+=static_cast<char>(128|(c&63)); }
    else if(c<65536) { out+=static_cast<char>(224|(c>>12)); out+=static_cast<char>(128|((c>>6)&63)); out+=static_cast<char>(128|(c&63)); }
    else { out+=static_cast<char>(240|(c>>18)); out+=static_cast<char>(128|((c>>12)&63)); out+=static_cast<char>(128|((c>>6)&63)); out+=static_cast<char>(128|(c&63)); }
}
struct Parser {
    const std::string& s; size_t p=0, nodes=0;
    void ws() { while(p<s.size() && (s[p]==' '||s[p]=='\n'||s[p]=='\r'||s[p]=='\t')) ++p; }
    char take() { if(p>=s.size()) throw std::runtime_error("Unexpected end of JSON"); return s[p++]; }
    void expect(char c) { if(take()!=c) throw std::runtime_error("Invalid JSON delimiter"); }
    unsigned hex() { unsigned n=0; for(int i=0;i<4;++i) { char c=take(); unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99; if(d>15) throw std::runtime_error("Invalid Unicode escape"); n=n*16+d; } return n; }
    std::string text() {
        expect('"'); std::string out;
        for(;;) { char c=take(); if(c=='"') break; if(static_cast<unsigned char>(c)<32) throw std::runtime_error("Control in string");
            if(c!='\\') out+=c;
            else { c=take(); switch(c) {
            case '"': case '\\': case '/': out+=c; break;
            case 'b': out+='\b'; break; case 'f': out+='\f'; break; case 'n': out+='\n'; break; case 'r': out+='\r'; break; case 't': out+='\t'; break;
            case 'u': { unsigned n=hex(); if(n>=0xD800 && n<=0xDBFF) { expect('\\'); expect('u'); unsigned low=hex(); if(low<0xDC00||low>0xDFFF) throw std::runtime_error("Invalid surrogate"); n=0x10000+((n-0xD800)<<10)+(low-0xDC00); } else if(n>=0xDC00&&n<=0xDFFF) throw std::runtime_error("Invalid surrogate"); utf8(out,n); break; }
            default: throw std::runtime_error("Invalid escape"); }
            }
            if(out.size()>16384) throw std::runtime_error("String too long");
        } return out;
    }
    Value read(unsigned depth=0) {
        if(depth>32 || ++nodes>100000) throw std::runtime_error("JSON limit exceeded"); ws(); if(p>=s.size()) throw std::runtime_error("Empty JSON");
        char c=s[p]; Value v;
        if(c=='"') return Value(text());
        if(c=='{' || c=='[') { ++p; v=c=='{'?Value::dict():Value::list(); ws(); char close=c=='{'?'}':']'; if(p<s.size()&&s[p]==close) { ++p; return v; }
            for(;;) { ws(); if(c=='{') { std::string key=text(); ws(); expect(':'); Value item=read(depth+1); if(!v.object.emplace(key,std::move(item)).second) throw std::runtime_error("Duplicate JSON key"); } else v.array.push_back(read(depth+1)); ws(); char end=take(); if(end==close) break; if(end!=',') throw std::runtime_error("Expected comma"); } return v; }
        for(auto word : {"true","false","null"}) { std::string w(word); if(s.compare(p,w.size(),w)==0) { p+=w.size(); if(w=="null") return v; return Value(w=="true"); } }
        size_t begin=p; if(s[p]=='-') ++p;
        if(p>=s.size()) throw std::runtime_error("Invalid number");
        if(s[p]=='0') ++p; else { if(s[p]<'1'||s[p]>'9') throw std::runtime_error("Invalid number"); while(p<s.size()&&s[p]>='0'&&s[p]<='9') ++p; }
        if(p<s.size()&&s[p]=='.') { ++p; size_t b=p; while(p<s.size()&&s[p]>='0'&&s[p]<='9') ++p; if(p==b) throw std::runtime_error("Invalid fraction"); }
        if(p<s.size()&&(s[p]=='e'||s[p]=='E')) { ++p; if(p<s.size()&&(s[p]=='+'||s[p]=='-')) ++p; size_t b=p; while(p<s.size()&&s[p]>='0'&&s[p]<='9') ++p; if(p==b) throw std::runtime_error("Invalid exponent"); }
        double n=std::strtod(s.substr(begin,p-begin).c_str(),nullptr); if(!std::isfinite(n)) throw std::runtime_error("Nonfinite number"); return Value(n);
    }
};
inline Value parse(const std::string& s) { if(s.size()>4*1024*1024) throw std::runtime_error("Guide exceeds 4 MB"); Parser p{s}; if(s.compare(0,3,"\xEF\xBB\xBF")==0) p.p=3; Value v=p.read(); p.ws(); if(p.p!=s.size()) throw std::runtime_error("Trailing JSON"); return v; }
inline std::string quote(const std::string& s) { std::ostringstream o; o<<'"'; for(unsigned char c:s) { if(c=='"'||c=='\\') o<<'\\'<<c; else if(c<32) o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<unsigned>(c)<<std::dec; else o<<c; } o<<'"'; return o.str(); }
inline std::string dump(const Value& v, unsigned depth=0) {
    switch(v.kind) { case Value::Null:return "null"; case Value::Bool:return v.boolean?"true":"false"; case Value::Number:{std::ostringstream o; o<<std::setprecision(17)<<v.num(); return o.str();} case Value::String:return quote(v.string); default:break; }
    bool obj=v.kind==Value::Object; std::string out=obj?"{":"["; bool first=true;
    auto add=[&](const std::string& key,const Value& x){ if(!first) out+=","; first=false; out+="\n"+std::string((depth+1)*2,' '); if(obj) out+=quote(key)+": "; out+=dump(x,depth+1); };
    if(obj) for(const auto& x:v.object) add(x.first,x.second); else for(const auto& x:v.array) add("",x);
    if(!first) out+="\n"+std::string(depth*2,' '); return out+(obj?"}":"]");
}
}
