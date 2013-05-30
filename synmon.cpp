#include "synmon.hpp"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/locale/encoding.hpp>
#include <boost/locale/info.hpp>
#include <boost/locale/generator.hpp>
#include <iostream>

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

synmon::synmon(boost::asio::io_service &ios, std::string const &pref)
: prefix(pref), db_(prefix), last_scan_(time(NULL)), changes_(0),
  monitor_(ios), timer_(ios)
{
  using std::string;
  using namespace boost::filesystem;

  path p(prefix);
  p /= "monitor.conf";
  ifstream in(p, std::ios::binary | std::ios::in);
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
  resize_file(p, 0);
  timer_.expires_from_now(boost::posix_time::seconds(10));
  timer_.async_wait(boost::bind(&synmon::handle_expiration, this, _1));
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
      scan(entry.string());
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
        ec = error_code();
      }
      // std::cout << "add to db: " << remote_fullname << "\n";
      db_.add(ec, finfo);
      if(ec) {
        std::cerr << "error " << ec.message() << "\n";
        ec = error_code();
      }
    } // eof is_regular file
    else if(fs::is_directory(iter->path())) {
      auto remote_fullname = to_remote_name(
        iter->path().string().substr(parent.string().size())
        );
      //std::cout << "directory: " << remote_fullname << "\n";
    } // eof is_directory
    else {
      std::cerr << "Unsupport file type\n";
    }
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

    if( ratio < 100.0 ) {
      for(auto i = on_monitored_dir_.begin(); i != on_monitored_dir_.end(); ++i)
        scan(*i);
      error_code internal_ec;
      db_.check_changes(internal_ec);
    }

    timer_.expires_from_now(boost::posix_time::seconds(10));
    timer_.async_wait(boost::bind(&synmon::handle_expiration, this, _1));
  }
}

