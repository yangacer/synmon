#ifndef SYNMON_HPP_
#define SYNMON_HPP_

#include <string>
#include <set>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include "dir_monitor/src/dir_monitor.hpp"
#include "json/variant.hpp"
#include "agent/agent_v2.hpp"
#include "db.hpp"

struct dl_ctx;

class synmon 
{
  typedef boost::shared_ptr<dl_ctx> shared_dl_ctx;
  typedef boost::shared_ptr<std::string> shared_buffer;
  typedef boost::shared_ptr<json::var_t> shared_json_var;
public:
  synmon(boost::asio::io_service &ios, 
         std::string const &prefix,
         std::string const &address,
         std::string const &account,
         std::string const &password);
  ~synmon();
  void add_directory(std::string const &dir);
  void on_auth(
    boost::function<bool(
      std::string &/* user */,
      std::string &/* password */)> handler)
    ;
  void on_transition(
    boost::function<void(std::string const& /* desc*/)> handler)
    ;
  //void add_monitor(std::string const &directory);
  //void rm_monitor(std::string const &directory);
  std::string const prefix;
  std::string const address;
protected:
  http::entity::query_map_t describe_file(std::string const &local_name) const;
  std::string to_local_name(std::string const &remote_name) const;
  void schedule_local_check();
  void scan(std::string const &entry);
  void sync_check();
  void handle_sync_check(
    boost::system::error_code const &ec,
    http::request const &req,
    http::response const &rep,
    boost::asio::const_buffer buffer,
    shared_buffer body);
  
  void sync(shared_json_var var);
  void handle_reading(
    boost::system::error_code const &ec,
    http::request const &req,
    http::response const &rep,
    boost::asio::const_buffer buffer,
    shared_buffer body,
    shared_json_var var);

  void handle_writing(
    boost::system::error_code const &ec,
    http::request const &req,
    http::response const &rep,
    boost::asio::const_buffer buffer,
    shared_dl_ctx dl_ctx,
    shared_json_var var);

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
  bool on_syncing_;
  boost::asio::dir_monitor monitor_;
  boost::asio::deadline_timer timer_;
  std::set<std::string> on_monitored_dir_;
  agent_v2 agent_;
  std::string account_;
  std::string password_;
  std::string cookie_;
  boost::function<bool(
    std::string &/* user */,
    std::string &/* password */)> auth_handler_;
  boost::function<void(
    std::string const& /* desc*/)> trans_handler_;
};

#endif
