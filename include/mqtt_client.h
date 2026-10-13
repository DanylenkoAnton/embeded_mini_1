#pragma once

bool mqttSetup();
bool publishSensorData();
void mqttLoop();  // keep-alive / обработка PINGRESP, вызывать каждый loop
