#include <hksr/framework/assets/ReferenceArrays.hpp>
#include <Windows.h>
#include <map>
#include <stdexcept>
#include <iostream>
namespace {
using namespace hksr;
int info,type,art,other_art,path_a,path_b,path_missing,bone_a,bone_b,next=0,fail_index=-1,queries=0;
std::vector<void*> data;
std::map<int,void*> held;
void Check(bool ok) { if(!ok) throw std::runtime_error("reference array transaction mismatch"); }
template<class T> api::Method M(T f) { return {reinterpret_cast<void*>(f),&info}; }
int __fastcall Keep(void* p,int zero,int kind,void*) { Check(!zero && kind==2); held[++next]=p; return next; }
void* __fastcall Target(int id,void*) { return held.at(id); }
void __fastcall Free(int id,void*) { held.erase(id); }
void* __fastcall Create(void* t,int count,void* meta) { Check(t==&type && meta==&info); data.assign(count,nullptr); return &data; }
void __fastcall Set(void* array,void* value,int index,void* meta) {
    Check(array==&data && meta==&info && !held.empty()); data.at(index)=value;
    if(index==fail_index) RaiseException(0xE0590001,0,0,nullptr);
}
void* __fastcall Find(void* root,void* path,void* meta) {
    Check(meta==&info && root!=&other_art); ++queries;
    if(path==&path_a) return &bone_a;
    if(path==&path_b) return &bone_b;
    Check(path==&path_missing); return nullptr;
}
}
void TestReferenceArrays() {
    api::ManagedRoots roots({M(Keep),M(Target),M(Free),{}, {}});
    api::ManagedArrays arrays({M(Create),{}, {},M(Set)});
    api::HierarchyAccess hierarchy({M(Find)});
    const auto close=[&](api::ManagedRootId id) { Check(roots.Release(id).complete); roots.Forget(id); Check(held.empty()); };
    const auto slots=framework::PrepareReferenceArray(roots,arrays,&type,{&bone_b,nullptr,&bone_a});
    Check(slots.ready && data==std::vector<void*>({&bone_b,nullptr,&bone_a})); close(slots.root);
    fail_index=0;
    const auto partial=framework::PrepareReferenceArray(roots,arrays,&type,{&bone_a,&bone_b}); fail_index=-1;
    Check(!partial.ready && partial.writes.size()==2 && partial.writes[0].exception==0xE0590001 && partial.writes[1].returned && data[1]==&bone_b); close(partial.root);
    const auto empty=framework::PrepareReferenceArray(roots,arrays,&type,{}); Check(empty.ready && data.empty()); close(empty.root);
    const auto palette=framework::PrepareBonePalette(roots,arrays,hierarchy,&art,&type,{&path_b,&path_a});
    Check(palette.ready && queries==2 && data==std::vector<void*>({&bone_b,&bone_a})); close(palette.array.root);
    const auto missing=framework::PrepareBonePalette(roots,arrays,hierarchy,&art,&type,{&path_missing,&path_a,&path_b});
    Check(!missing.ready && queries==5 && missing.bones.size()==3 && missing.bones[0].call.returned && !missing.bones[0].object && missing.bones[2].object==&bone_b && held.empty());
    std::cout<<"Reference array tests passed: official writes, partial results, authored bone ordering and no fallback.\n";
}
