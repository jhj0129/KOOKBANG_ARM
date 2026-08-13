import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
import cv2
from cv_bridge import CvBridge
import message_filters

class ImageStitcher(Node):
    def __init__(self):
        super().__init__('image_stitcher')
        self.bridge = CvBridge()

        self.sub_l = message_filters.Subscriber(self, Image, '/camera_left/color/image_raw')
        self.sub_f = message_filters.Subscriber(self, Image, '/camera_front/color/image_raw')
        self.sub_r = message_filters.Subscriber(self, Image, '/camera_right/color/image_raw')

        self.ts = message_filters.ApproximateTimeSynchronizer([self.sub_l, self.sub_f, self.sub_r], 10, 0.1)
        self.ts.registerCallback(self.callback)

        self.pub = self.create_publisher(Image, '/camera/combined', 10)

    def callback(self, ml, mf, mr):
        cv_l = self.bridge.imgmsg_to_cv2(ml, 'bgr8')
        cv_f = self.bridge.imgmsg_to_cv2(mf, 'bgr8')
        cv_r = self.bridge.imgmsg_to_cv2(mr, 'bgr8')

        combined = cv2.hconcat([cv_l, cv_f, cv_r])
        self.pub.publish(self.bridge.cv2_to_imgmsg(combined, 'bgr8'))

def main():
    rclpy.init()
    rclpy.spin(ImageStitcher())
    rclpy.shutdown()
