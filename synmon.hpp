#ifndef SYNMON_HPP_
#define SYNMON_HPP_

#include <string>
#include <set>
#include "dir_monitor/src/dir_monitor.hpp"
#include "db.hpp"

class synmon 
{
public:
  synmon(boost::asio::io_service &ios, std::string const &prefix);
  void add_directory(std::string const &dir);
  //void add_monitor(std::string const &directory);
  //void rm_monitor(std::string const &directory);
  std::string const prefix;
protected:
  void scan(std::string const &entry);
  void add_dir_to_conf(std::string const &dir);
  void handle_changes(
    boost::system::error_code const &ec, 
    boost::asio::dir_monitor_event const &ev);
  void handle_expiration(
    boost::system::error_code const &ec);
private:
  db db_;
  time_t last_scan_;
  size_t changes_;
  boost::asio::dir_monitor monitor_;
  boost::asio::deadline_timer timer_;
  std::set<std::string> on_monitored_dir_;
};

#endif
