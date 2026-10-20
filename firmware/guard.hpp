#pragma once
#include <cmath>
#include <deque>
#include <string>

// Todos os tempos da politica sao segundos simulados.
struct Plant {
  double t=0, air=-19, food=-19;
  bool door=false, failure=false, sensor=false;
  bool comp=false, fan=false, heat=false;
  void step() {
    t+=1;
    double cool=(comp && !failure) ? .04 : 0;
    if(!fan) cool*=.4;
    air+=.00018*(25-air)+(door?.05:0)+(heat?.06:0)-cool;
    food+=(air-food)/600;
  }
  void reset() {
    air=food=-19; door=failure=sensor=false;
    comp=fan=heat=false; // t monotonic; nao apaga bloqueio
  }
};
struct Output {
  bool comp=false, fan=false, heat=false, locked=false, valid=true;
  double rate=0;
  std::string mode="NORMAL", reason="NORMAL";
};
class Guard {
  bool comp=false, locked=false;
  double switched=0, defrostAt=-1, lastDefrost=-900;
  double highAt=-1, safeAt=-1, lockAt=-1;
  std::string reason="NORMAL";
  std::deque<std::pair<double,double>> history;
  void trip(const std::string& why,double t) {
    if(!locked) { reason=why; lockAt=t; }
    locked=true;
  }
 public:
  Output update(const Plant& p,double target=-19,bool defrost=false,
                bool bad=false,bool ack=false) {
    Output o;
    double temp=p.air+.025;
    if(p.sensor || !std::isfinite(temp) || temp < -50 || temp > 50) {
      trip("SENSOR_FAULT",p.t); comp=false; switched=p.t;
      safeAt=-1; defrostAt=-1; history.clear();
      o.locked=true; o.valid=false;
      o.mode=o.reason="SENSOR_FAULT"; return o;
    }
    history.push_back({p.t,temp});
    while(history.size()>1 && p.t-history.front().first>60)
      history.pop_front();
    double span=p.t-history.front().first;
    double rate=span>=30 ? (temp-history.front().second)/span : 0;
    if(bad) trip("INVALID_REQUEST",p.t);
    if(!std::isfinite(target) || target < -23 || target > -16)
      trip("SETPOINT_BLOCKED",p.t);
    if(temp>=-12) trip("HIGH_TEMPERATURE",p.t);
    if(temp>=-15) { if(highAt<0) highAt=p.t; }
    else highAt=-1;
    if(highAt>=0 && p.t-highAt>=30) trip("WARM_TOO_LONG",p.t);
    if(rate>.03 && !p.door && defrostAt<0) trip("RAPID_RISE",p.t);
    bool safe=temp<=-18 && !bad && target>=-23 && target<=-16 && !defrost;
    if(safe) { if(safeAt<0) safeAt=p.t; } else safeAt=-1;
    if(ack && safeAt>=0 && p.t-safeAt>=60) {
      locked=false; reason="NORMAL"; lockAt=-1;
    }
    if(locked) { target=-19; defrost=false; }
    if(defrost && defrostAt<0) {
      if(p.t-lastDefrost<900) { trip("DEFROST_TOO_SOON",p.t); defrost=false; }
      else { defrostAt=lastDefrost=p.t; }
    }
    if(defrostAt>=0 && p.t-defrostAt>=120) {
      trip("DEFROST_TIMEOUT",p.t); defrost=false;
    }
    if(locked || !defrost) {
      if(defrostAt>=0) { history.clear(); rate=0; }
      defrostAt=-1;
    }
    bool desired=comp;
    if(defrostAt>=0) desired=false;
    else if(temp>=target+1) desired=true;
    else if(temp<=target-1) desired=false;
    if(desired!=comp && p.t-switched>=30) { comp=desired; switched=p.t; }
    o.comp=comp; o.heat=defrostAt>=0 && !comp; o.fan=comp && !o.heat;
    o.locked=locked; o.reason=reason; o.rate=rate*60;
    o.mode=locked?"PROTECTION":"NORMAL";
    if(locked && lockAt>=0 && p.t-lockAt>=120 && comp && rate>=0)
      o.mode="RECOVERY_FAILED";
    return o;
  }
};
