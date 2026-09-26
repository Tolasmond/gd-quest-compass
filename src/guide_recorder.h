// Included inside the overlay namespace; UI uses copied samples only.
HWND recorderWindow=nullptr;
HBRUSH recorderBackgroundBrush=nullptr, recorderFieldBrush=nullptr, recorderReadOnlyBrush=nullptr;
HFONT recorderBodyFont=nullptr, recorderHeadingFont=nullptr, recorderHeaderFont=nullptr;
constexpr COLORREF RecBackground=RGB(13,11,9), RecPanel=RGB(23,18,14), RecField=RGB(9,8,7);
constexpr COLORREF RecReadOnly=RGB(20,16,12), RecBrown=RGB(58,40,26), RecBronze=RGB(155,106,54);
constexpr COLORREF RecParchment=RGB(225,210,173), RecEmber=RGB(198,106,43);
constexpr COLORREF RecSuccess=RGB(117,136,90), RecDisabled=RGB(105,92,74), RecSelected=RGB(82,51,22);
Sample recordingSample{};
QuestRow recordingQuest{};
bool recordingPositionValid=false;
std::vector<DetailRow> recordingObjectives;
std::vector<std::string> recorderLocations;
std::vector<std::string> recorderLinkedLocations;
struct RecorderEntityChoice { std::string record, name, roles; bool enemy=false; };
std::vector<RecorderEntityChoice> recorderQuestEntities;
bool recorderEnemyChecked=false;
std::string recorderQuestPath, recorderCatalogHash, recorderEntityError;
std::vector<size_t> recorderConnectionChoices;
constexpr int RecObjectives=101, RecLocations=102, RecName=103;
constexpr int RecSecretRadius=108;
constexpr int RecLinkedLocations=109;
constexpr int RecQuestEntities=217, RecEntityRecord=219, RecEntityEnemy=220;
constexpr int RecConnections=222, RecApproveConnection=223, RecDismissConnection=224;
constexpr int RecQuestArrow=225, RecShrineArrow=226, RecSecretArrow=227;
HWND RecControl(int id) { return GetDlgItem(recorderWindow,id); }
void RecorderStatus(const std::string& message) { Log(message.c_str()); }
void UpdateRecorderHeader() {
    if(!recorderWindow) return;
    std::wstring title=std::wstring(L"Overlay: ")+DisplayModeLabel(displayMode)+
        L"  |  "+recordingQuest.name+L"\nLocation: "+recordingSample.zone+L" ("+
        std::to_wstring(recordingSample.x)+L", "+std::to_wstring(recordingSample.y)+L", "+
        std::to_wstring(recordingSample.z)+L")";
    SetWindowTextW(RecControl(107),title.c_str());
}

void ReleaseRecorderTheme() {
    if(recorderBackgroundBrush) DeleteObject(recorderBackgroundBrush);
    if(recorderFieldBrush) DeleteObject(recorderFieldBrush);
    if(recorderReadOnlyBrush) DeleteObject(recorderReadOnlyBrush);
    if(recorderBodyFont) DeleteObject(recorderBodyFont);
    if(recorderHeadingFont) DeleteObject(recorderHeadingFont);
    if(recorderHeaderFont) DeleteObject(recorderHeaderFont);
    recorderBackgroundBrush=recorderFieldBrush=recorderReadOnlyBrush=nullptr;
    recorderBodyFont=recorderHeadingFont=recorderHeaderFont=nullptr;
}
void InitializeRecorderTheme(HWND hwnd) {
    ReleaseRecorderTheme();
    recorderBackgroundBrush=CreateSolidBrush(RecBackground);
    recorderFieldBrush=CreateSolidBrush(RecField);
    recorderReadOnlyBrush=CreateSolidBrush(RecReadOnly);
    recorderBodyFont=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    recorderHeadingFont=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Georgia");
    recorderHeaderFont=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Georgia");
    using DwmSetWindowAttributeFn=HRESULT (WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
    if(auto dwm=LoadLibraryW(L"dwmapi.dll")) {
        if(auto setAttribute=reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwm,"DwmSetWindowAttribute"))) {
            BOOL dark=TRUE;
            if(FAILED(setAttribute(hwnd,20,&dark,sizeof(dark)))) setAttribute(hwnd,19,&dark,sizeof(dark));
        }
        FreeLibrary(dwm);
    }
}
void ThemeNativeRecorderControl(HWND control) {
    using SetWindowThemeFn=HRESULT (WINAPI*)(HWND,LPCWSTR,LPCWSTR);
    if(auto theme=LoadLibraryW(L"uxtheme.dll")) {
        if(auto setTheme=reinterpret_cast<SetWindowThemeFn>(GetProcAddress(theme,"SetWindowTheme")))
            setTheme(control,L"DarkMode_Explorer",nullptr);
        FreeLibrary(theme);
    }
}
void DrawRecorderFrame(HDC dc,const RECT& rect) {
    HBRUSH panel=CreateSolidBrush(RecPanel);FillRect(dc,&rect,panel);DeleteObject(panel);
    HBRUSH border=CreateSolidBrush(RecBronze);FrameRect(dc,&rect,border);DeleteObject(border);
}
void DrawRecorderButton(const DRAWITEMSTRUCT& item) {
    bool disabled=(item.itemState&ODS_DISABLED)!=0, pressed=(item.itemState&ODS_SELECTED)!=0;
    if(item.CtlID==RecEntityEnemy || item.CtlID==RecQuestArrow || item.CtlID==RecShrineArrow || item.CtlID==RecSecretArrow) {
        HBRUSH panel=CreateSolidBrush(RecPanel);FillRect(item.hDC,&item.rcItem,panel);DeleteObject(panel);
        RECT box{item.rcItem.left+3,item.rcItem.top+5,item.rcItem.left+18,item.rcItem.top+20};
        HBRUSH field=CreateSolidBrush(RecField);FillRect(item.hDC,&box,field);DeleteObject(field);
        HBRUSH border=CreateSolidBrush(disabled?RecDisabled:RecBronze);FrameRect(item.hDC,&box,border);DeleteObject(border);
        auto guide=std::atomic_load(&guides::published);
        bool checked=item.CtlID==RecEntityEnemy?recorderEnemyChecked:
            item.CtlID==RecQuestArrow?guide->questArrow:item.CtlID==RecShrineArrow?guide->shrineArrow:guide->secretArrow;
        if(checked) {
            HPEN check=CreatePen(PS_SOLID,2,disabled?RecDisabled:RecSuccess),old=static_cast<HPEN>(SelectObject(item.hDC,check));
            MoveToEx(item.hDC,box.left+3,box.top+7,nullptr);LineTo(item.hDC,box.left+6,box.bottom-3);LineTo(item.hDC,box.right-2,box.top+3);
            SelectObject(item.hDC,old);DeleteObject(check);
        }
        wchar_t text[256]{};GetWindowTextW(item.hwndItem,text,256);RECT label=item.rcItem;label.left+=26;
        SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled?RecDisabled:RecParchment);SelectObject(item.hDC,recorderBodyFont);
        DrawTextW(item.hDC,text,-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if((item.itemState&ODS_FOCUS)&&!disabled) DrawFocusRect(item.hDC,&item.rcItem);
        return;
    }
    COLORREF fill=disabled?RGB(29,25,21):(pressed?RecBrown:RGB(42,30,20));
    HBRUSH brush=CreateSolidBrush(fill);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
    COLORREF edge=item.CtlID==201?RecEmber:RecBronze;
    HPEN pen=CreatePen(PS_SOLID,item.CtlID==201?2:1,edge),oldPen=static_cast<HPEN>(SelectObject(item.hDC,pen));
    HGDIOBJ oldBrush=SelectObject(item.hDC,GetStockObject(NULL_BRUSH));
    Rectangle(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom);
    SelectObject(item.hDC,oldBrush);SelectObject(item.hDC,oldPen);DeleteObject(pen);
    wchar_t text[256]{};GetWindowTextW(item.hwndItem,text,256);
    RECT label=item.rcItem;if(pressed) OffsetRect(&label,1,1);
    SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled?RecDisabled:RecParchment);
    SelectObject(item.hDC,recorderBodyFont);DrawTextW(item.hDC,text,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if((item.itemState&ODS_FOCUS)&&!disabled) {InflateRect(&label,-4,-4);DrawFocusRect(item.hDC,&label);}
}
void SetRecorderEnemyCheck(bool checked) {
    recorderEnemyChecked=checked;
    if(auto control=RecControl(RecEntityEnemy)) InvalidateRect(control,nullptr,TRUE);
}
void RefreshArrowChecks() {
    for(int id:{RecQuestArrow,RecShrineArrow,RecSecretArrow})
        if(auto control=RecControl(id)) InvalidateRect(control,nullptr,TRUE);
}
void DrawRecorderChoice(const DRAWITEMSTRUCT& item) {
    if(item.itemID==static_cast<UINT>(-1)) return;
    bool selected=(item.itemState&ODS_SELECTED)!=0, disabled=(item.itemState&ODS_DISABLED)!=0;
    HBRUSH brush=CreateSolidBrush(selected?RecSelected:RecField);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
    // Labels come from editable guide data; size the buffer from the item's length.
    bool combo=item.CtlType==ODT_COMBOBOX;
    LRESULT length=SendMessageW(item.hwndItem,combo?CB_GETLBTEXTLEN:LB_GETTEXTLEN,item.itemID,0);
    std::wstring buffer(length>0?static_cast<size_t>(length)+1:1,L'\0');
    if(length>0) SendMessageW(item.hwndItem,combo?CB_GETLBTEXT:LB_GETTEXT,item.itemID,reinterpret_cast<LPARAM>(buffer.data()));
    const wchar_t* text=buffer.c_str();
    RECT label=item.rcItem;label.left+=7;label.right-=4;
    SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled?RecDisabled:RecParchment);
    SelectObject(item.hDC,recorderBodyFont);DrawTextW(item.hDC,text,-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(item.itemState&ODS_FOCUS) DrawFocusRect(item.hDC,&item.rcItem);
}
guides::Value ConnectionValue(const connections::Observation& c) {
    auto v=guides::Value::dict();v["from"]=c.from;v["to"]=c.to;v["source"]=c.source;
    v["from_name"]=guides::narrow(c.fromName);v["to_name"]=guides::narrow(c.toName);
    v["from_x"]=c.fromX;v["from_y"]=c.fromY;v["from_z"]=c.fromZ;
    v["to_x"]=c.toX;v["to_y"]=c.toY;v["to_z"]=c.toZ;
    v["sample_tick"]=std::to_string(c.tick);v["dismissed"]=c.dismissed;return v;
}
connections::Observation ParseConnection(const guides::Value& v) {
    connections::Observation c{};c.from=v.at("from").str();c.to=v.at("to").str();
    if(v.object.count("source")) c.source=v.at("source").str();
    if(!connections::outdoor(c.from)||!connections::outdoor(c.to)||c.from==c.to) throw std::runtime_error("Invalid observed zones");
    c.fromName=guides::wide(v.at("from_name").str());c.toName=guides::wide(v.at("to_name").str());
    c.fromX=v.at("from_x").num();c.fromY=v.at("from_y").num();c.fromZ=v.at("from_z").num();
    c.toX=v.at("to_x").num();c.toY=v.at("to_y").num();c.toZ=v.at("to_z").num();
    c.tick=std::stoull(v.at("sample_tick").str());
    c.dismissed=v.at("dismissed").boolean;return c;
}
std::wstring ConnectionCachePath() {return guides::guideDirectory+L"/observed-connections.json";}
void LoadObservedConnections() {
    try {
        auto path=ConnectionCachePath();
        if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) return;
        auto saved=guides::readFile(path);
        if(saved.at("schema_version").uid()!=1) throw std::runtime_error("Unsupported observation cache version");
        std::vector<connections::Observation> loaded;
        for(const auto& v:guides::list(saved,"observations")) {
            auto c=ParseConnection(v);
            if(loaded.size()>=128) break;
            if(std::none_of(loaded.begin(),loaded.end(),[&](const auto& prior){return connections::samePair(prior,c);})) loaded.push_back(std::move(c));
        }
        AcquireSRWLockExclusive(&lock);observedConnections=std::move(loaded);observedConnectionsDirty=false;ReleaseSRWLockExclusive(&lock);
    } catch(const std::exception& e) {Log((std::string("Observation cache not loaded: ")+e.what()).c_str());}
}
void FlushObservedConnections() { // Window thread only: game hooks never write files.
    static ULONGLONG retryAfter=0;
    if(GetTickCount64()<retryAfter) return;
    std::vector<connections::Observation> copy;
    bool dirty=false;
    AcquireSRWLockExclusive(&lock);
    if(observedConnectionsDirty) {copy=observedConnections;observedConnectionsDirty=false;dirty=true;}
    ReleaseSRWLockExclusive(&lock);
    if(!dirty) return;
    try {
        auto document=guides::Value::dict();document["schema_version"]=1u;document["observations"]=guides::Value::list();
        for(const auto& c:copy) document["observations"].array.push_back(ConnectionValue(c));
        auto data=guidejson::dump(document)+"\n";
        auto path=ConnectionCachePath(),temp=path+L".tmp";
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create observation cache");
        DWORD written=0;bool ok=WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);
        CloseHandle(file);
        if(!ok || !MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());throw std::runtime_error("Cannot save observation cache");
        }
        retryAfter=0;
    } catch(const std::exception& e) {
        AcquireSRWLockExclusive(&lock);observedConnectionsDirty=true;ReleaseSRWLockExclusive(&lock);
        retryAfter=GetTickCount64()+1000;
        Log((std::string("Observation cache save failed: ")+e.what()).c_str());
    }
}
void FillObservedConnections() {
    if(!recorderWindow) return;
    SendMessageW(RecControl(RecConnections),LB_RESETCONTENT,0,0);recorderConnectionChoices.clear();
    std::vector<connections::Observation> copy;
    AcquireSRWLockShared(&lock);copy=observedConnections;ReleaseSRWLockShared(&lock);
    auto guide=std::atomic_load(&guides::published);
    for(size_t i=0;i<copy.size();++i) {
        const auto& c=copy[i];
        if(c.dismissed || guides::shares(*guide,c.from.c_str(),c.to.c_str())) continue;
        std::wstring label=c.fromName+L" → "+c.toName+L" ("+guides::wide(c.from)+L" / "+guides::wide(c.to)+L")";
        SendMessageW(RecControl(RecConnections),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        recorderConnectionChoices.push_back(i);
    }
    if(!recorderConnectionChoices.empty()) SendMessageW(RecControl(RecConnections),LB_SETCURSEL,0,0);
    bool selected=!recorderConnectionChoices.empty();
    EnableWindow(RecControl(RecApproveConnection),selected);
    EnableWindow(RecControl(RecDismissConnection),selected);
}
std::string SelectedLocation();
guides::Value LocationById(const std::string& id);
void UpdateSecretRadiusControl();
std::string NearbySavedWaypoint() {
    if(!recordingPositionValid) return {};
    auto guide=std::atomic_load(&guides::published);
    std::string nearest;
    double nearestDistance=5.0;
    for(const auto& id:recorderLocations) try {
        for(const auto& location:guides::list(guide->document,"locations")) if(!guides::removed(location) && guides::id(location)==id &&
            !guides::locationDisabled(location) && location.at("zone").str()==recordingSample.zoneTag) {
            double distance=std::hypot(location.at("x").num()-recordingSample.x,location.at("z").num()-recordingSample.z);
            if(distance<=nearestDistance) {nearest=id;nearestDistance=distance;}
            break;
        }
    } catch(const std::exception&) { /* Invalid saved locations are not eligible. */ }
    return nearest;
}
bool HasSelectedObjective() {
    auto selected=SendMessageW(RecControl(RecObjectives),CB_GETCURSEL,0,0);
    return selected>=0 && static_cast<size_t>(selected)<recordingObjectives.size();
}
void UpdateOrderControls() {
    auto selected=SendMessageW(RecControl(RecLinkedLocations),LB_GETCURSEL,0,0);
    EnableWindow(RecControl(215),selected>0);
    EnableWindow(RecControl(216),selected>=0 && static_cast<size_t>(selected+1)<recorderLinkedLocations.size());
}
void UpdateRecorderControls() {
    bool objective=HasSelectedObjective();
    auto linked=SendMessageW(RecControl(RecLinkedLocations),LB_GETCURSEL,0,0);
    auto saved=SendMessageW(RecControl(RecLocations),LB_GETCURSEL,0,0);
    bool linkedLocation=linked>=0 && static_cast<size_t>(linked)<recorderLinkedLocations.size();
    bool savedLocation=saved>=0 && static_cast<size_t>(saved)<recorderLocations.size();
    bool location=linkedLocation || savedLocation;
    bool secret=false, usableSavedLocation=false;
    if(location) try {
        auto selectedLocation=LocationById(SelectedLocation());
        secret=selectedLocation.object.count("secret_radius")!=0;
        usableSavedLocation=savedLocation && !guides::locationDisabled(selectedLocation);
    } catch(const std::exception&) {}
    auto entity=SendMessageW(RecControl(RecQuestEntities),CB_GETCURSEL,0,0);
    bool entityChoice=entity>=0 && static_cast<size_t>(entity)<recorderQuestEntities.size();
    bool hasLiveRecord=false;
    if(objective) {
        const auto& row=recordingObjectives[SendMessageW(RecControl(RecObjectives),CB_GETCURSEL,0,0)];
        auto guide=std::atomic_load(&guides::published);
        for(const auto& target:guide->targets) if(target.questUid==recordingQuest.id &&
            target.taskUid==row.taskUid && target.objectiveUid==row.objectiveUid && target.record[0]) hasLiveRecord=true;
    }
    EnableWindow(RecControl(201),objective && recordingPositionValid);
    EnableWindow(RecControl(204),objective && usableSavedLocation);
    EnableWindow(RecControl(205),location);
    EnableWindow(RecControl(206),objective && linkedLocation);
    EnableWindow(RecControl(211),location && recordingPositionValid && (objective || secret));
    EnableWindow(RecControl(212),location);
    EnableWindow(RecControl(213),recordingPositionValid);
    EnableWindow(RecControl(218),objective && entityChoice);
    EnableWindow(RecControl(221),objective && hasLiveRecord);
    EnableWindow(RecControl(RecEntityEnemy),objective && entityChoice);
    EnableWindow(RecControl(RecQuestEntities),objective && !recorderQuestEntities.empty());
    UpdateOrderControls();
}
void FillRecorderLocations() {
    std::string keep;try {keep=SelectedLocation();} catch(const std::exception&) {}
    SendMessageW(RecControl(RecLocations),LB_RESETCONTENT,0,0); recorderLocations.clear();
    SendMessageW(RecControl(RecLinkedLocations),LB_RESETCONTENT,0,0);recorderLinkedLocations.clear();
    auto g=std::atomic_load(&guides::published);
    std::vector<std::string> linkedOrder;
    std::set<std::string> linked;
    auto selected=SendMessageW(RecControl(RecObjectives),CB_GETCURSEL,0,0);
    if(selected>=0 && static_cast<size_t>(selected)<recordingObjectives.size()) {
        const auto& row=recordingObjectives[selected];
        for(const auto& t:g->targets) if(t.questUid==recordingQuest.id&&t.taskUid==row.taskUid&&t.objectiveUid==row.objectiveUid)
            for(const auto& b:guides::orderedBindings(g->document,t.id)) {
                auto location=b.at("location").str();
                if(linked.insert(location).second) linkedOrder.push_back(location);
            }
    }
    std::map<std::string,std::wstring> labels;
    std::vector<std::string> savedOrder;
    for(const auto& l:guides::list(g->document,"locations")) if(!guides::removed(l) && !guides::locationDisabled(l)) try {
        auto zoneName=l.object.find("zone_name");
        std::string label=(l.object.count("secret_radius")?"[Secret] ":"")+l.at("name").str()+" - "+(zoneName!=l.object.end()?zoneName->second.str():"saved area");
        auto location=guides::id(l);labels[location]=guides::wide(label);savedOrder.push_back(location);
    } catch(const std::exception&) { /* Loader diagnostics identify invalid entries. */ }
    for(const auto& location:linkedOrder) if(labels.count(location)) {
        auto label=std::to_wstring(recorderLinkedLocations.size()+1)+L". "+labels.at(location);
        SendMessageW(RecControl(RecLinkedLocations),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        recorderLinkedLocations.push_back(location);
    }
    for(const auto& location:savedOrder) if(!linked.count(location)) {
        SendMessageW(RecControl(RecLocations),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(labels.at(location).c_str()));
        recorderLocations.push_back(location);
    }
    for(size_t i=0;i<recorderLinkedLocations.size();++i) if(recorderLinkedLocations[i]==keep) SendMessageW(RecControl(RecLinkedLocations),LB_SETCURSEL,i,0);
    for(size_t i=0;i<recorderLocations.size();++i) if(recorderLocations[i]==keep) SendMessageW(RecControl(RecLocations),LB_SETCURSEL,i,0);
    if(auto nearby=NearbySavedWaypoint();!nearby.empty()) {
        auto found=std::find(recorderLocations.begin(),recorderLocations.end(),nearby);
        SendMessageW(RecControl(RecLinkedLocations),LB_SETCURSEL,static_cast<WPARAM>(-1),0);
        SendMessageW(RecControl(RecLocations),LB_SETCURSEL,found-recorderLocations.begin(),0);
    }
    UpdateSecretRadiusControl();
    UpdateRecorderControls();
}
std::string SelectedLocation() {
    auto linked=SendMessageW(RecControl(RecLinkedLocations),LB_GETCURSEL,0,0);
    if(linked>=0&&static_cast<size_t>(linked)<recorderLinkedLocations.size()) return recorderLinkedLocations[linked];
    auto i=SendMessageW(RecControl(RecLocations),LB_GETCURSEL,0,0);
    if(i<0||static_cast<size_t>(i)>=recorderLocations.size()) throw std::runtime_error("Select a location from either list first");return recorderLocations[i];
}
guides::Value LocationById(const std::string& id) { auto g=std::atomic_load(&guides::published);for(const auto& l:guides::list(g->document,"locations"))if(!guides::removed(l)&&guides::id(l)==id)return l;throw std::runtime_error("Location no longer exists"); }
void UpdateSecretRadiusControl() {
    bool secret=false;
    try {
        auto l=LocationById(SelectedLocation());secret=l.object.count("secret_radius")!=0;
        if(secret) SetWindowTextW(RecControl(RecSecretRadius),guides::wide(guidejson::dump(l.at("secret_radius"))).c_str());
    } catch(const std::exception&) {}
    EnableWindow(RecControl(214),secret);
}
DetailRow SelectedRecordingObjective() {
    auto i=SendMessageW(RecControl(RecObjectives),CB_GETCURSEL,0,0);if(i<0||static_cast<size_t>(i)>=recordingObjectives.size())throw std::runtime_error("Select an unfinished objective");
    // Captured when opened in the game; never read live game objects here.
    auto row=recordingObjectives[i]; Sample current=Snapshot();
    if(QuestsLive(current)) {
        bool stillUnfinished=false;
        for(const auto& q:current.quests.rows) if(q.id==recordingQuest.id&&q.detailsValid&&q.details)
            for(const auto& d:*q.details) if(d.kind==DetailKind::Objective&&d.taskUid==row.taskUid&&
                d.objectiveUid==row.objectiveUid&&d.taskState==2&&d.objectiveState==2) stillUnfinished=true;
        if(!stillUnfinished) throw std::runtime_error("Objective changed; close and reopen the recorder");
    }
    return row;
}
std::string RecorderName(const char* fallback) { wchar_t name[256]{};GetWindowTextW(RecControl(RecName),name,256);return *name?guides::narrow(name):fallback; }
std::wstring ObjectiveLabel(const DetailRow& row) { std::wstring text(row.text); auto prefix=text.find(L"]: "); if(prefix!=std::wstring::npos)text=text.substr(prefix+3); if(text.size()>240){text.resize(240);if(text.back()>=0xD800&&text.back()<=0xDBFF)text.pop_back();}return text; }
void LoadQuestEntityChoices() {
    recorderQuestEntities.clear();recorderQuestPath.clear();recorderCatalogHash.clear();recorderEntityError.clear();
    if(!recordingQuest.id) return;
    try {
        auto catalog=guides::readFile(guides::guideDirectory+L"/quest-entities.json");
        if(catalog.at("schema_version").uid()!=1) throw std::runtime_error("Unsupported quest entity catalog");
        recorderCatalogHash=catalog.at("source_sha256").str();
        if(recorderCatalogHash.size()!=64) throw std::runtime_error("Invalid quest entity source hash");
        for(const auto& quest:guides::list(catalog,"quests")) if(quest.at("quest_uid").uid()==recordingQuest.id) {
            recorderQuestPath=quest.at("quest_path").str();
            for(const auto& entry:guides::list(quest,"entities")) {
                RecorderEntityChoice choice;
                choice.record=entry.at("record").str();
                if(choice.record.rfind("records/creatures/",0)!=0 || choice.record.size()>511 ||
                   choice.record.size()<4 || choice.record.substr(choice.record.size()-4)!=".dbr" ||
                   choice.record.find("..")!=std::string::npos) continue;
                choice.name=entry.at("name").str();
                choice.enemy=choice.record.rfind("records/creatures/enemies/",0)==0;
                for(const auto& role:guides::list(entry,"roles")) {
                    if(!choice.roles.empty()) choice.roles+=", ";choice.roles+=role.str();
                }
                recorderQuestEntities.push_back(std::move(choice));
            }
            break;
        }
        if(recorderQuestEntities.empty()) recorderEntityError="No character records in the extracted data for this quest";
    } catch(const std::exception& e) {recorderQuestEntities.clear();recorderEntityError=std::string("Quest entity catalog unavailable: ")+e.what();}
}
void RefreshQuestEntityChoice() {
    SendMessageW(RecControl(RecQuestEntities),CB_RESETCONTENT,0,0);
    std::string currentRecord;
    bool currentEnemy=false;
    auto objective=SendMessageW(RecControl(RecObjectives),CB_GETCURSEL,0,0);
    if(objective>=0 && static_cast<size_t>(objective)<recordingObjectives.size()) {
        const auto& row=recordingObjectives[objective];
        auto guide=std::atomic_load(&guides::published);
        for(const auto& target:guide->targets) if(target.questUid==recordingQuest.id &&
            target.taskUid==row.taskUid && target.objectiveUid==row.objectiveUid) {currentRecord=target.record;currentEnemy=target.enemy;}
    }
    int selected=-1;
    for(size_t i=0;i<recorderQuestEntities.size();++i) {
        const auto& choice=recorderQuestEntities[i];
        auto label=guides::wide(choice.name+" ["+choice.roles+"] — "+choice.record);
        SendMessageW(RecControl(RecQuestEntities),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        if(currentRecord==choice.record) selected=static_cast<int>(i);
    }
    SendMessageW(RecControl(RecQuestEntities),CB_SETCURSEL,selected,0);
    SetWindowTextW(RecControl(RecEntityRecord),guides::wide(currentRecord).c_str());
    SetRecorderEnemyCheck(selected>=0 && currentEnemy);
    UpdateRecorderControls();
}
void ShowChosenQuestEntity() {
    auto selected=SendMessageW(RecControl(RecQuestEntities),CB_GETCURSEL,0,0);
    if(selected<0 || static_cast<size_t>(selected)>=recorderQuestEntities.size()) {
        SetWindowTextW(RecControl(RecEntityRecord),L"");UpdateRecorderControls();return;
    }
    const auto& choice=recorderQuestEntities[selected];
    SetWindowTextW(RecControl(RecEntityRecord),guides::wide(choice.record).c_str());
    SetRecorderEnemyCheck(choice.enemy);
    UpdateRecorderControls();
}
std::string EnsureRecordingTarget(guides::Value& next,const DetailRow& row) {
    auto g=std::atomic_load(&guides::published);for(const auto& t:g->targets)if(t.questUid==recordingQuest.id&&t.taskUid==row.taskUid&&t.objectiveUid==row.objectiveUid)return t.id;
    auto t=guides::Value::dict();t["id"]=guides::newId("objective");t["quest_uid"]=recordingQuest.id;t["task_uid"]=row.taskUid;t["objective_uid"]=row.objectiveUid;t["name"]=guides::narrow(ObjectiveLabel(row));t["record"]="";t["enemy"]=false;t["hint"]="Travel to a recorded location";t["provenance"]=guides::provenance("user_objective_mapping");guides::upsert(next,"targets",t);return guides::id(t);
}
void BindRecording(guides::Value& next,const std::string& target,const std::string& location) {
    guides::bindLocation(next,target,location);
}
guides::Value CapturedLocation() {
    auto l=guides::Value::dict(); l["id"]=guides::newId("location");l["name"]=RecorderName("Recorded approach");l["zone"]=recordingSample.zoneTag;l["zone_name"]=guides::narrow(recordingSample.zone);
    l["x"]=static_cast<double>(recordingSample.x);l["y"]=static_cast<double>(recordingSample.y);l["z"]=static_cast<double>(recordingSample.z);l["mode"]="Recorded waypoint";l["provenance"]=guides::provenance("player_position");
    l["provenance"]["player_zone"]=recordingSample.zoneTag;l["provenance"]["sample_tick"]=std::to_string(recordingSample.time);
    return l;
}
void ReviewObservedConnection(int action) {
    auto selected=SendMessageW(RecControl(RecConnections),LB_GETCURSEL,0,0);
    if(selected<0 || static_cast<size_t>(selected)>=recorderConnectionChoices.size()) throw std::runtime_error("Select an observed connection first");
    size_t index=recorderConnectionChoices[selected];
    connections::Observation c;
    AcquireSRWLockShared(&lock);
    bool available=index<observedConnections.size();if(available)c=observedConnections[index];
    ReleaseSRWLockShared(&lock);
    if(!available || c.dismissed) throw std::runtime_error("Observation is no longer available");
    if(action==RecDismissConnection) {
        AcquireSRWLockExclusive(&lock);observedConnections[index].dismissed=true;observedConnectionsDirty=true;ReleaseSRWLockExclusive(&lock);
        FlushObservedConnections();FillObservedConnections();RecorderStatus("Connection proposal dismissed; guide unchanged.");return;
    }
    auto next=guides::personal;
    auto merged=guides::merge(guides::defaults,next);
    if(guides::shares(*std::atomic_load(&guides::published),c.from.c_str(),c.to.c_str())) {
        FillObservedConnections();RecorderStatus("These areas are already connected in the guide.");return;
    }
    guides::Value first,second;bool hasFirst=false,hasSecond=false;
    for(const auto& area:guides::list(merged,"travel_areas")) if(!guides::removed(area)) {
        for(const auto& zone:guides::list(area,"zones")) {
            if(zone.str()==c.from) {first=area;hasFirst=true;}
            if(zone.str()==c.to) {second=area;hasSecond=true;}
        }
    }
    guides::Value area;
    if(hasFirst) area=first;
    else if(hasSecond) area=second;
    else {
        area=guides::Value::dict();area["id"]=guides::newId("travel-area");area["zones"]=guides::Value::list();
        area["provenance"]=guides::Value::dict();area["provenance"]["kind"]="user_reviewed_crossing";
        area["provenance"]["source"]="in-game observed zone crossing";
    }
    auto addZone=[&](const std::string& tag){
        for(const auto& existing:guides::list(area,"zones")) if(existing.str()==tag)return;
        area["zones"].array.emplace_back(tag);
    };
    addZone(c.from);addZone(c.to);
    if(hasFirst && hasSecond && guides::id(first)!=guides::id(second)) {
        for(const auto& zone:guides::list(second,"zones")) addZone(zone.str());
        second["deleted"]=true;guides::upsert(next,"travel_areas",second);
    }
    auto evidence=ConnectionValue(c);evidence["reviewed_utc_filetime"]=guides::newId("time");
    evidence["review"]="User approved a continuous outdoor zone crossing in Guide Recorder";
    auto& provenance=area["provenance"];
    if(!provenance.object.count("reviewed_connections")) provenance["reviewed_connections"]=guides::Value::list();
    provenance["reviewed_connections"].array.push_back(evidence);
    guides::upsert(next,"travel_areas",area);
    if(guides::commit(next)) {
        FillObservedConnections();RecorderStatus("Outdoor connection saved; quest bearings updated. Undo last change can restore it.");
    } else RecorderStatus(guides::status);
}
void RecorderAction(int action) {
    try {
        if(action==RecApproveConnection || action==RecDismissConnection) {ReviewObservedConnection(action);return;}
        if(action==209){guides::reload();RefreshArrowChecks();LoadQuestEntityChoices();FillRecorderLocations();RefreshQuestEntityChoice();FillObservedConnections();RecorderStatus(guides::status);return;}
        if(action==208){guides::undoLast();RefreshArrowChecks();FillRecorderLocations();RefreshQuestEntityChoice();FillObservedConnections();RecorderStatus(guides::status);return;}
        auto next=guides::personal;
        std::string recordedTargetId, recordedLocationId;
        if(action==218 || action==221) {
            auto row=SelectedRecordingObjective();
            auto targetId=EnsureRecordingTarget(next,row);
            auto merged=guides::merge(guides::defaults,next);
            guides::Value entry;
            bool found=false;
            for(const auto& candidate:guides::list(merged,"targets")) if(guides::id(candidate)==targetId) {entry=candidate;found=true;break;}
            if(!found) for(const auto& candidate:guides::list(next,"targets")) if(guides::id(candidate)==targetId) {entry=candidate;found=true;break;}
            if(!found) throw std::runtime_error("Selected objective mapping is unavailable");
            if(action==218) {
                auto selected=SendMessageW(RecControl(RecQuestEntities),CB_GETCURSEL,0,0);
                if(selected<0 || static_cast<size_t>(selected)>=recorderQuestEntities.size()) throw std::runtime_error("Choose a quest entity first");
                const auto& choice=recorderQuestEntities[selected];
                entry["record"]=choice.record;
                entry["enemy"]=recorderEnemyChecked;
                auto evidence=guides::Value::dict();
                evidence["source"]="data/npc-guidance/quest-graphs.json";
                evidence["source_sha256"]=recorderCatalogHash;
                evidence["quest_path"]=recorderQuestPath;
                evidence["entity_record"]=choice.record;
                evidence["entity_roles"]=choice.roles;
                evidence["review"]="User selected exact quest entity record for this objective; quest graph alone does not prove the association";
                evidence["recorded_utc_filetime"]=guides::newId("time");
                entry["provenance"]["live_tracking_selection"]=evidence;
            } else {
                entry["record"]="";entry["enemy"]=false;
                entry["provenance"]["live_tracking_selection"]["disabled_utc_filetime"]=guides::newId("time");
            }
            guides::upsert(next,"targets",entry);
            if(guides::commit(next)) {
                RefreshQuestEntityChoice();
                RecorderStatus(action==218?"Live tracking enabled for this objective; nearby exact-record matches take priority.":"Live tracking disabled for this objective.");
            } else RecorderStatus(guides::status);
            return;
        }
        if(action==215 || action==216) {
            auto row=SelectedRecordingObjective();auto target=EnsureRecordingTarget(next,row);
            auto links=guides::orderedBindings(guides::merge(guides::defaults,next),target);
            auto current=SendMessageW(RecControl(RecLinkedLocations),LB_GETCURSEL,0,0);
            if(current<0 || static_cast<size_t>(current)>=recorderLinkedLocations.size()) throw std::runtime_error("Select a linked waypoint first");
            auto chosen=recorderLinkedLocations[current];
            auto found=std::find_if(links.begin(),links.end(),[&](const guides::Value& link){return link.at("location").str()==chosen;});
            if(found==links.end()) throw std::runtime_error("Selected waypoint is no longer linked");
            auto index=static_cast<int>(found-links.begin());
            auto other=index+(action==215?-1:1);
            if(other<0 || static_cast<size_t>(other)>=links.size()) return;
            std::swap(links[index],links[other]);
            for(size_t i=0;i<links.size();++i) {links[i]["order"]=static_cast<unsigned>(i+1);guides::upsert(next,"bindings",links[i]);}
            if(guides::commit(next)) {FillRecorderLocations();RecorderStatus("Waypoint order saved; guidance updated.");}
            else RecorderStatus(guides::status);
            return;
        }
        if(action==213 || action==214) {
            wchar_t text[32]{};GetWindowTextW(RecControl(RecSecretRadius),text,32);
            wchar_t* end=nullptr;double radius=wcstod(text,&end);
            if(end==text || *end || !std::isfinite(radius) || radius<=0 || radius>1000) throw std::runtime_error("Enter a secret radius greater than 0 and at most 1000 world units");
            if(action==214) {
                auto l=LocationById(SelectedLocation());
                if(!l.object.count("secret_radius")) throw std::runtime_error("Select a saved secret entrance first");
                l["secret_radius"]=radius;guides::upsert(next,"locations",l);
            } else {
                if(!recordingPositionValid) throw std::runtime_error("No fresh captured position; close and reopen in the game");
                auto l=CapturedLocation();l["name"]=RecorderName("Secret entrance");l["mode"]="Recorded secret entrance";
                l["provenance"]["verification"]="User-recorded secret entrance approach";
                auto location=guides::recordLocation(next,l);
                auto merged=guides::merge(guides::defaults,next);
                for(auto saved:guides::list(merged,"locations")) if(guides::id(saved)==location) {
                    saved["secret_radius"]=radius;guides::upsert(next,"locations",saved);break;
                }
            }
        } else if(action==211) {
            auto l=LocationById(SelectedLocation());
            if(l.object.count("secret_radius")) {if(!recordingPositionValid) throw std::runtime_error("No fresh captured position; reopen in the game");}
            else SelectedRecordingObjective();
            auto capture=CapturedLocation();
            l["x"]=capture.at("x");l["y"]=capture.at("y");l["z"]=capture.at("z");l["zone"]=capture.at("zone");l["zone_name"]=capture.at("zone_name");l["mode"]="Recorded waypoint";l["enabled"]=true;auto history=l.at("provenance");if(!l.object.count("history"))l["history"]=guides::Value::list();l["history"].array.push_back(history);l["provenance"]=capture.at("provenance");guides::upsert(next,"locations",l);
        } else if(action==212) {
            auto location=SelectedLocation();auto merged=guides::merge(guides::defaults,next);auto l=LocationById(location);l["deleted"]=true;guides::upsert(next,"locations",l);
            for(auto b:guides::list(merged,"bindings"))if(b.at("location").str()==location){b["deleted"]=true;guides::upsert(next,"bindings",b);}
        } else if(action==205) { auto l=LocationById(SelectedLocation());l["name"]=RecorderName("Recorded location");guides::upsert(next,"locations",l); }
        else {
            auto row=SelectedRecordingObjective();auto target=EnsureRecordingTarget(next,row);
            if(action==204) {
                auto location=SelectedLocation();
                auto saved=LocationById(location);
                if(guides::locationDisabled(saved)) throw std::runtime_error("Selected location is unavailable for guidance");
                BindRecording(next,target,location);
            }
            else if(action==206) { auto location=SelectedLocation();auto merged=guides::merge(guides::defaults,next);bool found=false;for(auto b:guides::list(merged,"bindings"))if(!guides::removed(b)&&b.at("target").str()==target&&b.at("location").str()==location){b["deleted"]=true;guides::upsert(next,"bindings",b);found=true;}if(!found)throw std::runtime_error("That location is not linked to this objective"); }
            else if(action==201) {
                if(!recordingPositionValid) throw std::runtime_error("No fresh captured position; close and reopen in the game");
                auto l=CapturedLocation();auto location=guides::recordLocation(next,l);
                BindRecording(next,target,location);
                recordedTargetId=target; recordedLocationId=location;
            } else throw std::runtime_error("Unknown recorder action");
        }
        if(guides::commit(next)) {
            if(!recordedLocationId.empty() && hasActiveQuest && activeQuest.id==recordingQuest.id) {
                recordedQuest=recordingQuest.id;
                recordedTarget=recordedTargetId; recordedLocation=recordedLocationId;
            }
            FillRecorderLocations();
            if(!recordedLocationId.empty() && !recordedLocation.empty()) {
                RecorderStatus("Saved; recorded position selected for quest guidance.");return;
            }
            if(action==214) {
                auto l=LocationById(SelectedLocation());
                RecorderStatus("Saved secret radius: "+guidejson::dump(l.at("secret_radius"))+" world units. Guidance updated immediately.");return;
            }
        }
        RecorderStatus(guides::status);
    } catch(const std::exception& e){RecorderStatus(e.what());}
}
LRESULT CALLBACK RecorderProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT client{};GetClientRect(hwnd,&client);
        FillRect(dc,&client,recorderBackgroundBrush);
        DrawRecorderFrame(dc,{8,8,client.right-8,58});
        DrawRecorderFrame(dc,{8,58,client.right-8,320});
        DrawRecorderFrame(dc,{8,320,client.right-8,642});
        DrawRecorderFrame(dc,{8,650,client.right-8,694});
        DrawRecorderFrame(dc,{8,704,client.right-8,850});
        DrawRecorderFrame(dc,{8,854,client.right-8,895});
        DrawRecorderFrame(dc,{8,898,client.right-8,client.bottom-8});
        EndPaint(hwnd,&ps);return 0;
    }
    if(msg==WM_CTLCOLORSTATIC) {
        HDC dc=reinterpret_cast<HDC>(w);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RecParchment);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    if(msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX) {
        HDC dc=reinterpret_cast<HDC>(w);HWND child=reinterpret_cast<HWND>(l);
        bool readOnly=GetDlgCtrlID(child)==RecEntityRecord;
        SetBkMode(dc,OPAQUE);SetBkColor(dc,readOnly?RecReadOnly:RecField);SetTextColor(dc,IsWindowEnabled(child)?RecParchment:RecDisabled);
        return reinterpret_cast<LRESULT>(readOnly?recorderReadOnlyBrush:recorderFieldBrush);
    }
    if(msg==WM_DRAWITEM) {
        const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);
        if(item.CtlType==ODT_BUTTON) {DrawRecorderButton(item);return TRUE;}
        if(item.CtlType==ODT_LISTBOX||item.CtlType==ODT_COMBOBOX) {DrawRecorderChoice(item);return TRUE;}
    }
    if(msg==WM_COMMAND&&LOWORD(w)==RecObjectives&&HIWORD(w)==CBN_SELCHANGE){FillRecorderLocations();RefreshQuestEntityChoice();return 0;}
    if(msg==WM_COMMAND&&LOWORD(w)==RecQuestEntities&&HIWORD(w)==CBN_SELCHANGE){ShowChosenQuestEntity();return 0;}
    if(msg==WM_COMMAND&&LOWORD(w)==RecEntityEnemy&&HIWORD(w)==BN_CLICKED){SetRecorderEnemyCheck(!recorderEnemyChecked);return 0;}
    if(msg==WM_COMMAND&&HIWORD(w)==BN_CLICKED &&
       (LOWORD(w)==RecQuestArrow||LOWORD(w)==RecShrineArrow||LOWORD(w)==RecSecretArrow)) {
        int id=LOWORD(w);auto next=guides::personal;
        const auto guide=std::atomic_load(&guides::published);
        const char* key=id==RecQuestArrow?"quest":id==RecShrineArrow?"shrine":"secret";
        bool checked=id==RecQuestArrow?guide->questArrow:id==RecShrineArrow?guide->shrineArrow:guide->secretArrow;
        next["arrow_visibility"][key]=!checked;
        if(guides::commit(next)) {RefreshArrowChecks();RecorderStatus(std::string(key)+" arrow "+(checked?"hidden":"shown")+"; saved.");}
        else RecorderStatus(guides::status);
        return 0;
    }
    if(msg==WM_COMMAND&&(LOWORD(w)==RecLocations||LOWORD(w)==RecLinkedLocations)&&HIWORD(w)==LBN_SELCHANGE) {
        SendMessageW(RecControl(LOWORD(w)==RecLocations?RecLinkedLocations:RecLocations),LB_SETCURSEL,static_cast<WPARAM>(-1),0);
        UpdateSecretRadiusControl();UpdateRecorderControls();return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(w)==RecConnections&&HIWORD(w)==LBN_SELCHANGE) {
        bool selected=SendMessageW(RecControl(RecConnections),LB_GETCURSEL,0,0)>=0;
        EnableWindow(RecControl(RecApproveConnection),selected);EnableWindow(RecControl(RecDismissConnection),selected);return 0;
    }
    if(msg==WM_COMMAND) { int command=LOWORD(w);if(command==201||command==204||command==205||command==206||command==208||command==209||(command>=211&&command<=216)||command==218||command==221||command==RecApproveConnection||command==RecDismissConnection){RecorderAction(command);return 0;}if(command==210){ShowWindow(hwnd,SW_HIDE);return 0;} }
    if(msg==WM_CLOSE || (msg==WM_COMMAND && LOWORD(w)==IDCANCEL)){ShowWindow(hwnd,SW_HIDE);return 0;}
    if(msg==WM_DESTROY){ReleaseRecorderTheme();recorderWindow=nullptr;return 0;}
    return DefWindowProcW(hwnd,msg,w,l);
}
void OpenRecorder() {
    Sample s=Snapshot();int index=ReconcileSelection(s);ULONGLONG now=GetTickCount64();
    recordingPositionValid=s.valid&&s.time&&now-s.time<=1000&&s.zoneTime&&now-s.zoneTime<=1000&&*s.zoneTag&&std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.z);
    bool captureValid=index>=0&&recordingPositionValid;
    recordingSample=s;recordingQuest=captureValid?s.quests.rows[index]:QuestRow{};recordingObjectives.clear();
    if(recordingQuest.detailsValid&&recordingQuest.details)for(const auto& r:*recordingQuest.details)if(r.kind==DetailKind::Objective&&r.taskState==2&&r.objectiveState==2)recordingObjectives.push_back(r);
    if(!recorderWindow) {
        WNDCLASSW cls{};cls.lpfnWndProc=RecorderProc;cls.hInstance=module;cls.lpszClassName=L"GDGuideRecorder";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=nullptr;RegisterClassW(&cls);
        recorderWindow=CreateWindowExW(WS_EX_TOPMOST,cls.lpszClassName,L"Guide recorder",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,150,40,780,990,nullptr,nullptr,module,nullptr);
        if(!recorderWindow){Log("Recorder window creation failed");return;}
        InitializeRecorderTheme(recorderWindow);
        auto control=[&](const wchar_t* type,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style=0){
            if(wcscmp(type,L"BUTTON")==0) style|=BS_OWNERDRAW;
            if(wcscmp(type,L"LISTBOX")==0) style|=LBS_OWNERDRAWFIXED|LBS_HASSTRINGS;
            if(wcscmp(type,L"COMBOBOX")==0) style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
            HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,recorderWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),module,nullptr);
            SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(recorderBodyFont),TRUE);ThemeNativeRecorderControl(c);return c;
        };
        HWND header=control(L"STATIC",L"",107,16,12,730,45);SendMessageW(header,WM_SETFONT,reinterpret_cast<WPARAM>(recorderHeaderFont),TRUE);
        HWND objectiveHeading=control(L"STATIC",L"UNFINISHED OBJECTIVE",0,16,62,260,22);SendMessageW(objectiveHeading,WM_SETFONT,reinterpret_cast<WPARAM>(recorderHeadingFont),TRUE);
        control(L"COMBOBOX",L"",RecObjectives,16,86,730,220,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        SendMessageW(RecControl(RecObjectives),CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),25);SendMessageW(RecControl(RecObjectives),CB_SETITEMHEIGHT,0,23);
        control(L"STATIC",L"Optional location name",0,16,122,180,22);control(L"EDIT",L"",RecName,200,118,546,26,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);
        SendMessageW(RecControl(RecName),EM_SETLIMITTEXT,255,0);
        control(L"BUTTON",L"Record location",201,16,157,225,30,WS_TABSTOP);
        control(L"STATIC",L"Arrow Guide:",0,380,162,105,24);
        control(L"BUTTON",L"Quest",RecQuestArrow,490,160,75,27,WS_TABSTOP);
        control(L"BUTTON",L"Shrine",RecShrineArrow,575,160,80,27,WS_TABSTOP);
        control(L"BUTTON",L"Secret",RecSecretArrow,665,160,81,27,WS_TABSTOP);
        HWND trackingHeading=control(L"STATIC",L"QUEST CHARACTER FOR LIVE TRACKING",0,16,201,730,20);SendMessageW(trackingHeading,WM_SETFONT,reinterpret_cast<WPARAM>(recorderHeadingFont),TRUE);
        control(L"COMBOBOX",L"",RecQuestEntities,16,224,730,220,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        SendMessageW(RecControl(RecQuestEntities),CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),25);SendMessageW(RecControl(RecQuestEntities),CB_SETITEMHEIGHT,0,23);
        control(L"BUTTON",L"Enable live tracking",218,16,257,225,29,WS_TABSTOP);
        control(L"BUTTON",L"Disable live tracking",221,255,257,225,29,WS_TABSTOP);
        control(L"BUTTON",L"Enemy / death-aware",RecEntityEnemy,500,258,246,27,WS_TABSTOP);
        control(L"EDIT",L"",RecEntityRecord,16,292,730,25,WS_BORDER|ES_AUTOHSCROLL|ES_READONLY);
        control(L"STATIC",L"Linked waypoints (travel order)",0,16,328,354,22);
        control(L"STATIC",L"Other saved locations",0,390,328,356,22);
        control(L"LISTBOX",L"",RecLinkedLocations,16,353,354,194,WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|WS_TABSTOP);
        control(L"LISTBOX",L"",RecLocations,390,353,356,194,WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|WS_TABSTOP);
        SendMessageW(RecControl(RecLinkedLocations),LB_SETITEMHEIGHT,0,23);SendMessageW(RecControl(RecLocations),LB_SETITEMHEIGHT,0,23);
        SendMessageW(RecControl(RecLinkedLocations),LB_SETHORIZONTALEXTENT,1000,0);
        SendMessageW(RecControl(RecLocations),LB_SETHORIZONTALEXTENT,1000,0);
        control(L"BUTTON",L"Use for objective",204,16,560,230,30,WS_TABSTOP);
        control(L"BUTTON",L"Rename everywhere",205,258,560,230,30,WS_TABSTOP);
        control(L"BUTTON",L"Unlink from objective",206,500,560,246,30,WS_TABSTOP);
        control(L"BUTTON",L"Replace position everywhere",211,16,597,225,30,WS_TABSTOP);control(L"BUTTON",L"Delete location everywhere",212,255,597,225,30,WS_TABSTOP);
        control(L"BUTTON",L"↑ Move up",215,490,597,118,30,WS_TABSTOP);control(L"BUTTON",L"↓ Move down",216,620,597,126,30,WS_TABSTOP);
        control(L"BUTTON",L"Record secret entrance",213,16,657,225,30,WS_TABSTOP);
        control(L"STATIC",L"Radius (units)",0,255,663,100,24);
        control(L"EDIT",L"25",RecSecretRadius,358,659,68,26,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);
        SendMessageW(RecControl(RecSecretRadius),EM_SETLIMITTEXT,16,0);
        control(L"BUTTON",L"Set selected secret radius",214,448,657,298,30,WS_TABSTOP);
        HWND crossingHeading=control(L"STATIC",L"OBSERVED OUTDOOR CROSSINGS  (REVIEW BEFORE ADDING TO GUIDE)",0,16,712,730,22);SendMessageW(crossingHeading,WM_SETFONT,reinterpret_cast<WPARAM>(recorderHeadingFont),TRUE);
        control(L"LISTBOX",L"",RecConnections,16,736,730,75,WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_TABSTOP);
        SendMessageW(RecControl(RecConnections),LB_SETITEMHEIGHT,0,23);
        SendMessageW(RecControl(RecConnections),LB_SETHORIZONTALEXTENT,1100,0);
        control(L"BUTTON",L"Save selected connection",RecApproveConnection,16,813,350,30,WS_TABSTOP);
        control(L"BUTTON",L"Dismiss proposal",RecDismissConnection,390,813,356,30,WS_TABSTOP);
        control(L"BUTTON",L"Undo last change",208,16,861,350,30,WS_TABSTOP);
        control(L"BUTTON",L"Reload guide",209,390,861,356,30,WS_TABSTOP);
        control(L"BUTTON",L"Close",210,562,914,184,30,WS_TABSTOP);
    }
    UpdateRecorderHeader();RefreshArrowChecks();
    // Match the compass selection by quest/task/objective IDs, including after
    // guide reloads. Unmapped quests retain the first unfinished-row default.
    ReconcileGuide(s.guide);
    size_t objectiveSelection=0;
    SendMessageW(RecControl(RecObjectives),CB_RESETCONTENT,0,0);
    for(size_t i=0;i<recordingObjectives.size();++i) {
        const auto& row=recordingObjectives[i];
        SendMessageW(RecControl(RecObjectives),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(ObjectiveLabel(row).c_str()));
        if(IsSelectedObjective(*s.guide,recordingQuest,row)) objectiveSelection=i;
    }
    SendMessageW(RecControl(RecObjectives),CB_SETCURSEL,objectiveSelection,0);
    LoadQuestEntityChoices();RefreshQuestEntityChoice();
    SetWindowTextW(RecControl(RecName),L"");FillRecorderLocations();FillObservedConnections();RecorderStatus(!recordingPositionValid?"No fresh position. Close and reopen in the game to record.":!captureValid?"Position ready for secret recording; no selected quest. Secrets do not need an objective.":!recorderEntityError.empty()?recorderEntityError:guides::status.find("failed")!=std::string::npos||!std::atomic_load(&guides::published)->diagnostics.empty()?guides::status:"Capture ready. Select a quest character to enable live tracking; changes save immediately.");ShowWindow(recorderWindow,SW_SHOW);SetForegroundWindow(recorderWindow);
}
