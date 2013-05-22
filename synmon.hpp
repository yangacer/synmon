#ifndef SYNMON_HPP_
#define SYNMON_HPP_

#include <set>
#include "dir_monitor/src/dir_monitor.hpp"

class synmon 
{
public:
  synmon(boost::asio::io_service &ios);
  void add_monitor(std::string const &directory);
  void rm_monitor(std::string const &directory);
protected:
  void handle_changes(
    boost::system::error_code const &ec, 
    boost::asio::dir_monitor_event const &ev);
private:
  std::set<std::string> added_prefix_;
  boost::asio::dir_monitor monitor_;
};

#endif
