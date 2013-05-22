#include "synmon.hpp"
#include <boost/filesystem.hpp>
#include <iostream>

namespace fs = boost::filesystem;

synmon::synmon(boost::asio::io_service &ios, std::string const &prefix)
  : monitor_(ios), on_syncing_(false), db_(prefix)
{}

void synmon::add_monitor(std::string const &directory)
{
  error_code ec;
  fs::path entry = fs::system_complete(fs::path(directory), ec);
  if(ec || !fs::is_directory(entry))
    return;
  fs::path parent = entry;
  while(parent.filename() == ".") 
    parent.remove_filename();
  parent.remove_filename();
  fs::recursive_directory_iterator iter(entry), end;

  while(iter != end) {
    if(!fs::is_directory(iter->path())) {
      file_info finfo;
      auto local_fullname = iter->path().string();
      auto remote_fullname = iter->path().string().substr(parent.string().size());
      finfo.local_fullname = &local_fullname;
      finfo.remote_fullname = &remote_fullname;
      finfo.mtime = fs::last_write_time(iter->path(), ec);
      if(ec) {
        std::cerr << "[error] " << ec.message() << "\n";
        ec = error_code();
      }
      db_.add(ec, finfo);
    } else {
      std::cout << iter->path().string() << "\n";
    }
    ++iter;
  }
}

void synmon::rm_monitor(std::string const &directory)
{
}

void synmon::handle_changes(
  boost::system::error_code const &ec, 
  boost::asio::dir_monitor_event const &ev)
{
  if(!ec) {
    std::cout << "\n" << ev.dirname << "/" << ev.filename << " is changed\n";
    monitor_.async_monitor(boost::bind(
        &synmon::handle_changes, this, _1, _2));
  }
}

void synmon::handle_expiration(
  boost::system::error_code const &ec)
{
  if(!ec) {
    
  }
}

