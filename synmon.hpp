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
  void add_monitor(std::string const &directory);
  void rm_monitor(std::string const &directory);
protected:
  void handle_changes(
    boost::system::error_code const &ec, 
    boost::asio::dir_monitor_event const &ev);
  void handle_expiration(
    boost::system::error_code const &ec);
private:
  bool on_syncing_;
  db db_;
  boost::asio::dir_monitor monitor_;
};

#endif
