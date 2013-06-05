#include "synmon.hpp"
#include <sys/stat.h>
#include <ctime>
#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/locale/encoding.hpp>
#include <boost/locale/info.hpp>
#include <boost/locale/generator.hpp>
#include "json/json.hpp"
#include "agent/log.hpp"
#include "detail/ref_stream.hpp" // agent/detail/ref_stream.hpp
#include "error.hpp"

#include <iostream>

#define SYNMON_CHK_PERIOD_ 5

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

std::string to_utc_str(time_t t)
{
  std::string rt_str("xxxx-xx-xxTxx:xx:xx.xxx0");
  struct tm *tmb;
  tmb = std::gmtime(&t);
  strftime(&rt_str[0], rt_str.size(), "%Y-%m-%dT%H:%M:%S.000", tmb);
  rt_str.erase(rt_str.size()-1);
  return std::move(rt_str);
}

http::entity::query_map_t synmon::describe_file(std::string const &local_name) const
{
  using std::make_pair;

  struct stat sb;
  http::entity::query_map_t qm;
  error_code ec;
  std::string remote_name = db_.get_remote_name(ec, local_name);
  
  if(ec) {
    std::cerr << ec.message() << "\n";
    return qm;
  }
  remote_name.erase(0, 1); // skip '/'
  
  stat(local_name.c_str(), &sb);

  std::cout << "partial_name: " << remote_name << "\n";

  qm.insert(make_pair("partial_name", remote_name));
  qm.insert(make_pair("@file", local_name));
  qm.insert(make_pair("file_atime", to_utc_str(sb.st_atime)));
  qm.insert(make_pair("file_ctime", to_utc_str(sb.st_ctime)));
  qm.insert(make_pair("file_mtime", to_utc_str(sb.st_mtime)));

  return std::move(qm);
}

synmon::synmon(
  boost::asio::io_service &ios, 
  std::string const &pref,
  std::string const &account,
  std::string const &password)
: prefix(pref), db_(prefix), last_scan_(time(NULL)), changes_(0),
  on_syncing_(false),
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
  timer_.expires_from_now(boost::posix_time::seconds(SYNMON_CHK_PERIOD_));
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

    if( ratio < 100.0 && false == on_syncing_ )
      sync_check();

    timer_.expires_from_now(boost::posix_time::seconds(SYNMON_CHK_PERIOD_));
    timer_.async_wait(boost::bind(&synmon::handle_expiration, this, _1));
  }
}

void synmon::sync_check()
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

  http::entity::url url("http://10.0.0.185:8000/Node/Sync");
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
    shared_buffer body(new string);
    agent_.async_request(
      url, req, "GET", true,
      boost::bind(&synmon::handle_sync_check, this, _1, _2, _3, _4, body));
  } else {
    std::cerr << "error: " << ec.message() << "\n";
  }
}

void synmon::handle_sync_check(
  boost::system::error_code const &ec,
  http::request const &req,
  http::response const &rep,
  boost::asio::const_buffer buffer,
  shared_buffer body)
{
  using boost::asio::buffer_cast;
  using boost::asio::buffer_size;

  body->append(buffer_cast<char const*>(buffer), buffer_size(buffer));

  if( ec == boost::asio::error::eof ) {
    // std::cout << "body:\n" << *body << "\n";
    shared_json_var var(new json::var_t);
    auto beg(body->begin()), end(body->end());
    if(!json::phrase_parse(beg, end, *var))
      std::cerr << "error: Parsing of json response failed\n";
    sync(var);
  } else if (!ec) {
    // do nothing
  } else {
    std::cerr << "error: " << ec.message() << "\n";
  }
}

void synmon::sync(shared_json_var var)
{
  using std::string;
  json::pretty_print(std::cout, *var);
  json::object_t &map = mbof(*var)["file"].object();
  if(map.empty()) {
    on_syncing_ = false;
    return;
  }

  string const &name = map.begin()->first;
  json::object_t &info = mbof(map.begin()->second).object();
  auto version = mbof(info["v"]).intmax();
  file_status status = (file_status)mbof(info["s"]).cast<int>();
  
  on_syncing_ = true;

  // std::cout << name << ", v=" << version << ", s=" << status <<"\n";
  boost::system::error_code ec;
  if( status == file_status::reading ) {
    if( db_.set_status(ec, name, status) ) {
      std::cout << "update success, start to upload\n";
      std::cout << "local_file: " << db_.get_local_name(ec, name) << "\n";
      http::entity::url url("http://10.0.0.185:8000/1Path/CreateFile");
      http::request req;
      shared_buffer body(new string);
      auto cookie = http::get_header(req.headers, "Cookie");

      cookie->value = cookie_;
      url.query.query_map = describe_file(db_.get_local_name(ec, name));

      //for(auto i = url.query.query_map.begin(); i != url.query.query_map.end(); ++i)
      //  std::cout << i->first << " : " << boost::get<string>(i->second) << "\n";

      agent_.async_request(
        url, req, "POST", true,
        boost::bind(&synmon::handle_sync, this, _1, _2, _3, _4, body, var));
    } else if(ec != synmon_error::database_failure ) {
      std::cout << "update failed, proceed to next element\n";
      map.erase(map.begin());
      sync(var);
    } else {
      std::cerr << "error: " << ec.message() << "\n";
    }
  } else if( status == file_status::deleted ) {
    db_.set_status(ec, name, status);
    map.erase(map.begin());
    sync(var);
  } else {
    map.erase(map.begin());
    sync(var);
  }
}

void synmon::handle_sync(
  boost::system::error_code const &ec,
  http::request const &req,
  http::response const &rep,
  boost::asio::const_buffer buffer,
  shared_buffer body,
  shared_json_var var)
{
  using boost::asio::buffer_cast;
  using boost::asio::buffer_size;

  json::object_t &map = mbof(*var)["file"].object();
  body->append(buffer_cast<char const*>(buffer), buffer_size(buffer));
  if(ec == boost::asio::error::eof) {
    if( rep.status_code == 200 ) {
      error_code db_ec;
      std::string const &name = map.begin()->first;
      json::object_t &info = mbof(map.begin()->second).object();
      auto version = mbof(info["v"]).intmax();
      file_status status = (file_status)mbof(info["s"]).cast<int>();
      db_.set_status(db_ec, name, file_status::ok);
    } else {
      std::cerr << "http error: " << rep.status_code << "\n";
    }
    std::cout << "feedback: " << *body << "\n";
    map.erase(map.begin());
    sync(var);
  } else if(!ec) {
  } else {
    std::cerr << "error: " << ec.message() << "\n";
    map.erase(map.begin());
    sync(var);
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
