#include <SoftwareSerial.h>

// JDY-31蓝牙软串口定义 RXD=13, TXD=12
SoftwareSerial bluetooth(12, 13);  //(RX,TX) RXD-TX,TXD-RX

// 四路TCRT5000红外巡线引脚
const int IR_LEFTMOST  = 11;   // 最左红外
const int IR_LEFT      = 10;   // 左中红外
const int IR_RIGHT     = 3;    // 右中红外
const int IR_RIGHTMOST = 2;    // 最右红外

// HC-SR04超声波引脚
const int TRIG_PIN     = A1;   // 触发脚A0
const int ECHO_PIN     = A2;   // 回声脚A1

// L298N电机驱动引脚
const int ENA = 6;             // 左电机PWM调速
const int ENB = 5;             // 右电机PWM调速
const int IN1 = 8;             // 左电机正转
const int IN2 = 7;             // 左电机反转
const int IN3 = 9;             // 右电机正转
const int IN4 = 4;             // 右电机反转

// 速度与避障参数配置
int speedLevel = 5;            // 默认速度级别5
const int BASE_SPEED     = 150;// 基础速度基准值
const int SPIN_SPEED     = 150;// 原地自旋速度基准值
const int FAST_TURN      = 125;// 快速转向速度基准值
const int SLOW_TURN      = 70; // 慢速转向速度基准值
const int AVOID_DISTANCE = 16; // 避障触发距离(cm)

// 速度级别映射（1=最快255，10=最慢70）
const int SPEED_MAX = 255;     
const int SPEED_MIN = 70;      

// 状态控制变量
char btCommand = 'S';                             // 蓝牙默认指令-停止
bool autoMode = true;                             // 默认自动模式
unsigned long lastCheckTime = 0;                  // 巡线/避障检测计时
unsigned long lastHeartbeat = 0;                  // 蓝牙心跳计时
const unsigned long HEARTBEAT_INTERVAL = 1500;    // 心跳间隔1.5s
unsigned long avoidStartTime = 0;                 // 避障动作计时
int avoidPhase = 0;                               // 避障阶段标记

// 速度计算：根据级别返回当前基础速度（1-10级）
int getCurrentBaseSpeed() {
  int spd = SPEED_MAX - (SPEED_MAX - SPEED_MIN) * (speedLevel - 1) / 9;
  if (spd > 0 && spd < 60) spd = 60;              // 最低启动阈值60
  return spd;
}

// 速度缩放：按当前级别比例缩放基准速度
int getScaledSpeed(int original) {
  return (original * getCurrentBaseSpeed()) / 150;
}

// 电机底层控制（正=前进，负=后退）
void setMotor(int speedL, int speedR) {
  speedL = constrain(speedL, -255, 255);
  speedR = constrain(speedR, -255, 255);

  // 左电机控制
  digitalWrite(IN1, speedL >= 0 ? HIGH : LOW);
  digitalWrite(IN2, speedL >= 0 ? LOW : HIGH);
  analogWrite(ENA, abs(speedL));

  // 右电机控制
  digitalWrite(IN3, speedR >= 0 ? HIGH : LOW);
  digitalWrite(IN4, speedR >= 0 ? LOW : HIGH);
  analogWrite(ENB, abs(speedR));
}

// 电机动作封装
void stopCar()          { setMotor(0, 0); }
void forward()          { setMotor(getScaledSpeed(BASE_SPEED), getScaledSpeed(BASE_SPEED)); }
void back()             { setMotor(getScaledSpeed(-BASE_SPEED), getScaledSpeed(-BASE_SPEED)); }
void spinLeft()         { setMotor(getScaledSpeed(-SPIN_SPEED), getScaledSpeed(SPIN_SPEED)); }
void spinRight()        { setMotor(getScaledSpeed(SPIN_SPEED), getScaledSpeed(-SPIN_SPEED)); }
void turnLeftSmooth()   { setMotor(getScaledSpeed(SLOW_TURN), getScaledSpeed(FAST_TURN)); }
void turnRightSmooth()  { setMotor(getScaledSpeed(FAST_TURN), getScaledSpeed(SLOW_TURN)); }

// 超声波测距：返回距离(cm)，超量程返回999
long getDistance() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 20000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// 非阻塞避障逻辑：停止→后退→右转→前进
void avoidObstacle() {
  unsigned long now = millis();
  switch (avoidPhase) {
    case 0:
      stopCar();
      avoidStartTime = now;
      avoidPhase = 1;
      break;
    case 1:
      back();
      if (now - avoidStartTime >= 600) { avoidStartTime = now; avoidPhase = 2; }
      break;
    case 2:
      spinRight();
      if (now - avoidStartTime >= 700) { avoidStartTime = now; avoidPhase = 3; }
      break;
    case 3:
      forward();
      if (now - avoidStartTime >= 1200) { avoidPhase = 0; }
      break;
  }
}

// 四路红外巡线逻辑：直行/平滑转向/原地自旋纠偏
void lineTracking() {
  int s1 = digitalRead(IR_LEFTMOST);
  int s2 = digitalRead(IR_LEFT);
  int s3 = digitalRead(IR_RIGHT);
  int s4 = digitalRead(IR_RIGHTMOST);

  if (s2 == 1 && s3 == 1) { forward(); }
  else if (s2 == 0 && s3 == 1) { turnRightSmooth(); }
  else if (s2 == 1 && s3 == 0) { turnLeftSmooth(); }
  else if (s1 == 0) { spinRight(); }
  else if (s4 == 0) { spinLeft(); }
  else { stopCar(); }
}

// 初始化函数
void setup() {
  // 红外引脚设为上拉输入
  pinMode(IR_LEFTMOST,  INPUT_PULLUP);
  pinMode(IR_LEFT,      INPUT_PULLUP);
  pinMode(IR_RIGHT,     INPUT_PULLUP);
  pinMode(IR_RIGHTMOST, INPUT_PULLUP);

  // 电机驱动引脚设为输出
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // 超声波引脚初始化
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // 串口初始化
  Serial.begin(9600);
  bluetooth.begin(9600);

  // 开机初始化停止
  stopCar();
  delay(1000);
}

// 主循环函数
void loop() {
  // 蓝牙心跳包：每1.5s发送一个.
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    bluetooth.print(".");
    lastHeartbeat = millis();
  }

  // 蓝牙指令接收与解析
  if (bluetooth.available() > 0) {
    char cmd = bluetooth.read();
    if (cmd == '\n' || cmd == '\r') return; // 过滤换行符

    // 数字指令：调节速度级别(0-9 → 10-1级)
    if (cmd >= '0' && cmd <= '9') {
      speedLevel = (cmd == '0') ? 10 : cmd - '0';
      speedLevel = constrain(speedLevel, 1, 10);
      bluetooth.print("速度级别: ");
      bluetooth.println(speedLevel);
    } 
    // 字符指令：模式切换/手动控制
    else {
      btCommand = toupper(cmd);
      if (btCommand == 'X') {
        autoMode = !autoMode;
        stopCar();
        bluetooth.println(autoMode ? "已切换自动巡线模式" : "已切换手动控制模式");
      } else {
        autoMode = false;
        bluetooth.print("执行指令: ");
        bluetooth.println(btCommand);
      }
    }
  }

  // 模式执行：手动模式
  if (!autoMode) {
    switch (btCommand) {
      case 'F': forward(); break;
      case 'B': back(); break;
      case 'L': spinLeft(); break;
      case 'R': spinRight(); break;
      case 'A': turnLeftSmooth(); break;
      case 'D': turnRightSmooth(); break;
      case 'S': stopCar(); break;
      default: stopCar(); break;
    }
  } 
  // 模式执行：自动模式（巡线+避障）
  else {
    if (millis() - lastCheckTime > 80) { // 80ms检测一次，降低占用
      lastCheckTime = millis();
      long dist = getDistance();

      // 检测到障碍物，执行避障
      if (dist < AVOID_DISTANCE && dist > 0 && dist != 999) {
        avoidObstacle();
      } 
      // 无障碍物，执行巡线，复位避障状态
      else {
        if (avoidPhase != 0) avoidPhase = 0;
        lineTracking();
      }
    }
  }
}
