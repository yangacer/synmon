#ifndef SYNMON_EXPORT_HPP_
#define SYNMON_EXPORT_HPP_

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/noncopyable.hpp>
#include <string>

class synmon;

class synmon_export
: boost::noncopyable
{
public:
  synmon_export(
    boost::asio::io_service &ios, 
    std::string const &prefix,
    std::string const &address,
    std::string const &account,
    std::string const &password)
    ;
  void on_auth(
    boost::function<bool(
      std::string &/* user */,
      std::string &/* password */)> handler)
    ;
  void on_transition(
    boost::function<void(std::string const& /* desc*/)> handler)
    ;
private:
  boost::shared_ptr<synmon> impl_;
};

#endif
