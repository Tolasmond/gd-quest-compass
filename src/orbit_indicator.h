// Window-thread geometry: no game objects or graphics calls in the game hook.
constexpr int OrbitWindowSize = 360;
constexpr double ArrowArrivalDistance = 5.0;
struct OrbitGeometry { bool visible=false; int left=0,top=0; POINT points[3]{}; };
bool OrbitInputsValid(const Sample& s,const CompassReading& bearing,int width,int height,ULONGLONG now) {
    if(!s.valid||!s.time||now-s.time>1000||!s.screenValid||!bearing.valid||!bearing.bearingValid||
       s.viewportWidth!=width||s.viewportHeight!=height||width<=0||height<=0||
       !std::isfinite(s.screenX)||!std::isfinite(s.screenY)||s.screenX<0||s.screenX>=width||s.screenY<0||s.screenY>=height)return false;
    return true;
}
OrbitGeometry OrbitFor(const Sample& s,const CompassReading& bearing,int width,int height,ULONGLONG now,double radiusOffset=0) {
    OrbitGeometry g;
    if(!OrbitInputsValid(s,bearing,width,height,now))return g;
    if(!std::isfinite(bearing.distance)||bearing.distance<=ArrowArrivalDistance)return g;
    double length=std::hypot(bearing.screenRight,bearing.screenUp);
    if(!std::isfinite(length)||length<0.001)return g; // No invented direction at zero distance.
    double ux=bearing.screenRight/length,uy=-bearing.screenUp/length;
    double radius=std::clamp(std::min(width,height)*0.085,52.0,112.0)+28.0+radiusOffset;
    double cx=OrbitWindowSize/2+ux*radius,cy=OrbitWindowSize/2+uy*radius;
    auto point=[](double x,double y){return POINT{static_cast<LONG>(std::lround(x)),static_cast<LONG>(std::lround(y))};};
    g.points[0]=point(cx+ux*15,cy+uy*15);
    g.points[1]=point(cx-ux*10-uy*10,cy-uy*10+ux*10);
    g.points[2]=point(cx-ux*10+uy*10,cy-uy*10-ux*10);
    g.left=static_cast<int>(std::lround(s.screenX))-OrbitWindowSize/2;
    g.top=static_cast<int>(std::lround(s.screenY))-OrbitWindowSize/2;
    for(const auto& p:g.points) if(p.x+g.left<0||p.x+g.left>=width||p.y+g.top<0||p.y+g.top>=height)return g;
    g.visible=true;return g;
}
HWND orbitWindow=nullptr;
OrbitGeometry orbitGeometry,secretGeometry,shrineGeometry;
const OrbitGeometry& OrbitAnchor(const OrbitGeometry& quest,const OrbitGeometry& secret,const OrbitGeometry& shrine) {
    return quest.visible?quest:secret.visible?secret:shrine;
}
void PaintTriangle(HDC dc,const OrbitGeometry& g,COLORREF color) {
    if(!g.visible)return;
    HBRUSH brush=CreateSolidBrush(color);
    auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
    Polygon(dc,g.points,3);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);
}
void PaintOrbit(HDC dc,const RECT& bounds,const OrbitGeometry& g,const OrbitGeometry& secret=OrbitGeometry{},
                const OrbitGeometry& shrine=OrbitGeometry{}) {
    FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    PaintTriangle(dc,g,RGB(32,224,88));
    PaintTriangle(dc,secret,RGB(255,216,32));
    PaintTriangle(dc,shrine,RGB(32,220,235));
}
LRESULT CALLBACK OrbitProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_NCHITTEST)return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        // Draw black transparency and the triangle together, avoiding erase flicker.
        HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);
        if(buffer&&bitmap){auto old=SelectObject(buffer,bitmap);PaintOrbit(buffer,r,orbitGeometry,secretGeometry,shrineGeometry);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);}
        else PaintOrbit(dc,r,orbitGeometry,secretGeometry,shrineGeometry);
        if(bitmap)DeleteObject(bitmap);if(buffer)DeleteDC(buffer);
        EndPaint(hwnd,&ps);return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
