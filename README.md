# KOOKBANG_ARM

2026 국방로봇경진대회용 계절별 주행 노드 + Summer 로봇팔 자동 파지 연동 워크스페이스입니다.

이 저장소는 NUC에서 ROS 2 Humble로 빌드/실행하는 것을 기준으로 구성합니다.

> 중요
>
> - 로봇팔은 **Summer 미션에서만 사용**합니다.
> - Spring / Autumn / Winter 로직은 기존 코드를 유지합니다.
> - Summer에서 `supply_box`가 검출되면 차체를 정지시키고 로봇팔을 자동 실행합니다.
> - 팔이 `GRASP -> LIFT -> HOME`을 완료한 뒤 `/drok_arm_auto/done=True`를 보내면 차체가 다시 주행합니다.
> - 이 저장소의 Summer 코드 자체는 별도 커스텀 라이브러리를 요구하지 않습니다.
> - 실제 팔 제어/IK는 별도 저장소 `DROK_ARM_IK`를 NUC에 같이 설치해야 합니다.

---

## 1. 시스템 동작

```text
Navigation
   |
   | /cmd_vel_nav
   v
Summer Node
   |
   +---- red_light ----------> chassis STOP
   |
   +---- supply_box detected
              |
              v
        /cmd_vel = 0
              |
          wait 0.5 s
              |
              v
 /drok_arm_auto/enable = True
              |
              v
        DROK_ARM_IK
              |
       YOLO center XYZ
              |
   camera_link -> ARM_BASE_LINK
              |
              v
        IK / APPROACH
              |
            CLOSE
              |
            LIFT
              |
            HOME
              |
              v
 /drok_arm_auto/done = True
              |
              v
      Summer resumes
       /cmd_vel_nav
```

Summer 실행 중 한 번 파지한 `supply_box`가 카메라에 계속 보여도 다시 팔을 실행하지 않도록 재트리거 방지 로직이 들어가 있습니다.

---

## 2. GitHub 구조

```text
KOOKBANG_ARM/
├── README.md
├── .gitignore
└── src/
    └── drokck/
        ├── CMakeLists.txt
        ├── package.xml
        ├── spring_node.cpp
        ├── summer_node.cpp
        ├── autumn_node.cpp
        ├── winter_node.cpp
        └── image_stitcher.py
```

`build/`, `install/`, `log/`, 백업 파일은 GitHub에 넣지 않습니다.

---

# 3. NUC 설치 위치

NUC 홈 디렉토리에 아래 두 workspace를 둡니다.

```text
/home/<USER>/
├── KOOKBANG_ARM/
└── DROK_ARM_IK/
```

예를 들어 NUC 계정명이 `hgui`라면:

```text
/home/hgui/KOOKBANG_ARM
/home/hgui/DROK_ARM_IK
```

---

# 4. NUC 최초 설치

## 4-1. KOOKBANG_ARM

```bash
cd ~

git clone https://github.com/jhj0129/KOOKBANG_ARM.git

cd ~/KOOKBANG_ARM

source /opt/ros/humble/setup.bash

rosdep install --from-paths src --ignore-src -r -y

colcon build --symlink-install
```

빌드 후:

```bash
source ~/KOOKBANG_ARM/install/setup.bash

ros2 pkg list | grep drokck
```

정상이면:

```text
drokck
```

가 표시됩니다.

## 4-2. DROK ARM 실제 팔 코드

별도 workspace로 설치합니다.

```bash
cd ~

git clone https://github.com/jhj0129/DROK_ARM_IK.git

cd ~/DROK_ARM_IK

source /opt/ros/humble/setup.bash

colcon build --symlink-install
```

환경 확인:

```bash
source ~/DROK_ARM_IK/tools/source_env.sh
```

---

# 5. Summer ARM 연동에 필요한 ROS 토픽

## Summer가 사용

```text
/camera/camera/color/image_raw
/imu
/yolo_bbox_raw
/yolo_detected_object
/cmd_vel_nav
```

## Summer가 출력

```text
/cmd_vel
/ui_combined_vision
/light_status
/drok_arm_auto/enable
```

## Summer가 ARM에서 받음

```text
/drok_arm_auto/done
```

## ARM이 물체 위치에 사용

```text
/yolo_detected_object
/yolo_object_xyz
```

`/yolo_object_xyz`는 `geometry_msgs/msg/Vector3Stamped`이며 `camera_link` 기준으로 들어와야 합니다.

---

# 6. 대회 실행 순서

각 프로세스는 별도 터미널에서 실행하는 것을 권장합니다.

## Terminal 1 - 실제 팔 통신

```bash
source ~/DROK_ARM_IK/tools/source_env.sh

bash ~/DROK_ARM_IK/tools/run_real.sh
```

이 터미널은 계속 켜둡니다.

## Terminal 2 - ARM 자동 파지 노드

대회 전 반드시 CAMERA MODE인지 확인합니다.

```bash
grep -n "USE_FIXED_PRACTICE_TARGET" \
~/DROK_ARM_IK/tools/drok_auto_grasp_prototype1.py
```

대회에서는 반드시:

```python
USE_FIXED_PRACTICE_TARGET = False
```

이어야 합니다.

실행:

```bash
source ~/DROK_ARM_IK/tools/source_env.sh

bash ~/DROK_ARM_IK/tools/run_drok_auto_grasp_prototype1.sh
```

대회에서는 수동 trigger script를 실행하지 않습니다.

Summer가 `supply_box`를 검출하면 자동으로 `/drok_arm_auto/enable = True`를 발행합니다.

## Terminal 3 - YOLO + 중심 XYZ

현재 대회에서 사용하는 YOLO 노드를 실행합니다.

반드시 다음 토픽이 나와야 합니다.

```bash
ros2 topic echo /yolo_detected_object --once
```

```bash
ros2 topic echo /yolo_object_xyz --once
```

`supply_box` 검출 시 class와 같은 물체의 중심 3D 좌표가 함께 들어와야 합니다.

## Terminal 4 - Camera -> Robot TF

ARM은 YOLO의 `camera_link` 좌표를 `ARM_BASE_LINK` 좌표로 변환해서 IK에 사용합니다.

반드시 다음 TF가 존재해야 합니다.

```text
ARM_BASE_LINK -> camera_link
```

현재 translation baseline:

```text
x = -0.400 m
y = +0.065 m
z = +0.470 m
```

단, **카메라 회전 roll / pitch / yaw는 실제 장착 방향을 측정한 최종값을 사용해야 합니다.**

```bash
ros2 run tf2_ros static_transform_publisher \
  --x -0.400 \
  --y 0.065 \
  --z 0.470 \
  --roll <ROLL_RAD> \
  --pitch <PITCH_RAD> \
  --yaw <YAW_RAD> \
  --frame-id ARM_BASE_LINK \
  --child-frame-id camera_link
```

TF 확인:

```bash
ros2 run tf2_ros tf2_echo ARM_BASE_LINK camera_link
```

## Terminal 5 - Summer Node

```bash
cd ~/KOOKBANG_ARM

source /opt/ros/humble/setup.bash
source ~/KOOKBANG_ARM/install/setup.bash

ros2 run drokck summer_node
```

---

# 7. 실제 자동 동작 순서

```text
1. 차량 주행
2. YOLO에서 supply_box 검출
3. Summer가 /cmd_vel = 0
4. 0.5초 동안 차체 정지 안정화
5. Summer가 /drok_arm_auto/enable=True 1회 발행
6. ARM이 supply_box의 XYZ를 수집
7. camera_link -> ARM_BASE_LINK TF 변환
8. IK 계산
9. PREALIGN
10. APPROACH1
11. APPROACH2
12. gripper CLOSE
13. LIFT
14. HOME 복귀
15. ARM이 /drok_arm_auto/done=True 발행
16. Summer가 기존 /cmd_vel_nav 주행 재개
```

팔이 HOME으로 복귀한 뒤에도 그리퍼는 물체를 잡은 상태를 유지합니다.

---

# 8. Summer에서 수정 가능한 파라미터

파일:

```text
~/KOOKBANG_ARM/src/drokck/summer_node.cpp
```

## 차체 정지 후 ARM 시작 대기시간

현재:

```cpp
static constexpr double ARM_CHASSIS_SETTLE_SEC = 0.5;
```

차량 관성이 크면 값을 늘립니다.

## Summer 주행 속도

Summer는 기본적으로 `/cmd_vel_nav`를 받아 `/cmd_vel`로 전달합니다. 따라서 기본 주행속도는 navigation 쪽에서 조절합니다.

Summer 코드에는 다음 IMU 기반 배율 로직이 있습니다.

```cpp
if (out_msg.linear.x != 0.0 &&
    std::abs(current_imu_linear_x) < 0.1)
{
    out_msg.linear.x *= 2.0;
}
```

현재 값:

```text
IMU threshold = 0.1
linear speed multiplier = 2.0
```

---

# 9. ARM에서 수정 가능한 위치 파라미터

파일:

```text
~/DROK_ARM_IK/tools/drok_auto_grasp_prototype1.py
```

## 대회 / 연습 모드

```python
USE_FIXED_PRACTICE_TARGET = False
```

```text
False = 실제 YOLO + TF 좌표 사용
True  = 고정 연습좌표 사용
```

대회에서는 `False`.

## 로봇 기준 위치 오프셋

```python
ROBOT_OFFSET_FORWARD_CM = 0.0
ROBOT_OFFSET_RIGHT_CM = 0.0
ROBOT_OFFSET_UP_CM = 0.0
```

좌표 정의:

```text
FORWARD + : ARM_BASE_LINK +X
RIGHT   + : ARM_BASE_LINK -Y
UP      + : ARM_BASE_LINK +Z
```

예를 들어 실제 그리퍼가 검출점보다 항상 2 cm 왼쪽에 도착하면:

```python
ROBOT_OFFSET_RIGHT_CM = 2.0
```

## 연습용 고정좌표

```python
FIXED_GRASP_X_M = 0.4000
FIXED_GRASP_Y_M = 0.0000
FIXED_GRASP_Z_M = -0.1000
```

## YOLO 좌표 안정화

```python
REQUIRED_SAMPLES = 5
SAMPLE_WINDOW_SEC = 1.0
MAX_MEDIAN_DEVIATION_M = 0.08
```

## 허용 작업공간

```python
WORKSPACE_X_MIN_M = 0.10
WORKSPACE_X_MAX_M = 0.75
WORKSPACE_Y_MIN_M = -0.55
WORKSPACE_Y_MAX_M = +0.55
WORKSPACE_Z_MIN_M = -0.40
WORKSPACE_Z_MAX_M = 0.65
```

안전 영역이므로 실제 팔 workspace 확인 없이 무작정 확장하지 않습니다.

---

# 10. ARM 속도 관련 파라미터

파일:

```text
~/DROK_ARM_IK/tools/interactive_box_ik_grasp_v11.py
```

현재:

```python
REAL_CURRENT_TO_PREALIGN_SEC = 1.2
REAL_APPROACH1_SEC = 6.0*2
REAL_APPROACH2_SEC = 3.0*2
REAL_GRASP_TO_LIFT_SEC = 3.0*2
REAL_GRIPPER_CLOSE_SEC = 3.0*2
```

즉:

```text
CURRENT -> PREALIGN : 1.2 s
APPROACH1           : 12 s
APPROACH2           : 6 s
GRASP -> LIFT       : 6 s
GRIPPER CLOSE       : 6 s
```

**시간값이 작아질수록 팔은 빨라집니다.** 실제 팔에서 속도를 변경할 때는 단계적으로 확인합니다.

---

# 11. 접근 / Lift 파라미터

```python
NEAR_STANDOFF_M = 0.09
LIFT_HEIGHT_M = 0.05
```

```text
NEAR_STANDOFF_M = 물체 위 9 cm에서 최종 접근 시작
LIFT_HEIGHT_M   = 파지 후 5 cm 들어올림
```

---

# 12. HOME 이동시간

파일:

```text
~/DROK_ARM_IK/tools/drok_auto_grasp_prototype1.py
```

현재:

```python
START_HOME_SEC = 3.0*2
RETURN_HOME_SEC = 3.0*2
```

즉 시작 HOME / 복귀 HOME은 각각 6초입니다.

---

# 13. Gripper 파라미터

파일:

```text
~/DROK_ARM_IK/tools/interactive_box_ik_grasp_v11.py
```

현재 코드 기준:

```python
GRIPPER_OPEN_GAP_CM = 14.6
GRIPPER_OPEN_PROTOCOL_DEG = 105.11
GRIPPER_CLOSE_GAP_CM = 9.7
GRIPPER_CLOSE_PROTOCOL_DEG = 1172.96
GRIPPER_SPEED_DPS = 449
```

실물 gripper calibration을 다시 한 경우 실제 저장값을 기준으로 업데이트합니다.

---

# 14. TCP 5 cm 보정

현재 IK geometry에는 실측한 TCP 보정:

```text
gripper_center -> gripper_tcp
local +X = 0.05 m
```

가 적용되어 있어야 합니다.

확인 파일:

```text
~/DROK_ARM_IK/src/drok_arm_kinematics/config/drok_arm_kinematics_only.urdf
~/DROK_ARM_IK/src/drok_arm_kinematics/config/robot_geometry.yaml
```

기준값:

```text
xyz = [0.05, 0.0, 0.0]
```

TCP를 수정했다면:

```bash
cd ~/DROK_ARM_IK
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select drok_arm_kinematics
```

---

# 15. 대회 전 토픽 점검

```bash
ros2 topic echo /joint_states --once
ros2 topic echo /yolo_detected_object
ros2 topic echo /yolo_object_xyz
ros2 topic echo /drok_arm_auto/enable
ros2 topic echo /drok_arm_auto/done
ros2 topic echo /cmd_vel
```

---

# 16. ARM handshake만 테스트

실제 YOLO를 사용하기 전에 Summer의 ARM handshake만 확인하려면:

```bash
source /opt/ros/humble/setup.bash

ros2 topic pub --once \
  /yolo_detected_object \
  std_msgs/msg/String \
  "{data: 'supply_box'}"
```

Summer 로그에서:

```text
[ARM] supply_box detected. Chassis STOP.
```

약 0.5초 후:

```text
[ARM] Chassis settled. /drok_arm_auto/enable=True published.
```

가 나와야 합니다.

주의: 실제 ARM auto node까지 켜져 있으면 이 신호로 실제 팔이 움직일 수 있습니다.

---

# 17. 다시 빌드해야 하는 경우

`src/drokck`의 C++/CMake/package 파일을 수정했다면:

```bash
cd ~/KOOKBANG_ARM
source /opt/ros/humble/setup.bash
colcon build --symlink-install
source ~/KOOKBANG_ARM/install/setup.bash
```

---

# 18. 주의사항

- CAN interface state / bitrate는 Summer 코드에서 변경하지 않습니다.
- 모터 firmware / ROM 설정을 Summer 코드에서 변경하지 않습니다.
- ARM 작업 중에는 Summer가 `/cmd_vel=0`을 유지합니다.
- `/drok_arm_auto/done=True`가 오기 전에는 차량이 자동으로 다시 출발하지 않습니다.
- 현재 구조는 Summer 실행 한 번당 `supply_box` ARM 미션을 한 번만 수행합니다.
- 카메라 TF rotation은 실제 장착 자세에 맞춘 최종값이 필요합니다.
- 대회에서는 `USE_FIXED_PRACTICE_TARGET=False`를 반드시 확인합니다.
