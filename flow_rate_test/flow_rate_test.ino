#include <Arduino.h>

// 펌프 B 핀 번호 (기존 설정값)
const int PUMP_B = 26; 

// 릴레이 작동 방식 (보통 LOW에서 켜짐)
#define PUMP_ON LOW
#define PUMP_OFF HIGH

void setup() {
  Serial.begin(115200);
  
  // 펌프 핀 설정 및 초기화 (처음엔 꺼진 상태)
  pinMode(PUMP_B, OUTPUT);
  digitalWrite(PUMP_B, PUMP_OFF);
  
  Serial.println("--- Pump B Test System ---");
  Serial.println("Waiting for 3 minutes (180s) before start...");

  // 3분(180,000ms) 동안 대기
  delay(180000); 

  // 펌프 B 가동 시작
  Serial.println("Pump B Start: Running for 10 seconds");
  digitalWrite(PUMP_B, PUMP_ON);
  
  // 10초(10,000ms) 가동
  delay(10000);
  
  // 펌프 B 정지
  digitalWrite(PUMP_B, PUMP_OFF);
  Serial.println("Test Complete. Pump B is now OFF.");
  Serial.println("To test again, press the EN (Reset) button.");
}

void loop() {
  // loop를 비워두어 한 번만 실행되게 합니다.
}
