#include "synmon_export.hpp"
#include "synmon.hpp"

synmon_export::synmon_export(
  boost::asio::io_service &ios, 
  std::string const &prefix,
  std::string const &address,
  std::string const &account,
  std::string const &password)
{
  impl_.reset(
    new synmon(ios, prefix, address, account, password)
    );
}

void synmon_export::on_auth(
  boost::function<bool(
    std::string &/* user */,
    std::string &/* password */)> handler)
{
  impl_->on_auth(handler);
}

void synmon_export::on_transition(
  boost::function<void(std::string const& /* desc*/)> handler)
{
  impl_->on_transition(handler);
}
