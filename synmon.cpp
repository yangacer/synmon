#include "synmon.hpp"
#include <boost/filesystem.hpp>
#include <iostream>

namespace fs = boost::filesystem;
namespace sys = boost::system;

synmon::synmon(boost::asio::io_service &ios)
  : monitor_(ios)
{}

void synmon::add_monitor(std::string const &directory)
{
  auto res = added_prefix_.insert(directory);
  if(!res.second) return;
  
  sys::error_code ec;
  fs::path entry = fs::system_complete(fs::path(directory), ec);
  if(ec) return;
  fs::path parent = entry;
  parent.remove_filename().remove_filename();
  fs::recursive_directory_iterator iter(entry), end;

  while(iter != end) {
    if(!fs::is_directory(iter->path())) {
      std::cout << iter->path().string() << " - " <<
        fs::last_write_time(iter->path(), ec) << " ${sync_foler}" << 
        iter->path().string().substr(parent.string().size()) <<
        "\n"; 
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
