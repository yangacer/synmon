#include <iostream>
#include "synmon.hpp"

int main(int argc, char **argv)
{
  boost::asio::io_service ios;

  synmon sm(ios);
  sm.add_monitor(argv[1]);
  ios.run();

  return 0;
}
