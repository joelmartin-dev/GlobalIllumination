#include "camera.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

glm::mat4 Camera::getViewMatrix()
{
  return glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::getRotationMatrix()
{
  return glm::identity<glm::mat4>();
}

void Camera::update(float delta)
{
  forward.x = cos(yaw) * cos(pitch);
  forward.y = sin(pitch);
  forward.z = sin(yaw) * cos(pitch);
  forward = glm::normalize(forward);

  right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

  pitch += static_cast<float>(deltaPitch) * pitchSpeed * delta;
  pitch = glm::mod(pitch + glm::pi<float>(), glm::pi<float>() * 2.0f) - glm::pi<float>();
  yaw += static_cast<float>(deltaYaw) * yawSpeed * delta;
  yaw = glm::mod(yaw + glm::pi<float>(), glm::pi<float>() * 2.0f) - glm::pi<float>();

  float mod = shiftMod ? shiftSpeed : 1.0f;

  position += (forward * velocity.z + right * velocity.x + glm::vec3(0.0f, 1.0f, 0.0f) * velocity.y) * moveSpeed * delta * mod;
}

void Camera::cursor_pos_callback(double xpos, double ypos)
{
  double deltaXpos = oldXpos - xpos;
  double deltaYpos = oldYpos - ypos;

  oldXpos = xpos;
  oldYpos = ypos;

  deltaPitch = deltaYpos;
  deltaYaw = -deltaXpos;
}

void Camera::key_callback(GLFWwindow* pWindow, int key, int scancode, int action, int mods)
{
  // unused
  (void)scancode; (void) mods;
  
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
  {
    glfwSetWindowShouldClose(pWindow, GLFW_TRUE);
  }

  if (action == GLFW_REPEAT || action == GLFW_PRESS)
  {
    //std::clog << "PRESSING" << std::endl;
    switch (key)
    {
      case GLFW_KEY_W:
        velocity.z = 1.0f;
        break;
      case GLFW_KEY_A:
        velocity.x = -1.0f;
        break;
      case GLFW_KEY_S:
        velocity.z = -1.0f;
        break;
      case GLFW_KEY_D:
        velocity.x = 1.0f;
        break;
      case GLFW_KEY_Q:
        velocity.y = -1.0f;
        break;
      case GLFW_KEY_E:
        velocity.y = 1.0f;
        break;
      case GLFW_KEY_UP:
        deltaPitch = 1.0f;
        break;
      case GLFW_KEY_LEFT:
        deltaYaw = -1.0f;
        break;
      case GLFW_KEY_DOWN:
        deltaPitch = -1.0f;
        break;
      case GLFW_KEY_RIGHT:
        deltaYaw = 1.0f;
        break;
      case GLFW_KEY_LEFT_SHIFT:
        shiftMod = true;
        break;
      case GLFW_KEY_F:
        glfwSetInputMode(pWindow, GLFW_CURSOR, glfwGetInputMode(pWindow, GLFW_CURSOR) == GLFW_CURSOR_NORMAL ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        break;
      default:
        break;
    }
  }

  if (action == GLFW_RELEASE)
  {
    switch (key)
    {
      case GLFW_KEY_W:
      case GLFW_KEY_S:
        velocity.z = 0.0f;
        break;
      case GLFW_KEY_A:
      case GLFW_KEY_D:
        velocity.x = 0.0f;
        break;
      case GLFW_KEY_Q:
      case GLFW_KEY_E:
        velocity.y = 0.0f;
        break;
      case GLFW_KEY_UP:
      case GLFW_KEY_DOWN:
        deltaPitch = 0.0f;
        break;
      case GLFW_KEY_LEFT:
      case GLFW_KEY_RIGHT:
        deltaYaw = 0.0f;
        break;
      case GLFW_KEY_LEFT_SHIFT:
        shiftMod = false;
        break;
      default:
        break;
    }
  }
}