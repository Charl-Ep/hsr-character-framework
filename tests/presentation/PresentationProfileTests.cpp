#include <hksr/framework/presentation/PresentationProfileFiles.hpp>
#include <stdexcept>
#include <limits>
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>

namespace {
void Check(bool value){if(!value)throw std::runtime_error("presentation profile test failed");}
template<class F>void Reject(F fn){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}Check(caught);}
}
void TestPresentationProfiles() {
    using namespace hksr::framework;
    const std::string fixture=R"({"format":"hksr.presentation-profile","id":"fixture","source":"Official/Fixture","initial_phase":"normal",
      "phases":{"normal":{"idle":"IdleA"},"changed":{"idle":"IdleB"}},
      "actions":{"Enter":{"cues":[{"at":0.6,"phase":"changed"}],"complete_at":0.9},
        "Exit":{"on_complete":"normal"},"Wait":{"return":"hold"},
        "ReadyB":{"on_enter":"changed"},"ResetOnBreak":{"on_interrupt":"normal"}}})";
    auto profile=std::make_shared<const PresentationProfile>(ParsePresentationProfile(fixture));
    PresentationState first,second;first.Configure(profile);second.Configure(profile);
    first.Begin("Enter");first.Observe(0.59f);Check(first.Phase()=="normal");
    first.Observe(0.8f);Check(first.Phase()=="changed" && second.Phase()=="normal");
    first.Observe(0.2f);Check(first.Phase()=="changed"); // progress restart cannot replay an earlier cue
    Check(first.CompletionTime()==0.9f && first.ReturnToIdle());first.Complete();
    Check(first.Idle("fallback")=="IdleB");
    first.Begin("Unknown");first.Interrupt();Check(first.Phase()=="changed");
    first.Begin("Exit");first.Interrupt();Check(first.Phase()=="changed"); // interrupted completion is not a completion event
    first.Begin("Exit");first.Complete();Check(first.Phase()=="normal");
    first.Begin("ReadyB");first.Begin("Wait");Check(first.Phase()=="changed" && !first.ReturnToIdle());
    first.Begin("ResetOnBreak");first.Interrupt();Check(first.Phase()=="normal");
    first.Begin("ReadyB");first.LeaveDomain();Check(first.Phase()=="normal");
    first.Configure(profile,"changed");Check(first.Idle("fallback")=="IdleB");
    first.Configure(profile,"removed");Check(first.Phase()=="normal" && !first.Note().empty());
    first.Configure(nullptr);Check(first.Idle("fallback")=="fallback" && first.Phase().empty());
    first.Observe(std::numeric_limits<float>::infinity());
    auto bad=nlohmann::json::parse(fixture);bad["actions"]["Enter"]["cues"][0]["at"]=-1;Reject([&]{ParsePresentationProfile(bad.dump());});
    bad=nlohmann::json::parse(fixture);bad["actions"]["Enter"]["cues"][0]["phase"]="missing";Reject([&]{ParsePresentationProfile(bad.dump());});
    bad=nlohmann::json::parse(fixture);bad["actions"]["Wait"]["return"]="guess";Reject([&]{ParsePresentationProfile(bad.dump());});
    bad=nlohmann::json::parse(fixture);bad["actions"]["Enter"]["cues"][0]["at"]=2;Reject([&]{ParsePresentationProfile(bad.dump());});
    auto keep=std::make_shared<PresentationProfile>(*profile);keep->reset_on_domain_exit=false;
    first.Configure(keep,"changed");first.LeaveDomain();Check(first.Phase()=="changed");
    Check(ReadPresentationProfiles("a-nonexistent-profile-test-directory").sources.empty());
    const auto directory=std::filesystem::temp_directory_path()/("hksr-profile-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{directory};
    {std::ofstream file(directory/"valid.json");file<<fixture;}
    {std::ofstream file(directory/"broken.json");file<<"{bad";}
    auto loaded=ReadPresentationProfiles(directory);Check(loaded.sources.size()==1 && loaded.errors.size()==1);
    {std::ofstream file(directory/"duplicate.json");file<<fixture;}
    loaded=ReadPresentationProfiles(directory);Check(loaded.sources.empty() && loaded.errors.size()==2);
    const auto shipped=ReadPresentationProfiles(HKSR_PROFILE_DATA_DIR);
    Check(shipped.errors.empty() && !shipped.sources.empty());
    const auto sparxie=shipped.sources.at("Config/ConfigCharacter/Avatar/Avatar_Sparxie_00_Config.json");
    Check(sparxie->native_events.size()==3 && sparxie->native_events[0].scope=="gift-scene");
    auto native=nlohmann::json::parse(fixture);
    native["native_events"]={{{"file","test.json"},{"part","time_point"},{"states",{"A","B"}},{"scope","scene"},{"key","event"}}};
    Check(ParsePresentationProfile(native.dump()).native_events[0].states==std::vector<std::string>{"A","B"});
    auto duplicate=native;duplicate["native_events"].push_back(native["native_events"][0]);Reject([&]{ParsePresentationProfile(duplicate.dump());});
    auto invalid=native;invalid["native_events"][0]["states"]={"A","A"};Reject([&]{ParsePresentationProfile(invalid.dump());});
    invalid=native;invalid["native_events"][0]["part"]="guess";Reject([&]{ParsePresentationProfile(invalid.dump());});
    native["native_events"][0].erase("states");native["native_events"][0]["part"]="behavior";native["native_events"][0]["behavior"]="key";
    Check(ParsePresentationProfile(native.dump()).native_events[0].behavior=="key");
}
