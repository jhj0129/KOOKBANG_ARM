#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/string.hpp>
// ARM: Summer -> ARM enable, ARM -> Summer done 신호를 위해 Bool 메시지 추가
#include <std_msgs/msg/bool.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/imu.hpp> // IMU 메시지 추가
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>

class SummerNode : public rclcpp::Node {
public:
    SummerNode() : Node("Summer_Node") {
        img_sub_ = this->create_subscription<sensor_msgs::msg::Image>(
            "/camera/camera/color/image_raw", 10,
            std::bind(&SummerNode::process_front, this, std::placeholders::_1));

        imu_sub_ = this->create_subscription<sensor_msgs::msg::Imu>(
            "/imu", 10, [this](const sensor_msgs::msg::Imu::SharedPtr msg) {
                this->current_imu_linear_x = msg->linear_acceleration.x;
            });

        yolo_sub_ = this->create_subscription<std_msgs::msg::String>(
            "/yolo_bbox_raw", 10, [this](const std_msgs::msg::String::SharedPtr msg) {
            std::string data = msg->data;
            auto status_msg = std_msgs::msg::String();

            if (data.find("red_light") != std::string::npos) {
                is_red_light = true;
                status_msg.data = "STOP";
                status_pub_->publish(status_msg);
            }
            else if (data.find("green_light") != std::string::npos) {
                is_red_light = false;
                status_msg.data = "GO";
                status_pub_->publish(status_msg);
            }

            // ARM: 기존 supply_box 20초 대기 대신 ARM 미션 시작 준비
            if (data.find("supply_box") != std::string::npos) {
                start_arm_sequence_if_needed();
            }
        });

        nav_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
            "/cmd_vel_nav", 10, [this](const geometry_msgs::msg::Twist::SharedPtr msg) {
                this->current_nav = *msg;
            });

        cmd_pub_ = this->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);
        ui_image_pub_ = this->create_publisher<sensor_msgs::msg::Image>("/ui_combined_vision", 10);
        status_pub_ = this->create_publisher<std_msgs::msg::String>("/light_status", 10);

        // ARM: q.py가 발행하는 supply_box class를 직접 받아 ARM 미션 시작에 사용
        // ARM: 기존 /yolo_bbox_raw는 신호등 및 기존 Summer 기능 유지를 위해 그대로 둠
        arm_yolo_sub_ = this->create_subscription<std_msgs::msg::String>(
            "/yolo_detected_object", 10,
            [this](const std_msgs::msg::String::SharedPtr msg) {
                if (msg->data.find("supply_box") != std::string::npos) {
                    start_arm_sequence_if_needed();
                }
            });

        // ARM: Summer -> 자동팔 시작 신호
        arm_enable_pub_ = this->create_publisher<std_msgs::msg::Bool>(
            "/drok_arm_auto/enable", 10);

        // ARM: 자동팔이 grasp -> lift -> HOME을 완료했는지 확인
        arm_done_sub_ = this->create_subscription<std_msgs::msg::Bool>(
            "/drok_arm_auto/done", 10,
            [this](const std_msgs::msg::Bool::SharedPtr msg) {
                if (!msg->data) {
                    return;
                }

                // ARM: 현재 ARM 미션이 실제로 진행 중일 때만 DONE을 처리
                if (is_waiting && arm_started) {
                    is_waiting = false;
                    arm_started = false;

                    // ARM: 한 번 잡은 supply_box가 카메라에 계속 보여도 재실행하지 않음
                    arm_mission_completed = true;

                    RCLCPP_INFO(
                        this->get_logger(),
                        "[ARM] DONE received. Grasp + HOME complete. Resume chassis.");
                }
            });
    }

private:
    // ARM: supply_box가 여러 프레임 연속 검출되어도 ARM 미션은 한 번만 시작
    void start_arm_sequence_if_needed() {
        if (is_waiting || arm_started || arm_mission_completed) {
            return;
        }

        // ARM: supply_box 검출 즉시 차체 정지 인터락 시작
        is_waiting = true;
        arm_started = false;
        wait_start_time = this->now();

        RCLCPP_INFO(
            this->get_logger(),
            "[ARM] supply_box detected. Chassis STOP.");
    }

    void process_front(const sensor_msgs::msg::Image::SharedPtr mf) {
        try {
            cv::Mat img_f = cv_bridge::toCvCopy(mf, "bgr8")->image;
            cv::Size target_size(640, 480);
            cv::resize(img_f, img_f, target_size);

            auto out_msg = geometry_msgs::msg::Twist();

            if (is_red_light) {
                out_msg.linear.x = 0.0;
                out_msg.angular.z = 0.0;
            }
            // ARM: 기존 20초 대기 로직 제거
            // ARM: ARM이 DONE=True를 보낼 때까지 차체를 계속 정지
            else if (is_waiting) {
                out_msg.linear.x = 0.0;
                out_msg.angular.z = 0.0;
            }
            else {
                out_msg = current_nav;
            }

            // ARM: 차체 정지 명령 후 3초 안정화 시간을 확보한 다음 ARM을 한 번만 시작
            if (is_waiting && !arm_started) {
                double elapsed = (this->now() - wait_start_time).seconds();

                if (elapsed >= ARM_CHASSIS_SETTLE_SEC) {
                    auto enable_msg = std_msgs::msg::Bool();
                    enable_msg.data = true;

                    // ARM: /drok_arm_auto/enable=True 1회 발행
                    arm_enable_pub_->publish(enable_msg);
                    arm_started = true;

                    RCLCPP_INFO(
                        this->get_logger(),
                        "[ARM] Chassis settled. /drok_arm_auto/enable=True published.");
                }
            }

            // 2. IMU linear x가 0.1보다 작으면 토크(속도) 2배 증가 (정지 상태가 아닐 때만 적용)
            if (out_msg.linear.x != 0.0 && std::abs(current_imu_linear_x) < 0.1) {
                out_msg.linear.x *= 2.0;
            }

            cmd_pub_->publish(out_msg);

            cv::Mat ui_view;
            cv::resize(img_f, ui_view, cv::Size(), 0.5, 0.5);
            auto msg = cv_bridge::CvImage(mf->header, "bgr8", ui_view).toImageMsg();
            ui_image_pub_->publish(*msg);

            cv::imshow("Summer_Front_Vision", ui_view);
            cv::waitKey(1);

        } catch (cv::Exception& e) {
            RCLCPP_ERROR(this->get_logger(), "OpenCV Error: %s", e.what());
        }
    }

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr img_sub_;
    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr yolo_sub_;
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr nav_sub_;

    // ARM: q.py의 /yolo_detected_object 구독
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr arm_yolo_sub_;

    // ARM: 자동팔 시작 신호 publisher
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr arm_enable_pub_;

    // ARM: 자동팔 완료 신호 subscriber
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr arm_done_sub_;

    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr ui_image_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_pub_;

    geometry_msgs::msg::Twist current_nav;
    double current_imu_linear_x = 0.0;
    bool is_red_light = false;
    bool is_waiting = false;
    rclcpp::Time wait_start_time;

    // ARM: 차체 STOP 이후 ARM 시작까지 대기 시간
    static constexpr double ARM_CHASSIS_SETTLE_SEC = 3.0;

    // ARM: enable=True 중복 발행 방지
    bool arm_started = false;

    // ARM: Summer 실행 중 supply_box 재트리거 방지
    bool arm_mission_completed = false;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<SummerNode>());
    rclcpp::shutdown();
    return 0;
}
