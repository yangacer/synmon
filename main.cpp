#include <iostream>
#include <signal.h>

#include "synmon.hpp"

int main(int argc, char **argv)
{
  boost::asio::io_service ios;
  boost::asio::signal_set sigset(ios);

  sigset.add( SIGINT );
  try {
    synmon sm(ios, argv[1], "admin", "admin");
    sm.add_directory(argv[2]);

    sigset.async_wait(boost::bind(&boost::asio::io_service::stop, &ios));

    ios.run();
  } catch (std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
  }

  std::cerr << "Shutdown\n";

  return 0;
}
