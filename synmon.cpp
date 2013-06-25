#include "synmon.hpp"
#include <sys/stat.h>
#include <ctime>
#include <cstdio>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/locale/encoding.hpp>
#include <boost/locale/info.hpp>
#include <boost/locale/generator.hpp>
#include <openssl/sha.h>
#include "json/json.hpp"
#include "agent/log.hpp"
#include "detail/ref_stream.hpp" // agent/detail/ref_stream.hpp
#include "dl_ctx.hpp"
#include "error.hpp"

#include <iostream>

#define SYNMON_CHK_PERIOD_ 5

// FIXME Dir of locale encoding

//#ifdef SYNMON_ENABLE_HANDLER_TRACKING
#   define SYNMON_TRACKING(Desc) \
    logger::instance().async_log(Desc, false, (void*)this);
/*
#else
#   define SYNMON_TRACKING(Desc)
#endif
*/

#define STR2(X) #X
#define STR(X) STR2(X)
#define LN STR(__LINE__)

#define JSON_REF_ENT(Var, Obj, Ent, Type) \
  Obj[Ent] = json::##Type##_t(); \
  json::##Type##_t &Var = mbof(Obj[Ent]).Type();

#define SYNMON_LOG_ERROR(ERROR_CODE) \
{ \
  logger::instance().async_log(\
  "system_error("LN")", false, ERROR_CODE.message()); \
}

namespace fs = boost::filesystem;

std::string to_remote_name(std::string const &input)
{
  std::string rt;
#ifdef _WIN32
  rt = boost::locale::conv::to_utf<char>(input, "BIG5");
  for(auto i=rt.begin(); i!=rt.end(); ++i)
    if( *i == '\\')
      *i = '/';
#else
  rt = input;
#endif
  return std::move(rt);
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

  qm.insert(make_pair("partial_name", remote_name));
  qm.insert(make_pair("@file", local_name));
  qm.insert(make_pair("file_atime", to_utc_str(sb.st_atime)));
  qm.insert(make_pair("file_ctime", to_utc_str(sb.st_ctime)));
  qm.insert(make_pair("file_mtime", to_utc_str(sb.st_mtime)));

  return std::move(qm);
}

std::string synmon::to_local_name(std::string const &remote_name) const
{
  fs::path path(remote_name);
  while(path.parent_path() != path.root_path())
    path = path.parent_path();
  std::string suffix = remote_name.substr(path.string().size()+1);
#ifdef _WIN32
  for( auto i=suffix.begin(); i != suffix.end(); ++i) {
    if( *i == '/' )
      *i = '\\';
  }
  suffix = boost::locale::conv::from_utf<char>(suffix, "BIG5");
#endif
  for( auto i = on_monitored_dir_.begin(); i != on_monitored_dir_.end(); ++i) {
    fs::path t(*i);
    if(t.filename() == path.filename()) {
      return t.string() +  
#ifdef _WIN32
        "\\"
#else
        "/"
#endif
        + suffix;
    }
  }
  return std::string();
}

synmon::synmon(
  boost::asio::io_service &ios, 
  std::string const &pref,
  std::string const &addr,
  std::string const &account,
  std::string const &password)
: prefix(pref), address(addr),
  db_(prefix), last_scan_(time(NULL)), changes_(0),
  on_syncing_(false),
  monitor_(ios), timer_(ios),
  agent_(ios),
  account_(account),
  password_(password)
{
  using std::string;
  using namespace boost::filesystem;
  fs::path p(prefix);
  fs::path monitor_conf, log_file;
  monitor_conf = p / "synmon.conf" ;
  log_file = p / "synmon.log" ;
  logger::instance().use_file(log_file.string().c_str());

  SYNMON_TRACKING("synmon::ctor(" + 
                  pref + ", " +
                  addr + ", " +
                  account + ", " +
                  "***");

  ifstream in(monitor_conf, std::ios::binary | std::ios::in);
  if(in.is_open()) {
    string line;
    while( std::getline(in, line) ) {
      // add to monitor
      try {
        monitor_.add_directory(line);
        monitor_.async_monitor(boost::bind(
            &synmon::handle_changes, this, _1, _2));
        on_monitored_dir_.insert(line);
      } catch(std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
      }
    }
    in.close();
    fs::resize_file(monitor_conf, 0);
  }
  timer_.expires_from_now(boost::posix_time::seconds(SYNMON_CHK_PERIOD_));
  timer_.async_wait(boost::bind(&synmon::handle_expiration, this, _1));
  login(account_, password_);
}

void synmon::add_directory(std::string const &dir)
{ 
  SYNMON_TRACKING("synmon::add_directory(" + dir + ")" );
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
  SYNMON_TRACKING("synmon::scan");

  error_code ec;
  fs::path entry(dir);

  while(entry.filename() == ".") 
    entry.remove_filename();
  fs::path parent = entry;
  parent.remove_filename();
  fs::recursive_directory_iterator iter(entry), end;
  while(iter != end) {
    if( fs::is_regular(iter->path()) ) { //|| fs::is_directory(iter->path())) {
      file_info finfo;
      auto local_fullname = iter->path().string();
      auto remote_fullname = to_remote_name(
        iter->path().string().substr(parent.string().size())
        );
      if( remote_fullname[0] != '/' )
        remote_fullname.insert(0, "/");
      finfo.local_fullname = &local_fullname;
      finfo.remote_fullname = &remote_fullname;
      finfo.mtime = fs::last_write_time(iter->path(), ec);
      if(ec) {
        SYNMON_LOG_ERROR(ec);
        ec.clear();
      }
      db_.add(ec, finfo);
      if(ec) {
        SYNMON_LOG_ERROR(ec);
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
  p /= "synmon.conf";
  ofstream out(p, std::ios::binary | std::ios::out | std::ios::app);
  out << fullpath.string() << "\n";
  out.close();
}

void synmon::schedule_local_check()
{
  timer_.expires_from_now(boost::posix_time::seconds(SYNMON_CHK_PERIOD_));
  timer_.async_wait(boost::bind(&synmon::handle_expiration, this, _1));
}

//void synmon::rm_monitor(std::string const &directory)
//{}

void synmon::handle_changes(
  boost::system::error_code const &ec, 
  boost::asio::dir_monitor_event const &ev)
{
  SYNMON_TRACKING("synmon::handle_changes");

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
    SYNMON_LOG_ERROR(ec);
  }
}

void synmon::handle_expiration(
  boost::system::error_code const &ec)
{
  SYNMON_TRACKING("synmon::handle_expiration");

  if(!ec) {
    time_t period = time(NULL);
    period -= last_scan_;
    auto ratio = changes_ / period;
    //std::cout << ratio << " chg/sec\n";

    if( ratio < 12 && false == on_syncing_ ) {
      sync_check();
      changes_ = 0;
      last_scan_ = time(NULL);
    } else {
      schedule_local_check();
    }
  } else if( ec == boost::asio::error::operation_aborted ) {
  } else {
    SYNMON_LOG_ERROR(ec);
  }
}

void synmon::sync_check()
{
  SYNMON_TRACKING("synmon::sync_check");

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

  http::entity::url url("http://" + address + "/Node/Sync");
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
    // std::cout << "cli send: \n";
    // json::pretty_print(std::cout, obj);
    out.flush();
    shared_buffer body(new string);
    agent_.async_request(
      url, req, "POST", true,
      boost::bind(&synmon::handle_sync_check, this, _1, _2, _3, _4, body));
  } else {
    schedule_local_check();
    SYNMON_LOG_ERROR(ec);
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

  // FIXME  Restart handle_expiration if fails
  SYNMON_TRACKING("synmon::handle_sync_check");

  body->append(buffer_cast<char const*>(buffer), buffer_size(buffer));

  if( ec == boost::asio::error::eof ) {
    if( rep.status_code == 200 ) {
      shared_json_var var(new json::var_t);
      auto beg(body->begin()), end(body->end());
      if(!json::phrase_parse(beg, end, *var)) {
        std::cerr << "error: Parsing of json response failed\n";
        //std::cerr << *body << "\n";
      }
      // std::cout << "serv ack: \n";
      // json::pretty_print(std::cout, *var);
      sync(var);
    } else if( rep.status_code == 403) {
      agent_.io_service().post(
        boost::bind(&synmon::login, this, account_, password_));
    } else {
      std::cerr << "http error: " << rep.status_code << "\n";
    }
  } else if (!ec) {
  } else {
    SYNMON_LOG_ERROR(ec);
  }
  if(!on_syncing_)
    schedule_local_check();
}

void synmon::sync(shared_json_var var)
{
  using std::string;

  SYNMON_TRACKING("synmon::sync");
  assert(var.get() != 0 && "shared_json_var is freed abnormally");

  json::object_t &map = mbof(*var)["file"].object();
  if(map.empty()) {
    // std::cout << "sync done\n";
    on_syncing_ = false;
    schedule_local_check();
    return;
  }

  string const &name = map.begin()->first;
  string local_name = to_local_name(name);
  json::object_t &info = mbof(map.begin()->second).object();
  auto version = mbof(info["v"]).intmax();
  file_status status = (file_status)mbof(info["s"]).cast<int>();
  
  on_syncing_ = true;

  //std::cout << name << "(" << local_name << ")"
  //  ", v=" << version << ", s=" << status <<"\n";
  file_info fi;
  fi.local_fullname = &local_name;
  fi.remote_fullname = &name;
  boost::system::error_code ec;
  switch(status) {
  case ok:
  case modified:
    map.erase(map.begin());
    sync(var);
  break;
  case reading:
    if(db_.set_status(ec, fi, status)) {
      http::entity::url url("http://" + address + "/1Path/CreateFile");
      http::request req;
      shared_buffer body(new string);
      auto cookie = http::get_header(req.headers, "Cookie");

      cookie->value = cookie_;
      url.query.query_map = describe_file(db_.get_local_name(ec, name));
      url.query.query_map.insert(std::make_pair("version", version));
      agent_.async_request(
        url, req, "POST", true,
        boost::bind(&synmon::handle_reading, this, _1, _2, _3, _4, body, var));
      // SYNMON_TRACKING("+ uploading " + name);
    } else {
      SYNMON_LOG_ERROR(ec);
      map.erase(map.begin());
      sync(var);
    }
  break; // eof reading case
  case writing:
    assert(info.count("i") && "no id information");
    fi.status = file_status::ok;
    db_.add(ec, fi);
    fs::create_directories(fs::path(local_name).parent_path());
    if(ec) {
      std::cerr << "error: " << ec.message() << "\n";
      map.erase(map.begin());
      sync(var);
    } else {
      if(db_.set_status(ec, fi, status)) {
        std::string tmp =
          "http://" + address +"/" + 
          mbof(info["i"]).string() +
          "/Download"
          ;
        http::entity::url url(tmp);
        http::request req;
        auto cookie = http::get_header(req.headers, "Cookie");

        cookie->value = cookie_;
        url.query.query_map.insert(std::make_pair("version", version));
        agent_.async_request(
          url, req, "GET", true,
          boost::bind(&synmon::handle_writing, this, _1, _2, _3, _4,
                      shared_dl_ctx(new dl_ctx), var));
        
        //SYNMON_TRACKING("synmon::hande_writing (" + tmp + ")");
        std::cout << "downloading " << name << " ... " ; 
      } else {
        SYNMON_LOG_ERROR(ec);
        map.erase(map.begin());
        sync(var);
      }
    }
  break; // eof writing case
  case conflicted:
    if(!db_.set_status(ec, fi, status))
      SYNMON_LOG_ERROR(ec);
    map.erase(map.begin());
    sync(var);
  break; // eof conflicted case
  case deleted:
    db_.set_status(ec, fi, status);
    map.erase(map.begin());
    sync(var);
  break; // eof deleted case
  default:
    assert(false && "unrecognized status");
  }
  
}

void synmon::handle_reading(
  boost::system::error_code const &ec,
  http::request const &req,
  http::response const &rep,
  boost::asio::const_buffer buffer,
  shared_buffer body,
  shared_json_var var)
{
  using boost::asio::buffer_cast;
  using boost::asio::buffer_size;

  SYNMON_TRACKING("synmon::handle_reading");
  try {
    error_code db_ec;
    json::object_t &map = mbof(*var)["file"].object();

    body->append(buffer_cast<char const*>(buffer), buffer_size(buffer));

    if(ec == boost::asio::error::eof) {
      std::string const &name = map.begin()->first;
      json::object_t &info = mbof(map.begin()->second).object();
      auto version = mbof(info["v"]).intmax();
      file_status status = (file_status)mbof(info["s"]).cast<int>();
      assert(status == reading && "only reading status goes here");
      file_info fi;
      fi.remote_fullname = &name;
      fi.version = (int)version;
      //SYNMON_TRACKING("+ " + name + "\n" + *body);
      if( rep.status_code == 200 ) {
        // increament version
        fi.version++;
        if( false == db_.set_status(db_ec, fi, file_status::ok) ) {
          SYNMON_LOG_ERROR(db_ec);
        } else {
          std::cout << "upload finished: " << name << "\n";
        }
      } else if( rep.status_code == 409 ) {
        if( false == db_.set_status(db_ec, fi, file_status::conflicted) ) {
          SYNMON_LOG_ERROR(db_ec);
        } else {
          std::cerr << "conflicted\n";
        }
      } else {
        std::cerr << "http error: " << rep.status_code << "\n";
      }
      //std::cout << "feedback: " << *body << "\n";
      map.erase(map.begin());
      sync(var);
    } else if(!ec) {
    } else {
      SYNMON_LOG_ERROR(ec);
      map.erase(map.begin());
      sync(var);
    }
    // TODO any failed operations should be recover after all sync op are issued
  } catch(boost::bad_get &) {
    std::cerr << "access to shared_json_var failed\n";
    sync(var);
  }
}

void synmon::handle_writing(
  boost::system::error_code const &ec,
  http::request const &req,
  http::response const &rep,
  boost::asio::const_buffer buffer,
  shared_dl_ctx dctx,
  shared_json_var var)
{
  using boost::asio::buffer_cast;
  using boost::asio::buffer_size;

  SYNMON_TRACKING("synmon::handle_writing");

  json::object_t &map = mbof(*var)["file"].object();

  std::string const &name = map.begin()->first;
  std::string local_name = to_local_name(name);
  json::object_t &info = mbof(map.begin()->second).object();
  auto version = mbof(info["v"]).intmax();
  file_status status = (file_status)mbof(info["s"]).cast<int>();
  file_info fi;
  error_code db_ec;

  fi.remote_fullname = &name;
  fi.local_fullname =&local_name;
  fi.version = (int)version;

  if(!ec || ec == boost::asio::error::eof ) {
    if( rep.status_code == 200 ) {
      if( false == *dctx ) {
        auto content_length = http::find_header(rep.headers, "Content-Length");
        size_t size = content_length->value_as<size_t>();
        assert( content_length != rep.headers.end() && "No content length" );
        dctx->open(local_name, size);
      }
      dctx->append(buffer_cast<char const*>(buffer), buffer_size(buffer));
      if( ec == boost::asio::error::eof) {
        if(db_.set_status(db_ec, fi, file_status::ok)) {
          std::cout << "finished " << "\n";
          dctx->commit();
          db_.resync_mtime(db_ec, fi);
          if(db_ec)
            SYNMON_LOG_ERROR(ec);
        } else
          SYNMON_LOG_ERROR(ec);
      }
    } else {
      std::cerr << "http error: " << rep.status_code << "\n";
    }
    if( ec == boost::asio::error::eof) {
      map.erase(map.begin());
      sync(var);
    }
  } else {
    SYNMON_LOG_ERROR(ec);
    db_.set_status(db_ec, fi, file_status::deleted);
    map.erase(map.begin());
    sync(var);
  }
}

void synmon::login(std::string const &account, std::string const &password)
{
  // FIXME This may failure after closing socket
  // TODO discover device
  using std::make_pair;

  SYNMON_TRACKING("synmon::login");

  http::entity::url url("http://" + address + "/Node/User/Auth");
  http::request req;

  std::string sha;
  unsigned char tmp[20];
  if(0 == SHA1((unsigned char const*)password.c_str(), password.size(), tmp)) {
    std::cerr << "error: Unable to generate SHA-1 passwd.\n";
    return;
  }
  sha.resize(40);
  for(size_t i=0; i<20; ++i) 
    std::sprintf(&sha[i*2], "%02x", tmp[i]); 

  url.query.query_map.insert(make_pair("name", account));
  url.query.query_map.insert(make_pair("password", sha));
  url.query.query_map.insert(make_pair("sha1", "1"));
  url.query.query_map.insert(make_pair("expire_time", "7200"));

  agent_.async_request(
    url, req, "GET", true,
    boost::bind(&synmon::handle_login, this, _1, _2, _3, _4));
  // std::cerr << "do login\n";
}

void synmon::handle_login(
  boost::system::error_code const &ec,
  http::request const &req,
  http::response const &rep,
  boost::asio::const_buffer buffer)
{
  SYNMON_TRACKING("synmon::handle_login");

  if(boost::asio::error::eof == ec) {
    if( rep.status_code == 200 ) {
      auto ck = http::find_header(rep.headers, "Set-Cookie");
      cookie_ = ck->value;
      cookie_ = cookie_.substr(0, cookie_.find(";"));
    } else {
      std::cerr << "Login failed\n";
    }
  } else if(ec) {
    SYNMON_LOG_ERROR(ec);
  }
}
