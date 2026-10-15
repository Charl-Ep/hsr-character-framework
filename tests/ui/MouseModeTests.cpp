#include <hksr/control/MouseMode.hpp>
#include <cstddef>
#include <stdexcept>

namespace {
using namespace hksr::api;
struct Mouse {int pad=0;bool forced=false,mode=false;};
Mouse object;bool visible=false,alt=false,allowed=true;int locked=1,roots=0,forces=0,closes=0,refreshes=0,token=0;
float rotation_delta=0;
void Check(bool b){if(!b)throw std::runtime_error("mouse mode contract test failed");}
void* __fastcall Instance(void*){return &object;}
__declspec(noinline) void __fastcall Force(Mouse* p,void*){++forces;if(allowed){p->forced=true;p->mode=true;visible=true;locked=0;rotation_delta=0;}}
__declspec(noinline) void __fastcall SdkForce(Mouse* p,void*){if(allowed){p->forced=true;p->mode=true;visible=true;locked=0;}}
__declspec(noinline) void __fastcall Close(Mouse* p,void*){++closes;p->forced=false;}
void __fastcall Hide(Mouse* p,void*){if(!p->forced && !alt){p->mode=false;visible=false;locked=1;}}
void __fastcall Refresh(Mouse*,void*){++refreshes;visible=true;locked=0;}
bool __fastcall Mode(Mouse* p,void*){return p->mode;}
bool __fastcall Visible(void*){return visible;}
int __fastcall Locked(void*){return locked;}
int __fastcall Keep(void* p,int handle,int kind,void*){Check(p==&object && !handle && kind==2);++roots;return 1;}
void __fastcall Free(int handle,void*){Check(handle==1);--roots;}
template<class F> Method Bind(F fn){return {reinterpret_cast<void*>(fn),&token};}
}
void TestMouseMode() {
    MouseModeMethods m{Bind(Instance),Bind(Force),Bind(SdkForce),Bind(Close),Bind(Hide),Bind(Refresh),Bind(Mode),Bind(Visible),Bind(Locked),offsetof(Mouse,forced)};
    ReferenceMethods refs;refs.retain=Bind(Keep);refs.free=Bind(Free);
    ObserveMouseModeOwners(m);MouseModeLease lease(m,refs);
    lease.Service(true);Check(object.forced && visible && !locked && roots==1 && forces==1);
    lease.Service(true);Check(forces==1);
    // Actual adventure update inlines CloseForceShowCursor's flag store.
    // Mode stays active; repeating Force would reset already submitted input.
    for(int frame=0;frame<5;++frame) {
        object.forced=false;rotation_delta=2.0f;lease.Service(true);
        Check(forces==1 && rotation_delta==2.0f && object.mode && visible && !locked);
    }
    // A real mode loss still needs one reacquisition, not a permanent latch.
    object.mode=false;visible=false;locked=1;lease.Service(true);Check(forces==2 && object.forced && object.mode && visible && !locked);
    visible=false;locked=1;lease.Service(true);Check(visible && !locked && refreshes==1);
    SdkForce(&object,&token);lease.Service(false);Check(object.forced && roots==0 && closes==0);
    Close(&object,&token);Hide(&object,&token);
    lease.Service(true);Check(object.forced && roots==1);
    Close(&object,&token);const auto before=forces;lease.Service(true);Check(object.mode && !object.forced && forces==before);
    alt=true;lease.Service(false);Check(!object.forced && visible && roots==0);
    alt=false;Hide(&object,&token);Check(!visible && locked==1);
    Force(&object,&token);const int count=closes;lease.Service(true);lease.Service(false);Check(object.forced && closes==count && roots==0);
    Close(&object,&token);Hide(&object,&token);
    allowed=false;lease.Service(true);Check(!object.forced && !visible);lease.Service(false);Check(roots==0);
    allowed=true;lease.Service(true);lease.Service(false);Check(!object.forced && !visible && roots==0);
}
