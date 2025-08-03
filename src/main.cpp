#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "app.hpp"


int main()
{
  App app;

  try
  {
    app.run();
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}