#include <hksr/framework/assets/TextureObjects.hpp>
#include <Windows.h>
#include <map>
#include <stdexcept>
#include <iostream>

namespace {
using namespace hksr;
struct Texture { bool alive=false; unsigned initialized=0,loaded=0,applied=0,settings=0; };
int next=0,info;
std::map<int,void*> roots;
bool fail_ctor=false, reject_data=false, fail_setting=false;
void Check(bool value) { if(!value) throw std::runtime_error("Framework texture preparation mismatch"); }
int __fastcall Keep(void* object,int zero,int kind,void*) { Check(!zero && kind==2); roots[++next]=object; return next; }
void* __fastcall Target(int id,void*) { return roots.at(id); }
void __fastcall Free(int id,void*) { roots.erase(id); }
bool __fastcall Alive(Texture* object,void*) { return object->alive; }
void __fastcall Destroy(Texture* object,void*) { object->alive=false; }
void __fastcall Construct(Texture* object,int w,int h,int format,int mips,bool linear,void* external,void* metadata) {
    Check(metadata==&info && !external && w==2 && h==2 && format==4 && mips==1 && linear && !roots.empty());
    object->alive=true; ++object->initialized;
    if(fail_ctor) RaiseException(0xE0550001,0,0,nullptr);
}
bool __fastcall Load(Texture* object,const void* data,int size,void* metadata) {
    Check(metadata==&info && size==16 && static_cast<const unsigned char*>(data)[3]==255); ++object->loaded; return !reject_data;
}
void __fastcall Apply(Texture* object,bool update,bool discard,void* metadata) { Check(metadata==&info && !update && discard); ++object->applied; }
template<int Expected> void __fastcall Setting(Texture* object,int value,void* metadata) {
    Check(metadata==&info && value==Expected); ++object->settings;
    if(fail_setting && Expected==5) RaiseException(0xE0550002,0,0,nullptr);
}
void __fastcall Bias(Texture* object,float value,void* metadata) { Check(metadata==&info && value==0.5f); ++object->settings; }
template<class T> api::Method M(T function) { return {reinterpret_cast<void*>(function),&info}; }
}
void TestTextureObjects() {
    api::OwnedUnityObjects owner({M(Alive),M(Destroy)},{M(Keep),M(Target),M(Free),{}, {}});
    api::TextureAccess access({M(Construct),M(Load),M(Apply),M(Setting<1>),M(Setting<5>),M(Bias),M(Setting<0>),M(Setting<1>),M(Setting<2>)});
    framework::TextureDescription d; d.width=2; d.height=2; d.format=4; d.mip_count=1; d.linear=true;
    d.filter=1; d.anisotropy=5; d.mip_bias=0.5f; d.wrap={0,1,2}; d.payload_bytes=16;
    std::vector<std::uint8_t> payload(16,255);
    auto close=[&](api::UnityObjectId id) { owner.Close(id); Check(owner.Close(id).complete); owner.Forget(id); Check(roots.empty()); };
    Texture good;
    const auto a=framework::PrepareTexture(owner,access,&good,d,payload);
    Check(a.ready && a.error.empty() && good.settings==6 && good.applied==1 && roots.size()==1); close(a.owned);
    Texture partial; fail_ctor=true;
    const auto b=framework::PrepareTexture(owner,access,&partial,d,payload);
    Check(!b.ready && b.initialize.exception==0xE0550001 && partial.alive && partial.loaded==0 && roots.size()==1); close(b.owned); fail_ctor=false;
    Texture rejected; reject_data=true;
    const auto c=framework::PrepareTexture(owner,access,&rejected,d,payload);
    Check(!c.ready && c.load.read.returned && !c.load.value && rejected.settings==0 && rejected.applied==0); close(c.owned); reject_data=false;
    Texture settings; fail_setting=true;
    const auto e=framework::PrepareTexture(owner,access,&settings,d,payload);
    Check(!e.ready && e.sampling.aniso.exception==0xE0550002 && settings.settings==6 && settings.applied==0); close(e.owned); fail_setting=false;
    Texture invalid; d.payload_bytes=15;
    const auto f=framework::PrepareTexture(owner,access,&invalid,d,payload);
    Check(!f.ready && invalid.initialized==0 && roots.size()==1); close(f.owned);
    std::cout<<"Framework texture transaction passed: private rooting, constructor/upload/sampling failures, no regenerated mips.\n";
}
