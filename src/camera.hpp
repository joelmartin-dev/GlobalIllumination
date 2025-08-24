#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#include <glm/ext/quaternion_common.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

class Camera
{
  public:
  glm::vec3 velocity = glm::vec3(0.0f);
  glm::vec3 position = glm::vec3(0.0f, 0.3f, 0.0f);
  float pitch { 0.0f };
  float deltaPitch { 0.0f };
  float yaw { 0.0f };
  float deltaYaw { 0.0f };
  float moveSpeed { 1.0f };
  float rotSpeed { 2.0f };
  bool shiftMod { false };
  float shiftSpeed { 2.0f };

  double oldXpos { 0.0f };
  double oldYpos { 0.0f };

  glm::vec3 forward;
  glm::vec3 right;

  glm::mat4 getViewMatrix();
  glm::mat4 getRotationMatrix();

  void update(float delta);

  void cursor_pos_callback(double xpos, double ypos);
  void key_callback(GLFWwindow* pWindow, int key, int scancode, int action, int mods);
};

#endif