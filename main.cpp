#include <iostream>
#include <signal.h>

#include "synmon.hpp"

int main(int argc, char **argv)
{
  boost::asio::io_service ios;
  boost::asio::signal_set sigset(ios);

  sigset.add( SIGINT );
  
  synmon sm(ios, argv[1]);
  sm.add_monitor(argv[2]);

  sigset.async_wait(boost::bind(&boost::asio::io_service::stop, &ios));

  ios.run();

  std::cerr << "Shutdown\n";

  return 0;
}
