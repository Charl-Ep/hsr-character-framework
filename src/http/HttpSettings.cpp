#include <hksr/framework/http/HttpControl.hpp>
#include <fstream>
#include <set>
#include <charconv>

namespace hksr::framework {
namespace {
std::string Trim(std::string text) {
    const auto begin=text.find_first_not_of(" \t\r\n");
    if(begin==std::string::npos)return {};
    return text.substr(begin,text.find_last_not_of(" \t\r\n")-begin+1);
}
}
HttpSettings ReadHttpSettings(const std::filesystem::path& path) {
    HttpSettings out;
    if(!std::filesystem::exists(path))return out;
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("cannot read http-settings.cfg");
    std::string line;std::set<std::string> seen;bool section=false;unsigned number=0;
    while(std::getline(input,line)) {
        ++number;
        if(number==1 && line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
        line=Trim(line);
        if(line.empty() || line[0]=='#' || line[0]==';')continue;
        if(line=="[http]") {section=true;continue;}
        const auto split=line.find('=');
        if(!section || split==std::string::npos)throw std::invalid_argument("http-settings.cfg: expected [http] key=value at line "+std::to_string(number));
        const auto key=Trim(line.substr(0,split)),value=Trim(line.substr(split+1));
        if(!seen.insert(key).second)throw std::invalid_argument("http-settings.cfg: duplicate "+key);
        if(key=="enabled") {
            if(value!="true" && value!="false")throw std::invalid_argument("http enabled must be true or false");
            out.enabled=value=="true";
        } else if(key=="address")out.address=value;
        else if(key=="port") {
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),out.port);
            if(parsed.ec!=std::errc() || parsed.ptr!=value.data()+value.size() || !out.port || out.port>65535)
                throw std::invalid_argument("http port must be 1..65535");
        } else if(key=="token")out.token=value;
        else if(key=="origins") {
            std::size_t begin=0;
            while(begin<value.size()) {
                const auto end=value.find(',',begin);auto item=Trim(value.substr(begin,end-begin));
                if(!item.empty())out.origins.push_back(std::move(item));
                if(end==std::string::npos)break;begin=end+1;
            }
        } else throw std::invalid_argument("http-settings.cfg: unknown key "+key);
    }
    if(input.bad())throw std::runtime_error("error reading http-settings.cfg");
    return out;
}
}
