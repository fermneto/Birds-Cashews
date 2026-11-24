#include "events/SkyPlaneShots.h"
#include <opencv2/imgcodecs.hpp>
#include <iostream>

// O aviao parado no ceu, com o nariz para -Z, como sai de um PlaneLaunched antes de comecar a perseguir.
void captureSkyPlaneShots(const std::string& outputDir, const std::function<cv::Mat(glm::vec3, glm::vec3)>& shoot,
                          GameplaySession& session) {
    const glm::vec3 where(0, kPlaneTriggerHeight + kPlaneSpawnRise, -200);
    GameEvent launch;
    launch.type = GameEventType::PlaneLaunched;
    launch.actor = kSkyPlaneActor;
    launch.target = kNoPlayer;
    launch.value = 250;
    launch.position = where;
    session.skyPlane().handle(launch);
    cv::imwrite(outputDir + "/20_aviao_perto.png", shoot(where + glm::vec3(-26, 6, -30), where));
    cv::imwrite(outputDir + "/21_aviao_longe.png", shoot(where + glm::vec3(-60, -10, -kPlaneSpawnDistance * 0.6f), where));
    cv::imwrite(outputDir + "/22_aviao_de_lado.png", shoot(where + glm::vec3(-45, 2, 0), where));
    std::cout << "Aviao do teto: " << session.skyPlane().planes().size() << " no ceu para as fotos\n";
}
