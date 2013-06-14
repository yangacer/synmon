#include <iostream>
#include <signal.h>

#include "synmon.hpp"

void usage()
{
  std::cout << 
    "Usage: synmon <prefix> <directory> <address> <user_name> <password>\n"
    " prefix    - For storing configuration and log file.\n"
    " directory - Directory to sync.\n"
    " address   - Device address.\n"
    " user_name - Account of nucs-arm.\n"
    ;
  exit(0);
}

int main(int argc, char **argv)
{
  if(argc < 5)
    usage();

  boost::asio::io_service ios;
  boost::asio::signal_set sigset(ios);

  sigset.add( SIGINT );

  try {
    synmon sm(ios, argv[1], argv[3], argv[4], argv[5]);
    sm.add_directory(argv[2]);

    sigset.async_wait(boost::bind(&boost::asio::io_service::stop, &ios));

    ios.run();
  } catch (std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
  }

  std::cout << "\nsynmon closed\n";

  return 0;
}
