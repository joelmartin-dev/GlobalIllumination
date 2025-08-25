#include <cstdlib>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    main();
    return 0;
}