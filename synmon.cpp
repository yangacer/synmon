#include "synmon.hpp"
#include <boost/filesystem.hpp>
#include <iostream>

namespace fs = boost::filesystem;

synmon::synmon(boost::asio::io_service &ios, std::string const &pref)
  : prefix(pref), monitor_(ios), on_syncing_(false), db_(pref)
{}

void synmon::add_monitor(std::string const &directory)
{
  error_code ec;
  fs::path entry = fs::system_complete(fs::path(directory), ec);
  if(ec || !fs::is_directory(entry))
    return;
  while(entry.filename() == ".") 
    entry.remove_filename();
  fs::path parent = entry;
  parent.remove_filename();
  fs::recursive_directory_iterator iter(entry), end;

  monitor_.add_directory(entry.string());
  monitor_.async_monitor(boost::bind(
      &synmon::handle_changes, this, _1, _2));
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
      //std::cout << "moniotor file: " << local_fullname << "\n";
      db_.add(ec, finfo);
    } else {
      monitor_.add_directory(iter->path().string());
      monitor_.async_monitor(boost::bind(
          &synmon::handle_changes, this, _1, _2));
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
  using namespace boost::asio;

  if(!ec) {
    fs::path dir(ev.dirname);
    std::string evstr;
    dir /= ev.filename;

    switch(ev.type) {
    case dir_monitor_event::added: 
      evstr = "created";
      monitor_.add_directory(dir.string());
    break;
    case dir_monitor_event::modified:
      evstr = "modified";
      add_monitor(dir.parent_path().string());
      {
        error_code internal_ec;
        db_.check_changes(internal_ec);
      }
      break;
    default:
      evstr = "changed (unknown event)";
    }
    //std::cout << dir.string() << " is " << evstr << "\n";
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

