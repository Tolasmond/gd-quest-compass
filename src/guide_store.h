#pragma once
#include "guide_json.h"
#include <memory>
#include <atomic>
#include <set>
#include <algorithm>
#include <io.h>
namespace guides {
using guidejson::Value;
inline std::wstring wide(const std::string& s) { if(s.find('\0')!=std::string::npos) throw std::runtime_error("Embedded NUL"); int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0); if(!n&&!s.empty()) throw std::runtime_error("Invalid UTF-8"); std::wstring w(n,0); if(n) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),&w[0],n); return w; }
inline std::string narrow(const std::wstring& w) { int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr); if(!n&&!w.empty()) throw std::runtime_error("Invalid Unicode"); std::string s(n,0); if(n) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),&s[0],n,nullptr,nullptr); return s; }
template<size_t N> void copy(char (&out)[N], const std::string& s) { wide(s); if(s.size()>=N) throw std::runtime_error("Field too long"); strcpy_s(out,s.c_str()); }
template<size_t N> void copy(wchar_t (&out)[N], const std::string& s) { auto w=wide(s); if(w.size()>=N) throw std::runtime_error("Field too long"); wcscpy_s(out,w.c_str()); }
inline std::string id(const Value& v) { auto s=v.at("id").str(); if(s.empty()||s.size()>95||s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=std::string::npos) throw std::runtime_error("Invalid stable ID"); return s; }
inline const std::vector<Value>& list(const Value& v,const char* key) { const auto& a=v.at(key); if(a.kind!=Value::Array||a.array.size()>4096) throw std::runtime_error(std::string("Invalid/oversized list: ")+key); return a.array; }
inline bool removed(const Value& v) { auto i=v.object.find("deleted"); return i!=v.object.end() && i->second.kind==Value::Bool && i->second.boolean; }
inline bool locationDisabled(const Value& v) { auto i=v.object.find("enabled"); return i!=v.object.end() && i->second.kind==Value::Bool && !i->second.boolean; }
struct ObjectiveRequirement { unsigned taskUid=0, objectiveUid=0; };
struct Target { char id[96]{}, record[512]{}; unsigned questUid=0,taskUid=0,objectiveUid=0; wchar_t name[256]{},searchHint[256]{}; bool enemy=false; std::vector<ObjectiveRequirement> requiresCompleted; };
struct Candidate { unsigned target=0, order=4096; char id[96]{},locationId[96]{},zone[256]{}; wchar_t name[256]{},mode[128]{}; double x=0,y=0,z=0; };
struct Secret { Candidate location; double radius=25; };
struct Guide { std::vector<Target> targets; std::vector<Candidate> candidates; std::vector<Secret> secrets; std::map<std::string,std::string> areas; Value document; std::string diagnostics; bool questArrow=true,shrineArrow=true,secretArrow=true; };
inline Value emptyDocument() { Value d=Value::dict(); d["schema_version"]=1u; for(auto k:{"targets","locations","bindings","travel_areas"}) d[k]=Value::list(); return d; }
inline Value readFile(const std::wstring& path) {
    FILE* f=nullptr; if(_wfopen_s(&f,path.c_str(),L"rb") || !f) throw std::runtime_error("Cannot open guide file");
    std::string s; char b[4096]; size_t n; while((n=fread(b,1,sizeof(b),f))!=0) { s.append(b,n); if(s.size()>4*1024*1024) { fclose(f); throw std::runtime_error("Guide exceeds 4 MB"); } } bool bad=ferror(f)!=0; fclose(f); if(bad) throw std::runtime_error("Guide read failed"); return guidejson::parse(s);
}
inline Value merge(const Value& base,const Value& personal) {
    if(base.at("schema_version").uid()!=1||personal.at("schema_version").uid()!=1) throw std::runtime_error("Unsupported guide schema");
    Value out=emptyDocument();
    // Older files may still contain transition pairs. They remain on disk as
    // historical evidence but are not part of compiled quest guidance.
    for(auto key:{"targets","locations","bindings","travel_areas"}) {
        std::map<std::string,size_t> indices;
        for(const Value* source:{&base,&personal}) {
            std::set<std::string> seen;
            for(const auto& entry:list(*source,key)) { auto name=id(entry); if(!seen.insert(name).second) throw std::runtime_error("Duplicate ID in file: "+name); auto found=indices.find(name); if(found==indices.end()) { indices[name]=out[key].array.size(); out[key].array.push_back(entry); } else out[key].array[found->second]=entry; }
        }
    } return out;
}
inline unsigned bindingOrder(const Value& binding) {
    auto found=binding.object.find("order");
    if(found==binding.object.end()) return 4096; // Legacy order is array order.
    unsigned order=found->second.uid();
    if(order>=4096) throw std::runtime_error("Waypoint order must be below 4096");
    return order;
}
inline std::vector<Value> orderedBindings(const Value& document,const std::string& target) {
    std::vector<Value> result;
    for(const auto& binding:list(document,"bindings")) try {
        if(!removed(binding)&&binding.at("target").str()==target) result.push_back(binding);
    } catch(const std::exception&) { /* Loader diagnostics identify malformed bindings. */ }
    auto safeOrder=[](const Value& binding) {try{return bindingOrder(binding);}catch(const std::exception&){return 4096u;}};
    std::stable_sort(result.begin(),result.end(),[&](const Value& a,const Value& b){return safeOrder(a)<safeOrder(b);});
    return result;
}
inline std::shared_ptr<const Guide> compile(const Value& base,const Value& personal) {
    auto g=std::make_shared<Guide>(); g->document=merge(base,personal);
    auto visibility=personal.object.find("arrow_visibility");
    if(visibility!=personal.object.end()) {
        if(visibility->second.kind!=Value::Object) throw std::runtime_error("Invalid arrow visibility settings");
        auto setting=[&](const char* name) {
            auto found=visibility->second.object.find(name);
            if(found==visibility->second.object.end()) return true;
            if(found->second.kind!=Value::Bool) throw std::runtime_error(std::string("Invalid arrow visibility setting: ")+name);
            return found->second.boolean;
        };
        g->questArrow=setting("quest");g->shrineArrow=setting("shrine");g->secretArrow=setting("secret");
    }
    auto report=[&](const char* section,const Value& v,const std::exception& e){g->diagnostics+=std::string(section)+" "+id(v)+": "+e.what()+"\n";};
    std::map<std::string,Value> locations;
    for(const auto& v:list(g->document,"locations")) if(!removed(v) && !locationDisabled(v)) try {
        auto zone=v.at("zone").str(); if(zone.empty()) throw std::runtime_error("No verified usable zone");
        Candidate c; copy(c.zone,zone); copy(c.name,v.at("name").str()); copy(c.mode,v.at("mode").str());
        c.x=v.at("x").num();c.y=v.at("y").num();c.z=v.at("z").num(); if(std::abs(c.x)>1e7||std::abs(c.y)>1e7||std::abs(c.z)>1e7) throw std::runtime_error("Coordinates out of range");
        const auto& p=v.at("provenance"); p.at("kind").str(); p.at("source").str(); p.at("coordinate_frame").str(); p.at("game_build").str();
        if(v.object.count("secret_radius")) {
            double radius=v.at("secret_radius").num();
            if(radius<=0 || radius>1000) throw std::runtime_error("Secret radius must be >0 and <=1000");
            copy(c.locationId,id(v));g->secrets.push_back({c,radius});
        }
        locations[id(v)]=v;
    } catch(const std::exception& e){ report("location",v,e); }
    std::set<std::string> objectiveKeys;
    for(const auto& v:list(g->document,"targets")) if(!removed(v)) try {
        Target t; copy(t.id,id(v)); t.questUid=v.at("quest_uid").uid();t.taskUid=v.at("task_uid").uid();t.objectiveUid=v.at("objective_uid").uid();
        std::string key=std::to_string(t.questUid)+"/"+std::to_string(t.taskUid)+"/"+std::to_string(t.objectiveUid);
        if(!objectiveKeys.insert(key).second) throw std::runtime_error("Duplicate objective mapping");
        copy(t.name,v.at("name").str());copy(t.searchHint,v.at("hint").str()); auto r=v.at("record").str(); std::replace(r.begin(),r.end(),'\\','/'); for(char& c:r) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');
        if(!r.empty()&&(r.find("records/")!=0||r.size()<4||r.substr(r.size()-4)!=".dbr"||r.find("..")!=std::string::npos)) throw std::runtime_error("Invalid exact record path"); copy(t.record,r);
        if(v.at("enemy").kind!=Value::Bool) throw std::runtime_error("Invalid enemy flag"); t.enemy=v.at("enemy").boolean;
        if(v.object.count("requires_completed")) {
            std::set<std::pair<unsigned,unsigned>> seen;
            for(const auto& requirement:list(v,"requires_completed")) {
                ObjectiveRequirement prerequisite{requirement.at("task_uid").uid(),requirement.at("objective_uid").uid()};
                if(!prerequisite.taskUid || !prerequisite.objectiveUid || (prerequisite.taskUid==t.taskUid && prerequisite.objectiveUid==t.objectiveUid) ||
                    !seen.insert({prerequisite.taskUid,prerequisite.objectiveUid}).second) throw std::runtime_error("Invalid/duplicate objective prerequisite");
                t.requiresCompleted.push_back(prerequisite);
            }
        }
        g->targets.push_back(t);
    } catch(const std::exception& e){ report("target",v,e); }
    for(const auto& v:list(g->document,"bindings")) if(!removed(v)) try {
        auto target=v.at("target").str(), loc=v.at("location").str(); size_t ti=0; while(ti<g->targets.size()&&target!=g->targets[ti].id) ++ti;
        if(ti==g->targets.size()||!locations.count(loc)) throw std::runtime_error("Missing/invalid target or location"); const auto& l=locations.at(loc); Candidate c;
        c.target=static_cast<unsigned>(ti);c.order=bindingOrder(v);
        copy(c.id,id(v));copy(c.locationId,loc);copy(c.zone,l.at("zone").str());copy(c.name,l.at("name").str());copy(c.mode,l.at("mode").str());c.x=l.at("x").num();c.y=l.at("y").num();c.z=l.at("z").num(); g->candidates.push_back(c);
    } catch(const std::exception& e){report("binding",v,e);}
    // Reorder only each target's slots, preserving legacy global candidate indices.
    for(size_t target=0;target<g->targets.size();++target) {
        std::vector<size_t> positions;std::vector<Candidate> route;
        for(size_t i=0;i<g->candidates.size();++i)if(g->candidates[i].target==target){positions.push_back(i);route.push_back(g->candidates[i]);}
        std::stable_sort(route.begin(),route.end(),[](const Candidate& a,const Candidate& b){return a.order<b.order;});
        for(size_t i=0;i<positions.size();++i)g->candidates[positions[i]]=route[i];
    }
    for(const auto& v:list(g->document,"travel_areas")) if(!removed(v)) try {
        auto group=id(v); const auto& zones=list(v,"zones"); std::vector<std::string> tags; for(const auto& z:zones) { auto s=z.str(); if(s.empty()||s.size()>255||g->areas.count(s)) throw std::runtime_error("Empty/duplicate area tag"); tags.push_back(s); } for(const auto& s:tags) g->areas[s]=group;
    } catch(const std::exception& e){report("travel area",v,e);}
    return g;
}
inline bool shares(const Guide& g,const char* a,const char* b) { if(!a||!b||!*a||!*b) return false; if(!strcmp(a,b)) return true; auto x=g.areas.find(a),y=g.areas.find(b); return x!=g.areas.end()&&y!=g.areas.end()&&x->second==y->second; }
inline void upsert(Value& d,const char* section,const Value& v) { auto key=id(v); auto& a=d[section].array; for(auto& row:a) if(id(row)==key) {row=v;return;} a.push_back(v); }
inline std::wstring guideDirectory;
inline std::shared_ptr<const Guide> published=[](){ auto g=std::make_shared<Guide>();g->document=emptyDocument();return g;}();
inline Value defaults=emptyDocument(),personal=emptyDocument();
inline std::vector<Value> undo;
inline std::string status="Guide not loaded";
inline void publish(std::shared_ptr<const Guide> g) { std::atomic_store(&published,std::move(g)); }
inline bool reload() { try { auto b=readFile(guideDirectory+L"/defaults.json"); Value p=emptyDocument(); auto file=guideDirectory+L"/personal.json"; if(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES) p=readFile(file); auto g=compile(b,p); defaults=std::move(b);personal=std::move(p);undo.clear();status=g->diagnostics.empty()?"Guide loaded":g->diagnostics;publish(g);return true; } catch(const std::exception& e){status=std::string("Reload failed; previous guide retained: ")+e.what();return false;} }
inline void safeWrite(const Value& document) {
    auto bytes=guidejson::dump(document)+"\n"; guidejson::parse(bytes);
    auto path=guideDirectory+L"/personal.json", temp=path+L".tmp", backup=path+L".bak";
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); if(f==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create temporary guide"); DWORD written=0;
    bool ok=WriteFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(f); CloseHandle(f);
    if(!ok) { DeleteFileW(temp.c_str()); throw std::runtime_error("Guide write failed; previous file retained"); }
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) ok=ReplaceFileW(path.c_str(),temp.c_str(),backup.c_str(),0,nullptr,nullptr)!=FALSE;
    else ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok) { DeleteFileW(temp.c_str());throw std::runtime_error("Guide replacement failed; previous file retained"); }
}
inline bool commit(const Value& next,bool remember=true) { try { auto g=compile(defaults,next); if(!g->diagnostics.empty()) throw std::runtime_error(g->diagnostics); auto file=guideDirectory+L"/personal.json"; if(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES && guidejson::dump(readFile(file))!=guidejson::dump(personal)) throw std::runtime_error("Personal guide changed on disk; reload before saving"); safeWrite(next); if(remember) {undo.push_back(personal);if(undo.size()>20)undo.erase(undo.begin());} personal=next;publish(g);status="Saved; guidance updated";return true; } catch(const std::exception& e) {status=std::string("Not saved: ")+e.what();return false;} }
inline bool undoLast() { if(undo.empty()){try{auto backup=readFile(guideDirectory+L"/personal.json.bak");if(!commit(backup))return false;status="Restored saved backup";return true;}catch(const std::exception&){status="No saved change to undo";return false;}} auto previous=undo.back();if(!commit(previous,false))return false;undo.pop_back();status="Last change undone";return true; }
inline std::string newId(const char* prefix) { static unsigned sequence=0; FILETIME t;GetSystemTimeAsFileTime(&t);return std::string(prefix)+"-"+std::to_string((static_cast<unsigned long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime)+"-"+std::to_string(++sequence); }
inline Value provenance(const char* kind) { Value p=Value::dict();p["kind"]=kind;p["source"]="in-game recorder";p["game_build"]="24825149";p["coordinate_frame"]="game world XYZ";p["verification"]="user-recorded; not a verified spawn";p["recorded_utc_filetime"]=newId("time");return p; }
}
