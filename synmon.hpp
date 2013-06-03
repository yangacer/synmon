#ifndef SYNMON_HPP_
#define SYNMON_HPP_

#include <string>
#include <set>
#include "dir_monitor/src/dir_monitor.hpp"
#include "agent/agent_v2.hpp"
#include "db.hpp"

class synmon 
{
public:
  synmon(boost::asio::io_service &ios, 
         std::string const &prefix,
         std::string const &account,
         std::string const &password);
  void add_directory(std::string const &dir);
  //void add_monitor(std::string const &directory);
  //void rm_monitor(std::string const &directory);
  std::string const prefix;
protected:
  void scan(std::string const &entry);
  void sync();
  void handle_sync(
    boost::system::error_code const &ec,
    http::request const &req,
    http::response const &rep,
    boost::asio::const_buffer buffer);
  void add_dir_to_conf(std::string const &dir);
  void handle_changes(
    boost::system::error_code const &ec, 
    boost::asio::dir_monitor_event const &ev);
  void handle_expiration(
    boost::system::error_code const &ec);
  void login(std::string const &account, std::string const &password);
  void handle_login(
    boost::system::error_code const &ec,
    http::request const &req,
    http::response const &rep,
    boost::asio::const_buffer buffer);
private:
  db db_;
  time_t last_scan_;
  size_t changes_;
  boost::asio::dir_monitor monitor_;
  boost::asio::deadline_timer timer_;
  std::set<std::string> on_monitored_dir_;
  agent_v2 agent_;
  std::string cookie_;
};

#endif
