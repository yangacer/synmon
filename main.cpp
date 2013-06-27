#include <iostream>
#include <signal.h>

#include "synmon.hpp"

void usage()
{
  std::cout << 
    "Usage: synmon <prefix> <address> <user_name> <password> [sync_dir]\n"
    " prefix    - For storing configuration and log file.\n"
    " address   - Device address.\n"
    " user_name - Account of nucs-arm.\n"
    " password  - Password for account above.\n"
    " sync_dir  - Directory to be synced.\n"
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
  sigset.add( SIGTERM );

  try {
    synmon sm(ios, argv[1], argv[2], argv[3], argv[4]);

    if(argc > 5) {
      sm.add_directory(argv[5]);
    }

    sigset.async_wait(boost::bind(&boost::asio::io_service::stop, &ios));

    ios.run();
  } catch (std::exception &e) {
    std::cerr << "exception: " << e.what() << "\n";
  }

  std::cout << "\nsynmon closed\n";

  return 0;
}
