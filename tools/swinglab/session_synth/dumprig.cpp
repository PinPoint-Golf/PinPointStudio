#include "skeleton3d_rig.h"
#include <cstdio>
using namespace pinpoint::skeleton3d;
int main(){const Rig&R=rig();
 printf("{\"N\":%d,\"joints\":[",Rig::N);
 for(int j=0;j<Rig::N;j++){auto t=R.restT[j];auto q=R.restQ[j];auto n=R.neutral[j];
  printf("%s{\"name\":\"%s\",\"parent\":%d,\"group\":%d,\"t\":[%.9f,%.9f,%.9f],\"q\":[%.9f,%.9f,%.9f,%.9f],\"n\":[%.9f,%.9f,%.9f,%.9f],\"first\":%d,\"nd\":%d}",j?",":"",ybot::kJoints[j].name,R.parent[j],jointGroup(j),t.x,t.y,t.z,q.w,q.x,q.y,q.z,n.w,n.x,n.y,n.z,R.firstDof[j],R.nDof[j]);}
 printf("],\"rigToWorld\":[%.9f,%.9f,%.9f,%.9f],\"dofs\":[",R.rigToWorld.w,R.rigToWorld.x,R.rigToWorld.y,R.rigToWorld.z);
 for(int k=0;k<R.dofCount();k++){auto&d=R.dofs[k];printf("%s{\"name\":\"%s\",\"joint\":%d,\"kind\":%d,\"axis\":[%.9f,%.9f,%.9f]}",k?",":"",d.name,d.joint,int(d.kind),d.axis.x,d.axis.y,d.axis.z);}
 printf("],\"groups\":[");for(int g=0;g<GroupCount;g++)printf("%s\"%s\"",g?",":"",groupName(g));printf("]}\n");}
