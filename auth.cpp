#include <iostream>
#include <string>
#include <cstdlib>
#include <boost/bind.hpp>

#include "agent/agent_v2.hpp"

void usage()
{
  std::cout <<
    "Usage: auth <address> <user_name> <password>\n"
    " Note the password has to be SHA-1 encrypted.\n"
    ;
  exit(0);
}

void handle_response(
  boost::system::error_code const &ec,
  http::request const &req,
  http::response const &rep,
  boost::asio::const_buffer buffer,
  int &code)
{
  if((ec && ec != boost::asio::error::eof) || 
     rep.status_code != 200 )
    code = 1;
}

int main(int argc, char **argv)
{
  using namespace std;

  if(argc < 4)
    usage();

  string 
    address (argv[1]),
    account (argv[2]),
    password(argv[3])
    ;
  
  boost::asio::io_service ios;
  agent_v2 agent_(ios);
  int code = 0;

  http::entity::url url("http://" + address + "/Node/User/Auth");
  http::request req;
  
  url.query.query_map.insert(make_pair("name", account));
  url.query.query_map.insert(make_pair("password", password));
  url.query.query_map.insert(make_pair("sha1", "1"));
  url.query.query_map.insert(make_pair("expire_time", "1"));

  agent_.async_request(
    url, req, "GET", true, 
    boost::bind(&handle_response, _1, _2, _3, _4, boost::ref(code))
    );

  ios.run();

  return code;
}
