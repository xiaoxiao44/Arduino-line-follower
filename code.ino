#include <SoftwareSerial.h>

// JDY-31蓝牙软串口定义 RXD=13, TXD=12
SoftwareSerial bluetooth(12, 13);  //(RX,TX) RXD-TX,TXD-RX

// ── 引脚定义 ────────────────────────────────────────────────────────
// 四路TCRT5000红外巡线引脚 黑线=0，白底=1）
const int IR_LEFTMOST  = 11;   // 最左
const int IR_LEFT      = 10;   // 左中
const int IR_RIGHT     = 3;    // 右中
const int IR_RIGHTMOST = 2;    // 最右

// HC-SR04超声波引脚
const int TRIG_PIN     = A0;   // 超声波触发
const int ECHO_PIN     = A1;   // 超声波回声

// 电机引脚
const int IN1 = 7;             // 左电机正转
const int IN2 = 6;             // 左电机反转
const int IN3 = 5;             // 右电机正转
const int IN4 = 4;             // 右电机反转

// ── 参数配置 ────────────────────────────────────────────────────────
const int AVOID_DISTANCE   = 16;   // 避障触发距离 (cm)

// ── 状态变量 ────────────────────────────────────────────────────────
char btCommand = 'S';
bool autoMode = true;
unsigned long lastCheckTime = 0;

// ── 蓝牙心跳 ────────────────────────────────────────────────────────
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 1500;

// ── 非阻塞避障变量 ───────────────────────────────────────────────────
unsigned long avoidStartTime = 0;
int avoidPhase = 0;

// ── 电机控制─────────────────────────────────────────────────────────
void setMotor(int dirL, int dirR) {
  // 左电机方向：1=前进，-1=后退，0=停止
  digitalWrite(IN1, dirL == 1 ? HIGH : LOW);
  digitalWrite(IN2, dirL == -1 ? HIGH : LOW);

  // 右电机方向：1=前进，-1=后退，0=停止
  digitalWrite(IN3, dirR == 1 ? HIGH : LOW);
  digitalWrite(IN4, dirR == -1 ? HIGH : LOW);
}

void stopCar()        { setMotor(0, 0); }
void forward()        { setMotor(1, 1); }
void back()           { setMotor(-1, -1); }
void spinLeft()       { setMotor(-1, 1); }
void spinRight()      { setMotor(1, -1); }
void turnLeftSmooth() { setMotor(0, 1); }   // 左停右进 → 右偏
void turnRightSmooth(){ setMotor(1, 0); }   // 左进右停 → 左偏

// ── 超声波测距 ──────────────────────────────────────────────────────
long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 25000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// ── 非阻塞避障 ──────────────────────────────────────────────────────
void avoidObstacle() {
  unsigned long now = millis();
  switch (avoidPhase) {
    case 0:
      stopCar();
      avoidStartTime = now;
      avoidPhase = 1;
      break;

    case 1:  // 后退
      if (now - avoidStartTime >= 500) {
        back();
        avoidStartTime = now;
        avoidPhase = 2;
      }
      break;

    case 2:  // 右转避开
      if (now - avoidStartTime >= 600) {
        spinRight();
        avoidStartTime = now;
        avoidPhase = 3;
      }
      break;

    case 3:  // 前进脱离
      if (now - avoidStartTime >= 1000) {
        forward();
        avoidStartTime = now;
        avoidPhase = 4;
      }
      break;

    case 4:  // 恢复
      if (now - avoidStartTime >= 800) {
        avoidPhase = 0;
      }
      break;
  }
}

// ── 巡线逻辑 ────────────────────────────────────────────────────────
// 传感器电平 
//黑线 = 0（低电平）,白底 / 无遮挡 = 1（高电平）
void lineTracking() {
  int s1 = digitalRead(IR_LEFTMOST);
  int s2 = digitalRead(IR_LEFT);
  int s3 = digitalRead(IR_RIGHT);
  int s4 = digitalRead(IR_RIGHTMOST);

  if (s2 == 1 && s3 == 1) {
    forward();
  }
  else if (s2 == 0 && s3 == 1) {
    turnRightSmooth();
  }
  else if (s2 == 1 && s3 == 0) {
    turnLeftSmooth();
  }
  else if (s1 == 0) {
    spinRight();
  }
  else if (s4 == 0) {
    spinLeft();
  }
  else {
    stopCar();
  }
}

// ── 初始化 ──────────────────────────────────────────────────────────
void setup() {
  // 巡线红外传感器（INPUT_PULLUP模式，黑线=0，白底=1）
  pinMode(IR_LEFTMOST,  INPUT_PULLUP);
  pinMode(IR_LEFT,      INPUT_PULLUP);
  pinMode(IR_RIGHT,     INPUT_PULLUP);
  pinMode(IR_RIGHTMOST, INPUT_PULLUP);

  // 超声波
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // 电机方向引脚
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  Serial.begin(9600);
  bluetooth.begin(9600);

  stopCar();
  delay(1500);

  Serial.println("启动完成 - 无PWM控制");
  Serial.println("电机方向: 7,6,5,4");
  Serial.println("蓝牙: RX=13 TX=12");
  Serial.println("巡线: 9,8,3,2 (INPUT_PULLUP，黑线=0，白底=1)");
}

// ── 主循环 ──────────────────────────────────────────────────────────
void loop() {
  // 蓝牙心跳
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    bluetooth.print(".");
    lastHeartbeat = millis();
  }

  // 接收蓝牙指令
  if (bluetooth.available() > 0) {
    char cmd = bluetooth.read();
    if (cmd == '\n' || cmd == '\r') return;

    Serial.print("收到: "); Serial.println(cmd);

    char upperCmd = toupper(cmd);

    if (upperCmd == 'X') {
      autoMode = !autoMode;
      stopCar();
      Serial.print("模式切换: ");
      Serial.println(autoMode ? "自动巡线" : "手动控制");
      bluetooth.println(autoMode ? "自动模式" : "手动模式");
    } 
    else {
      btCommand = upperCmd;
      autoMode = false;

      // 增加蓝牙反馈
      bluetooth.print("执行: ");
      bluetooth.println(upperCmd);
    }
  }

  // 执行对应模式
  if (!autoMode) {
    // 手动模式
    switch (btCommand) {
      case 'F': forward();       break;
      case 'B': back();          break;
      case 'L': spinLeft();      break;
      case 'R': spinRight();     break;
      case 'A': turnLeftSmooth(); break;
      case 'D': turnRightSmooth(); break;
      case 'S': stopCar();       break;
      default:  stopCar();       break;
    }
  }
  else {
    // 自动模式：巡线 + 避障
    if (millis() - lastCheckTime > 80) {
      lastCheckTime = millis();

      long dist = getDistance();

      if (dist < AVOID_DISTANCE && dist > 0 && dist != 999) {
        avoidObstacle();
      }
      else {
        if (avoidPhase != 0) avoidPhase = 0;
        lineTracking();
      }
    }
  }
}
