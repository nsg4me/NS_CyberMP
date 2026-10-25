#include "Utils.h"

Red::WorldPosition Game::ToRed(const glm::vec3& acPosition)
{
    return Red::WorldPosition(Red::Vector3{acPosition.x, acPosition.y, acPosition.z});
}

glm::vec3 Game::ToGlm(const Red::WorldPosition& acPosition)
{
    const auto pos = acPosition.AsVector3();
    return {pos.X, pos.Y, pos.Z};
}

Red::Quaternion Game::ToRed(const glm::quat& acOrientation)
{
    return {acOrientation.x, acOrientation.y, acOrientation.z, acOrientation.w};
}

glm::quat Game::ToGlm(const Red::Quaternion& acOrientation)
{
    return {acOrientation.r,
        acOrientation.i,
        acOrientation.j,
        acOrientation.k};
}