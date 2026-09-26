#pragma once
#include <cmath>
#include <optional>
#include <string>

namespace connections {
struct Observation {
    std::string from, to;
    std::string source="in-game zone observation";
    std::wstring fromName, toName;
    double fromX=0, fromY=0, fromZ=0, toX=0, toY=0, toZ=0;
    unsigned long long tick=0;
    bool dismissed=false;
};
inline bool samePair(const Observation& a,const Observation& b) {
    return (a.from==b.from && a.to==b.to) || (a.from==b.to && a.to==b.from);
}
inline bool outdoor(const std::string& tag) {
    if(tag.rfind("tagMap",0)!=0 || tag.size()>=256) return false;
    for(unsigned char c:tag) if(c<33 || c>126) return false;
    return true;
}
inline double horizontal(double ax,double az,double bx,double bz) {return std::hypot(ax-bx,az-bz);}
class Detector {
    Observation previous{}, pending{};
    unsigned long long previousTick=0, crossingTick=0, blockedUntil=0;
    bool hasPrevious=false, hasPending=false;
public:
    std::optional<Observation> observe(unsigned long long tick,const char* zone,const wchar_t* name,
                                       double x,double y,double z) {
        if(!zone || !*zone || !name || !*name || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
           (hasPrevious && (tick<previousTick || tick-previousTick>1500))) {
            hasPrevious=hasPending=false;blockedUntil=tick+5000;return {};
        }
        Observation current{};current.from=zone;current.fromName=name;
        current.fromX=x;current.fromY=y;current.fromZ=z;current.tick=tick;
        // Fast travel can move the player before the displayed zone tag changes.
        // Remember a recent discontinuity so an unchanged destination position
        // cannot masquerade as a seamless boundary crossing moments later.
        if(hasPrevious && (horizontal(previous.fromX,previous.fromZ,x,z)>40 || std::abs(previous.fromY-y)>12)) {
            hasPending=false;blockedUntil=tick+5000;
        }
        if(tick<blockedUntil) {
            previous=current;previousTick=tick;hasPrevious=true;hasPending=false;return {};
        }
        std::optional<Observation> found;
        if(hasPending) {
            if(current.from!=pending.to || tick-crossingTick>2500 ||
               horizontal(x,z,pending.fromX,pending.fromZ)>50 || std::abs(y-pending.fromY)>16) hasPending=false;
            else if(tick-crossingTick>=300) {
                pending.toX=x;pending.toY=y;pending.toZ=z;
                found=pending;hasPending=false;
            }
        }
        if(hasPrevious && !hasPending && !found && previous.from!=current.from &&
           outdoor(previous.from) && outdoor(current.from) &&
           horizontal(previous.fromX,previous.fromZ,x,z)<=20 && std::abs(previous.fromY-y)<=8) {
            pending=previous;pending.to=current.from;pending.toName=current.fromName;
            pending.tick=tick;crossingTick=tick;hasPending=true;
        }
        previous=current;previousTick=tick;hasPrevious=true;
        return found;
    }
};
}
