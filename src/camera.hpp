#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

class Camera
{
  public:
  glm::vec3 velocity;
  glm::vec3 position;
  float pitch { 0.0f };
  float yaw { 0.0f };

  glm::mat4 getViewMatrix();
  glm::mat4 getRotationMatrix();

  void update();
};

#endif