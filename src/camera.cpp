#include "camera.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

glm::mat4 Camera::getViewMatrix()
{
  glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.0f), position);
  glm::mat4 cameraRotation = getRotationMatrix();
  return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix()
{
  glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3 { 1.0f, 0.0f, 0.0f });
  glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3 { 0.0f, -1.0f, 0.0f });

  return glm::mat4_cast(yawRotation) * glm::mat4_cast(pitchRotation);
}

void Camera::update()
{
  glm::mat4 cameraRotation = getRotationMatrix();
  position += glm::vec3(cameraRotation * glm::vec4(velocity * 0.5f, 0.0f));
}