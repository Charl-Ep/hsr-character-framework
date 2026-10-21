#include <hksr/framework/http/HttpControl.hpp>
#include "HttpProtocol.hpp"
#include <httplib.h>
#include <atomic>
#include <thread>
#include <algorithm>
#include <set>

namespace hksr::framework {
namespace {
using http::Json;
void Send(httplib::Response& r,const Json& value,int status=200) {
    r.status=status;r.set_content(value.dump(-1,' ',false,Json::error_handler_t::replace),"application/json; charset=utf-8");
}
void Error(httplib::Response& r,int status,const char* code,const std::string& message) {
    Send(r,{{"error",{{"code",code},{"message",message}}}},status);
}
// Secrets are never put into URLs, status, errors or the application snapshot.
bool TokenEqual(const std::string& a,const std::string& b) {
    std::size_t difference=a.size()^b.size();
    for(std::size_t i=0;i<b.size();++i)difference|=(i<a.size()?static_cast<unsigned char>(a[i]):0)^static_cast<unsigned char>(b[i]);
    return difference==0;
}
void Validate(const HttpSettings& s) {
    IN6_ADDR six{};IN_ADDR four{};
    if(InetPtonA(AF_INET,s.address.c_str(),&four)!=1 && InetPtonA(AF_INET6,s.address.c_str(),&six)!=1)
        throw std::invalid_argument("HTTP address must be an explicit IPv4 or IPv6 literal");
    if(!s.port || s.port>65535)throw std::invalid_argument("HTTP port must be 1..65535");
    if(s.token.size()<32 || s.token.size()>256 ||
        std::any_of(s.token.begin(),s.token.end(),[](unsigned char c){return c<=32 || c>=127;}))
        throw std::invalid_argument("HTTP requires an access token of 32..256 printable ASCII characters without spaces");
    for(const auto& o:s.origins) {
        const auto start=o.compare(0,7,"http://")==0?7u:o.compare(0,8,"https://")==0?8u:0u;
        if(!start || o.size()<=start || o.find_first_of("\r\n\t ,*?#@",start)!=std::string::npos || o.find('/',start)!=std::string::npos)
            throw std::invalid_argument("HTTP origins must be exact http(s) origins, no path or wildcard");
    }
}
struct Receipt {
    std::mutex mutex;
    std::optional<CommandReply> reply;
    std::optional<CommandReply> Read() {std::lock_guard<std::mutex> lock(mutex);return reply;}
    void Set(const CommandReply& value) {std::lock_guard<std::mutex> lock(mutex);reply=value;}
};
// A locally shutdown Winsock socket need not wake WSAPoll immediately.
// Check cancellation between bounded waits; retain the original overall
// read/write deadlines and let httplib handle buffering and HTTP parsing.
class SocketStream final:public httplib::Stream {
public:
    SocketStream(socket_t socket,const std::atomic<bool>& stopping,
        time_t read_sec,time_t read_usec,time_t write_sec,time_t write_usec)
        :inner_(socket,0,0,0,0),
         socket_(socket),stopping_(stopping),read_sec_(read_sec),read_usec_(read_usec),
         write_sec_(write_sec),write_usec_(write_usec){}
    bool is_readable() const override {return inner_.is_readable();}
    bool wait_readable() const override {return Wait(true,read_sec_,read_usec_);}
    bool wait_writable() const override {return Wait(false,write_sec_,write_usec_);}
    bool is_peer_alive() const override {return !stopping_ && inner_.is_peer_alive();}
    ssize_t read(char* data,std::size_t size) override {
        if(stopping_){error_=httplib::Error::ConnectionClosed;return -1;}
        if(!inner_.is_readable()) {
            if(!wait_readable()){error_=httplib::Error::Timeout;return -1;}
            inner_.set_readable_hint();
        }
        const auto result=inner_.read(data,size);error_=inner_.get_error();return result;
    }
    ssize_t write(const char* data,std::size_t size) override {
        if(!wait_writable())return -1;
        return inner_.write(data,size);
    }
    void get_remote_ip_and_port(std::string& ip,int& port) const override {inner_.get_remote_ip_and_port(ip,port);}
    void get_local_ip_and_port(std::string& ip,int& port) const override {inner_.get_local_ip_and_port(ip,port);}
    socket_t socket() const override {return socket_;}
    time_t duration() const override {return inner_.duration();}
    void set_read_timeout(time_t seconds,time_t micros) override {read_sec_=seconds;read_usec_=micros;}
    const char* buffered_data(std::size_t& size) const override {return inner_.buffered_data(size);}
    void consume_buffered(std::size_t size) override {inner_.consume_buffered(size);}
private:
    bool Wait(bool reading,time_t seconds,time_t micros) const {
        using namespace std::chrono;
        const auto deadline=seconds<0?steady_clock::time_point::max():
            steady_clock::now()+std::chrono::seconds(seconds)+microseconds(micros);
        while(!stopping_) {
            const auto remaining=duration_cast<microseconds>(deadline-steady_clock::now()).count();
            const auto slice=static_cast<time_t>((std::max)(0LL,(std::min)(50000LL,remaining)));
            const auto result=reading?httplib::detail::select_read(socket_,0,slice):
                httplib::detail::select_write(socket_,0,slice);
            if(stopping_)return false;
            if(result!=0)return result>0;
            if(steady_clock::now()>=deadline)return false;
        }
        return false;
    }
    httplib::detail::SocketStream inner_;
    socket_t socket_;
    const std::atomic<bool>& stopping_;
    time_t read_sec_,read_usec_,write_sec_,write_usec_;
};
// Own accepted sockets through their close, so Stop also interrupts partial
// headers/bodies and slow readers. Upstream Server::stop closes only the
// listener. This narrow adapter uses the pinned library's HTTP serving loop;
// it does not implement another HTTP parser or change the vendored source.
class Server final:public httplib::Server {
public:
    void Interrupt() {
        stopping_=true;stop();
        std::lock_guard<std::mutex> lock(sockets_mutex_);
        for(const auto socket:sockets_)::shutdown(socket,SD_BOTH);
    }
private:
    bool process_and_close_socket(socket_t socket) override {
        {
            std::lock_guard<std::mutex> lock(sockets_mutex_);
            if(stopping_){::closesocket(socket);return false;}
            sockets_.insert(socket);
        }
        struct Close {
            Server& owner;socket_t socket;
            ~Close() {
                std::lock_guard<std::mutex> lock(owner.sockets_mutex_);
                owner.sockets_.erase(socket);::shutdown(socket,SD_BOTH);::closesocket(socket);
            }
        } close{*this,socket};
        return serve_guarded([&] {
            std::string remote,local;int remote_port=0,local_port=0;
            httplib::detail::get_remote_ip_and_port(socket,remote,remote_port);
            httplib::detail::get_local_ip_and_port(socket,local,local_port);
            SocketStream stream(socket,stopping_,read_timeout_sec_,read_timeout_usec_,write_timeout_sec_,write_timeout_usec_);
            return httplib::detail::process_server_socket_core(svr_sock_,socket,keep_alive_max_count_,keep_alive_timeout_sec_,
                [&] {return stream.is_readable();},
                [&](bool,bool close_connection,bool& closed) {
                    if(stopping_)return false;
                    return process_request(stream,remote,remote_port,local,local_port,close_connection,closed,nullptr);
                });
        });
    }
    std::atomic<bool> stopping_{false};
    std::mutex sockets_mutex_;
    std::set<socket_t> sockets_;
};
}
struct HttpControl::State {
    ApplicationService& application;
    HttpSettings settings;
    Server server;
    std::thread listener;
    std::atomic<bool> stopping{false},finished{false};
    std::atomic<unsigned> streams{0};
    mutable std::mutex status_mutex;
    std::string failure;
    std::mutex receipt_mutex;
    std::map<std::uint64_t,std::shared_ptr<Receipt>> receipts;
    static constexpr unsigned StreamLimit=8; // connections, never character count
    static constexpr std::size_t PendingLimit=256,ReceiptHistory=256;

    State(ApplicationService& a,HttpSettings s):application(a),settings(std::move(s)) {
        // Reserve four HTTP workers even when all SSE slots are occupied.
        // Accepted sockets and bodies are bounded against untrusted clients.
        server.new_task_queue=[] {return new httplib::ThreadPool(StreamLimit+4,StreamLimit+4,64);};
        server.set_socket_options([](socket_t socket) {
            // Windows SO_REUSEADDR allows a second listener to steal traffic.
            if(!httplib::set_socket_opt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,1))
                throw std::runtime_error("cannot request exclusive HTTP address ownership");
        });
        server.set_payload_max_length(64*1024);
        server.set_read_timeout(2,0);server.set_write_timeout(2,0);
        server.set_keep_alive_timeout(1);server.set_keep_alive_max_count(8);
        server.set_default_headers({{"Cache-Control","no-store"},{"X-Content-Type-Options","nosniff"}});
        server.set_pre_routing_handler([this](const auto& request,auto& response) {
            return Authorize(request,response)?httplib::Server::HandlerResponse::Unhandled:httplib::Server::HandlerResponse::Handled;
        });
        server.set_exception_handler([](const auto&,auto& response,std::exception_ptr error) {
            try {if(error)std::rethrow_exception(error);}
            catch(const std::exception& e){Error(response,500,"internal_error",e.what());return;}
            catch(...){}
            Error(response,500,"internal_error","unexpected transport failure");
        });
        server.set_error_handler([](const auto&,auto& response) {
            if(response.body.empty())Error(response,response.status,"http_error","request not accepted by HTTP server");
        });
        server.Get("/v1/snapshot",[this](const auto&,auto& r){Send(r,http::Updates(application.ReadUpdates()));});
        server.Get("/v1/catalog",[this](const auto&,auto& r){Send(r,http::Catalog(application.Snapshot()));});
        server.Get(R"(/v1/characters/(\d+)/actions)",[this](const auto& q,auto& r) {
            try {Send(r,http::Actions(application.Snapshot(),http::Id(q.matches[1].str())));}
            catch(const std::out_of_range& e){Error(r,404,"not_found",e.what());}
            catch(const std::exception& e){Error(r,400,"invalid_request",e.what());}
        });
        server.Post("/v1/commands",[this](const auto& q,auto& r){Submit(q,r);});
        server.Get(R"(/v1/characters/(\d+)/tasks)",[this](const auto& q,auto& r) {
            try {Send(r,http::Tasks(application.Snapshot(),http::Id(q.matches[1].str())));}
            catch(const std::out_of_range& e){Error(r,404,"not_found",e.what());}
            catch(const std::exception& e){Error(r,400,"invalid_request",e.what());}
        });
        server.Get(R"(/v1/requests/(\d+))",[this](const auto& q,auto& r){ReadReceipt(q,r);});
        server.Get("/v1/events",[this](const auto& q,auto& r){Events(q,r);});
    }
    ~State() {Stop();}
    void Stop() {
        stopping=true;server.Interrupt();
        if(listener.joinable())listener.join();
    }
    bool Authorize(const httplib::Request& q,httplib::Response& r) {
        const auto origin=q.get_header_value("Origin");
        if(q.has_header("Origin")) {
            if(q.get_header_value_count("Origin")!=1 || std::find(settings.origins.begin(),settings.origins.end(),origin)==settings.origins.end()) {
                Error(r,403,"origin_denied","browser origin is not configured");return false;
            }
            r.set_header("Access-Control-Allow-Origin",origin);r.set_header("Vary","Origin");
            r.set_header("Access-Control-Expose-Headers","Location");
        }
        if(q.method=="OPTIONS") {
            if(origin.empty()){Error(r,403,"origin_denied","preflight requires an allowed origin");return false;}
            r.set_header("Access-Control-Allow-Methods","GET, POST, OPTIONS");
            r.set_header("Access-Control-Allow-Headers","Authorization, Content-Type, Last-Event-ID");
            r.status=204;return false;
        }
        if(q.get_header_value_count("Authorization")!=1 || !TokenEqual(q.get_header_value("Authorization"),"Bearer "+settings.token)) {
            r.set_header("WWW-Authenticate","Bearer");Error(r,401,"unauthorized","valid Bearer token required");return false;
        }
        if(stopping){Error(r,503,"stopping","HTTP service is stopping");return false;}
        return true;
    }
    void TrimReceipts() { // receipt_mutex held; never discard pending work
        std::size_t terminal=0;for(const auto& entry:receipts)terminal+=entry.second->Read().has_value();
        for(auto i=receipts.begin();i!=receipts.end() && terminal>ReceiptHistory;) {
            if(i->second->Read()){i=receipts.erase(i);--terminal;}else ++i;
        }
    }
    void Submit(const httplib::Request& q,httplib::Response& r) {
        auto type=q.get_header_value("Content-Type");const auto semicolon=type.find(';');type.resize(semicolon==std::string::npos?type.size():semicolon);
        while(!type.empty() && type.back()==' ')type.pop_back();
        if(type!="application/json"){Error(r,415,"content_type","Content-Type must be application/json");return;}
        std::optional<CharacterCommand> command;
        try {
            command=http::Command(Json::parse(q.body,[](int depth,Json::parse_event_t,Json&) {
                if(depth>16)throw std::invalid_argument("command JSON nesting exceeds 16 levels");
                return true;
            }));
        }
        catch(const std::exception& e){Error(r,400,"invalid_request",e.what());return;}
        auto receipt=std::make_shared<Receipt>();std::uint64_t id=0;
        {
            std::lock_guard<std::mutex> lock(receipt_mutex);TrimReceipts();
            std::size_t pending=0;for(const auto& entry:receipts)pending+=!entry.second->Read().has_value();
            if(pending>=PendingLimit){Error(r,429,"queue_full","HTTP commands await official-frame dispatch; retry after observing progress");return;}
            try {id=application.Post(std::move(*command),[receipt](const auto& reply){receipt->Set(reply);});}
            catch(const std::exception& e){Error(r,503,"application_unavailable",e.what());return;}
            // Callback can already have run. It owns only this receipt, never
            // a network response/connection or the transport's lifetime.
            receipts.emplace(id,std::move(receipt));
        }
        const auto url="/v1/requests/"+std::to_string(id);r.set_header("Location",url);
        Send(r,{{"serial",std::to_string(id)},{"state","queued"},{"request_url",url}},202);
    }
    void ReadReceipt(const httplib::Request& q,httplib::Response& r) {
        std::uint64_t id=0;
        try {id=http::Id(q.matches[1].str());}catch(const std::exception& e){Error(r,400,"invalid_request",e.what());return;}
        std::shared_ptr<Receipt> receipt;
        {std::lock_guard<std::mutex> lock(receipt_mutex);TrimReceipts();const auto found=receipts.find(id);if(found!=receipts.end())receipt=found->second;}
        if(!receipt){Error(r,404,"request_unavailable","unknown or expired HTTP dispatch receipt; this does not prove nonexecution");return;}
        const auto reply=receipt->Read();Json value={{"serial",std::to_string(id)},{"state",reply?"dispatched":"queued"}};
        if(reply)value["reply"]=http::Reply(*reply);Send(r,value);
    }
    void Events(const httplib::Request& q,httplib::Response& r) {
        std::optional<ApplicationCursor> cursor;
        try {
            if(q.get_header_value_count("Last-Event-ID")>1)throw std::invalid_argument("duplicate Last-Event-ID");
            if(q.has_header("Last-Event-ID") && !q.get_header_value("Last-Event-ID").empty())cursor=http::ParseCursor(q.get_header_value("Last-Event-ID"));
        }catch(const std::exception& e){Error(r,400,"invalid_cursor",e.what());return;}
        auto count=streams.load();
        do {if(count>=StreamLimit){Error(r,429,"stream_limit","too many SSE connections; ordinary HTTP remains available");return;}}
        while(!streams.compare_exchange_weak(count,count+1));
        // Shared lease survives response copies; releases on setup exceptions,
        // disconnects and normal completion, exactly once.
        auto lease=std::shared_ptr<void>(this,[this](void*){--streams;});
        struct Stream {std::optional<ApplicationCursor> cursor;bool first=true;std::chrono::steady_clock::time_point heartbeat=std::chrono::steady_clock::now();};
        auto stream=std::make_shared<Stream>();stream->cursor=std::move(cursor);
        r.set_header("X-Accel-Buffering","no");
        r.set_chunked_content_provider("text/event-stream; charset=utf-8",[this,stream,lease](std::size_t,httplib::DataSink& sink) {
            if(stopping || !sink.is_writable()){sink.done();return false;}
            if(!stream->first && !application.WaitForChange(*stream->cursor,std::chrono::milliseconds(250))) {
                if(std::chrono::steady_clock::now()-stream->heartbeat>=std::chrono::seconds(15)) {
                    if(!sink.write(": keepalive\n\n",13))return false;stream->heartbeat=std::chrono::steady_clock::now();
                }
                return !stopping;
            }
            auto update=application.ReadUpdates(stream->cursor);
            const bool first=stream->first;stream->first=false;stream->cursor=update.changes.cursor;
            if(update.changes.reset || !update.changes.events.empty()) {
                const auto bytes=http::Event(update);if(!sink.write(bytes.data(),bytes.size()))return false;
                stream->heartbeat=std::chrono::steady_clock::now();
            } else if(first || std::chrono::steady_clock::now()-stream->heartbeat>=std::chrono::seconds(15)) {
                if(!sink.write(": keepalive\n\n",13))return false;stream->heartbeat=std::chrono::steady_clock::now();
            }
            if(update.snapshot.closed || stopping)sink.done();
            return true;
        });
    }
};
HttpControl::HttpControl(ApplicationService& application):application_(application){}
HttpControl::~HttpControl() {Stop();}
void HttpControl::Start(const HttpSettings& settings) {
    if(state_)throw std::logic_error("HTTP service must be stopped before starting");
    if(!settings.enabled)return;
    Validate(settings);
    auto next=std::make_unique<State>(application_,settings);
    if(!next->server.bind_to_port(settings.address,static_cast<int>(settings.port)))throw std::runtime_error("cannot bind HTTP listener to requested address and port");
    auto* state=next.get();
    state->listener=std::thread([state] {
        try {
            if(!state->server.listen_after_bind() && !state->stopping) {
                std::lock_guard<std::mutex> lock(state->status_mutex);state->failure="HTTP accept loop failed";
            }
        } catch(const std::exception& e) {std::lock_guard<std::mutex> lock(state->status_mutex);state->failure=e.what();}
        state->finished=true;
    });
    state_=std::move(next);
}
void HttpControl::Stop() {if(state_){state_->Stop();state_.reset();}}
HttpStatus HttpControl::Status() const {
    if(!state_)return {};
    std::lock_guard<std::mutex> lock(state_->status_mutex);
    return {!state_->finished && !state_->stopping,state_->settings.port,state_->failure};
}
}
