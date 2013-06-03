#include "synmon.hpp"
#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/locale/encoding.hpp>
#include <boost/locale/info.hpp>
#include <boost/locale/generator.hpp>
#include "json/json.hpp"
#include "agent/log.hpp"
#include "detail/ref_stream.hpp" // agent/detail/ref_stream.hpp

#include <iostream>

#define JSON_REF_ENT(Var, Obj, Ent, Type) \
  Obj[Ent] = json::##Type##_t(); \
  json::##Type##_t &Var = mbof(Obj[Ent]).Type();

namespace fs = boost::filesystem;

std::string to_remote_name(std::string const &input)
{
  auto rt = boost::locale::conv::to_utf<char>(input, "BIG5");
#ifdef _WIN32
  for(auto i=rt.begin(); i!=rt.end(); ++i)
    if( *i == '\\')
      *i = '/';
#endif
  return rt;
}

synmon::synmon(
  boost::asio::io_service &ios, 
  std::string const &pref,
  std::string const &account,
  std::string const &password)
: prefix(pref), db_(prefix), last_scan_(time(NULL)), changes_(0),
  monitor_(ios), timer_(ios),
  agent_(ios)
{
  using std::string;
  using namespace boost::filesystem;
  path p(prefix);
  path monitor_conf, log_file;
  monitor_conf = p / "monitor.conf";
  log_file = p / "synmon.log";
  logger::instance().use_file(log_file.string().c_str());
  ifstream in(monitor_conf, std::ios::binary | std::ios::in);
  if(in.is_open()) {
    string line;
    while( std::getline(in, line) ) {
      // add to monitor
      try {
        //std::cout << "dir: " << line << "\n";
        monitor_.add_directory(line);
        monitor_.async_monitor(boost::bind(
            &synmon::handle_changes, this, _1, _2));
        on_monitored_dir_.insert(line);
      } catch(std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
      }
    }
    in.close();
  }  
  resize_file(monitor_conf, 0);
  timer_.expires_from_now(boost::posix_time::seconds(10));
  timer_.async_wait(boost::bind(&synmon::handle_expiration, this, _1));
  login(account, password);
  std::cout << "ctor\n";
}

void synmon::add_directory(std::string const &dir)
{ 
  add_dir_to_conf(dir);
  // add to monitor
  try {
    error_code ec;
    fs::path entry = fs::system_complete(fs::path(dir), ec);
    if(ec || !fs::is_directory(entry))
      return;
    auto rt = on_monitored_dir_.insert(entry.string());
    if( rt.second ) {
      monitor_.add_directory(entry.string());
      monitor_.async_monitor(boost::bind(
          &synmon::handle_changes, this, _1, _2));
    }
  } catch(std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
  }
}

void synmon::scan(std::string const &dir)
{
  error_code ec;
  fs::path entry(dir);

  while(entry.filename() == ".") 
    entry.remove_filename();
  fs::path parent = entry;
  parent.remove_filename();
  fs::recursive_directory_iterator iter(entry), end;
  while(iter != end) {
    if( fs::is_regular(iter->path()) ) {
      file_info finfo;
      auto local_fullname = iter->path().string();
      auto remote_fullname = to_remote_name(
        iter->path().string().substr(parent.string().size())
        );
      finfo.local_fullname = &local_fullname;
      finfo.remote_fullname = &remote_fullname;
      finfo.mtime = fs::last_write_time(iter->path(), ec);
      if(ec) {
        std::cerr << "error " << ec.message() << "\n";
        ec.clear();
      }
      // std::cout << "add to db: " << remote_fullname << "\n";
      db_.add(ec, finfo);
      if(ec) {
        std::cerr << "error " << ec.message() << "\n";
        ec.clear();
      }
    } // eof is_regular file
    /*
    else if(fs::is_directory(iter->path())) {
      auto remote_fullname = to_remote_name(
        iter->path().string().substr(parent.string().size())
        );
      output.push_back(remote_fullname);
      //std::cout << "directory: " << remote_fullname << "\n";
    } // eof is_directory
    
    else {
      std::cerr << "Unsupport file type\n";
    }
    */
    ++iter;
  }
}

void synmon::add_dir_to_conf(std::string const &dir)
{
  using namespace boost::filesystem;
  
  error_code ec;

  path p(prefix), fullpath(dir);
  fullpath = fs::system_complete(fullpath,ec);
  if(ec) return;
  p /= "monitor.conf";
  ofstream out(p, std::ios::binary | std::ios::out | std::ios::app);
  out << fullpath.string() << "\n";
  out.close();
}


//void synmon::rm_monitor(std::string const &directory)
//{}

void synmon::handle_changes(
  boost::system::error_code const &ec, 
  boost::asio::dir_monitor_event const &ev)
{
  using std::locale;
  using std::cout;
  using std::string;
  using namespace boost::asio;
  using namespace boost::locale;

  if(!ec) {
    changes_++;
    monitor_.async_monitor(boost::bind(
        &synmon::handle_changes, this, _1, _2));
  } else {
    std::cerr << "error: " << ec.message() << "\n";
  }
}

void synmon::handle_expiration(
  boost::system::error_code const &ec)
{
  if(!ec) {
    time_t period = time(NULL);
    period -= last_scan_;
    double ratio = changes_ / (double)period;
    std::cout << ratio << " chg/sec\n";
    last_scan_ = time(NULL);
    changes_ = 0;

    if( ratio < 100.0 )
      sync();

    timer_.expires_from_now(boost::posix_time::seconds(10));
    timer_.async_wait(boost::bind(&synmon::handle_expiration, this, _1));
  }
}

void synmon::sync()
{
  using std::string;
  if(cookie_.empty()) return;

  json::object_t obj;
  JSON_REF_ENT(dirs, obj, "dir", array);

  for(auto i = on_monitored_dir_.begin(); i != on_monitored_dir_.end(); ++i) {
    fs::path p(*i);
    string remote_dir = to_remote_name(p.filename().string());
    remote_dir = "/" + remote_dir + "/";
    dirs.push_back(remote_dir);
    scan(*i);
  }

  http::entity::url url("http://10.0.0.185:8000/1Path/Sync");
  http::request req;

  auto cookie = http::get_header(req.headers, "Cookie");
  cookie->value = cookie_;
  
  error_code ec;
  db_.check_changes(ec, obj);

  if(!ec) {
    auto iter = url.query.query_map.insert(
      std::make_pair("changes", string()));
    string &val = boost::get<string>(iter->second);
    ref_str_stream out(val);
    json::pretty_print(out, obj, json::print::compact);
    out.flush();
    std::cout << "flushed:\n" << val << "\n";
    agent_.async_request(
      url, req, "GET", true,
      boost::bind(&synmon::handle_sync, this, _1, _2, _3, _4));
  } else {
    std::cerr << "error: " << ec.message() << "\n";
  }
}

void synmon::handle_sync(
  boost::system::error_code const &ec,
  http::request const &req,
  http::response const &rep,
  boost::asio::const_buffer buffer)
{
  if( !ec ) {
    
  } else {
    
  }
}

void synmon::login(std::string const &account, std::string const &password)
{
  // TODO discover device
  http::entity::url url("http://10.0.0.185:8000/Node/User/Auth");
  http::request req;

  url.query.query_map.insert(make_pair("name", account));
  url.query.query_map.insert(make_pair("password", password));

  agent_.async_request(
    url, req, "GET", true,
    boost::bind(&synmon::handle_login, this, _1, _2, _3, _4));
}

void synmon::handle_login(
  boost::system::error_code const &ec,
  http::request const &req,
  http::response const &rep,
  boost::asio::const_buffer buffer)
{
  if(boost::asio::error::eof == ec) {
    if( rep.status_code == 200 ) {
      auto ck = http::find_header(rep.headers, "Set-Cookie");
      cookie_ = ck->value;
      cookie_ = cookie_.substr(0, cookie_.find(";"));
      //std::cout << "Log-on with cookie : " << cookie_ << "\n";
    } else {
      std::cerr << "Login failed\n";
    }
  } else if(ec) {
    std::cerr << "Error: " << ec.message() << "\n";
  }
}
