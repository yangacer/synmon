#include <iostream>
#include "synmon.hpp"

int main(int argc, char **argv)
{
  boost::asio::io_service ios;

  synmon sm(ios, argv[1]);
  sm.add_monitor(argv[2]);
  ios.run();

  return 0;
}
