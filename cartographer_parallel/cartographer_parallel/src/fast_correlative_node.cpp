#include <ros/ros.h>
#include <geometry_msgs/PoseArray.h>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/OccupancyGrid.h>
#include <sensor_msgs/LaserScan.h>
#include <std_msgs/String.h>
#include <visualization_msgs/MarkerArray.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cartographer_parallel/fast_matcher.h"

namespace cartographer_parallel_ros {
namespace {

geometry_msgs::Quaternion YawToQuat(const double yaw) {
  geometry_msgs::Quaternion q;
  q.x = 0.0;
  q.y = 0.0;
  q.z = std::sin(0.5 * yaw);
  q.w = std::cos(0.5 * yaw);
  return q;
}

geometry_msgs::Pose ToPose(const cartographer_parallel::CandOut& cand) {
  geometry_msgs::Pose pose;
  pose.position.x = cand.x;
  pose.position.y = cand.y;
  pose.position.z = 0.0;
  pose.orientation = YawToQuat(cand.yaw);
  return pose;
}

}  // namespace

class FastCorrelativeNode {
 public:
  FastCorrelativeNode() {
    ros::NodeHandle nh;
    ros::NodeHandle pnh("~");

    std::string map_yaml;
    std::string scan_topic;
    pnh.param<std::string>("map_yaml_file", map_yaml, std::string(""));
    pnh.param<std::string>("scan_topic", scan_topic, std::string("scan"));
    pnh.param<std::string>("map_frame", map_frame_, std::string("map"));
    pnh.param<std::string>("base_frame", base_frame_, std::string("base_link"));
    pnh.param("initial_x", pose_.x, 0.0);
    pnh.param("initial_y", pose_.y, 0.0);
    pnh.param("initial_yaw", pose_.yaw, 0.0);
    initial_pose_ = pose_;
    pnh.param("global_first_match", global_first_match_, true);
    pnh.param("global_every_n", global_every_n_, 0);
    pnh.param("publish_top_candidates", publish_top_candidates_, 100);
    pnh.param("initial_publish_top_candidates",
              initial_publish_top_candidates_, 500);
    pnh.param("initial_min_score", initial_min_score_, 0.90f);

    cartographer_parallel::MatchOpt opt;
    pnh.param("linear_search_window", opt.linear_window, 3.0);
    pnh.param("global_search_window", opt.global_window, 20.0);
    pnh.param("full_map_search", opt.full_map_search, false);
    pnh.param("angular_search_window", opt.angular_window, 0.35);
    pnh.param("angular_step", opt.angular_step, 0.05);
    pnh.param("branch_and_bound_depth", opt.branch_depth, 4);
    pnh.param("min_score", opt.min_score, 0.05f);
    pnh.param("max_candidates", opt.max_cand, 200);
    opt.max_cand = publish_top_candidates_;
    matcher_.SetOptions(opt);

    cartographer_parallel::MatchOpt initial_opt = opt;
    pnh.param("initial_global_search_window", initial_opt.global_window, 30.0);
    pnh.param("initial_full_map_search", initial_opt.full_map_search, true);
    pnh.param("initial_angular_search_window", initial_opt.angular_window, 3.15);
    pnh.param("initial_angular_step", initial_opt.angular_step, 0.035);
    pnh.param("initial_branch_and_bound_depth", initial_opt.branch_depth, 3);
    initial_opt.max_cand = initial_publish_top_candidates_;
    initial_matcher_.SetOptions(initial_opt);

    cartographer_parallel::MatchOpt initial_refine_opt = opt;
    pnh.param("initial_refine_linear_window",
              initial_refine_opt.linear_window, 1.0);
    pnh.param("initial_refine_angular_search_window",
              initial_refine_opt.angular_window, 0.08);
    pnh.param("initial_refine_angular_step",
              initial_refine_opt.angular_step, 0.005);
    pnh.param("initial_refine_branch_and_bound_depth",
              initial_refine_opt.branch_depth, 2);
    initial_refine_opt.full_map_search = false;
    initial_refine_opt.max_cand = initial_publish_top_candidates_;
    initial_refine_matcher_.SetOptions(initial_refine_opt);

    if (map_yaml.empty() || !matcher_.LoadMap(map_yaml) ||
        !initial_matcher_.LoadMap(map_yaml) ||
        !initial_refine_matcher_.LoadMap(map_yaml)) {
      ROS_FATAL_STREAM("Failed to load map yaml: " << map_yaml);
      throw std::runtime_error("failed to load map");
    }
    ROS_INFO_STREAM("Loaded map " << map_yaml << " (" << matcher_.width()
                                  << "x" << matcher_.height()
                                  << ", resolution " << matcher_.resolution()
                                  << ")");
    ROS_INFO_STREAM("Initial search: full_map="
                    << (initial_opt.full_map_search ? "true" : "false")
                    << ", global_window=" << initial_opt.global_window
                    << ", angular_window=" << initial_opt.angular_window
                    << ", angular_step=" << initial_opt.angular_step
                    << ", branch_depth=" << initial_opt.branch_depth
                    << ", accept_score=" << initial_min_score_);
    ROS_INFO_STREAM("Initial refine: linear_window="
                    << initial_refine_opt.linear_window
                    << ", angular_window=" << initial_refine_opt.angular_window
                    << ", angular_step=" << initial_refine_opt.angular_step
                    << ", branch_depth=" << initial_refine_opt.branch_depth);

    map_pub_ = nh_group.advertise<nav_msgs::OccupancyGrid>("map", 1, true);
    odom_pub_ = nh_group.advertise<nav_msgs::Odometry>("fast_correlative_odom", 10);
    candidate_pub_ = nh_group.advertise<geometry_msgs::PoseArray>(
        "fast_correlative_candidates", 10);
    marker_pub_ = nh_group.advertise<visualization_msgs::MarkerArray>(
        "fast_correlative_markers", 10);
    candidate_text_pub_ = nh_group.advertise<std_msgs::String>(
        "fast_correlative_candidate_text", 10);
    scan_sub_ = nh_group.subscribe(scan_topic, 1,
                                   &FastCorrelativeNode::scanCallback, this);
    ROS_INFO_STREAM("Subscribed to scan topic: "
                    << nh_group.resolveName(scan_topic, false));

    publishMap();
  }

 private:
  void publishMap() {
    nav_msgs::OccupancyGrid msg;
    msg.header.stamp = ros::Time::now();
    msg.header.frame_id = map_frame_;
    msg.info.resolution = matcher_.resolution();
    msg.info.width = matcher_.width();
    msg.info.height = matcher_.height();
    msg.info.origin.position.x = matcher_.origin_x();
    msg.info.origin.position.y = matcher_.origin_y();
    msg.info.origin.orientation.w = 1.0;
    msg.data.resize(msg.info.width * msg.info.height);

    const std::vector<unsigned char>& map = matcher_.map();
    const int w = matcher_.width();
    const int h = matcher_.height();
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const int src = y * w + x;
        const int dst = (h - 1 - y) * w + x;
        msg.data[dst] = static_cast<int8_t>(
            std::min(100, static_cast<int>(std::lround(map[src] * 100.0 / 255.0))));
      }
    }
    map_pub_.publish(msg);
  }

  void scanCallback(const sensor_msgs::LaserScanConstPtr& msg) {
    std::vector<float> xs;
    std::vector<float> ys;
    xs.reserve(msg->ranges.size());
    ys.reserve(msg->ranges.size());
    for (size_t i = 0; i < msg->ranges.size(); ++i) {
      const float r = msg->ranges[i];
      if (!std::isfinite(r) || r < msg->range_min || r > msg->range_max) continue;
      const float a = msg->angle_min + static_cast<float>(i) * msg->angle_increment;
      xs.push_back(r * std::cos(a));
      ys.push_back(r * std::sin(a));
    }
    if (xs.empty()) return;

    cartographer_parallel::MatchOut out;
    if (!has_pose_ && global_first_match_) {
      cartographer_parallel::MatchOut broad_out;
      const bool broad_ok =
          initial_matcher_.Match(xs, ys, initial_pose_, true, &broad_out);
      out = broad_out;
      if (broad_ok) {
        cartographer_parallel::MatchOut refine_out;
        if (initial_refine_matcher_.Match(xs, ys, broad_out.pose, false,
                                          &refine_out) &&
            refine_out.score > out.score) {
          out = refine_out;
        }
      }
      const bool locked = out.ok && out.score >= initial_min_score_;
      ++scan_count_;
      if (locked) {
        pose_ = out.pose;
        has_pose_ = true;
        ROS_INFO_STREAM("Initial pose locked at score " << out.score
                        << " pose=(" << pose_.x << ", " << pose_.y
                        << ", " << pose_.yaw << ")");
      } else {
        ROS_WARN_THROTTLE(1.0, "Waiting for high-confidence initial match. "
                               "score=%.3f broad=%.3f required=%.3f",
                          out.score, broad_out.score, initial_min_score_);
      }

      publishCandidates(msg->header.stamp, out.cand,
                        initial_publish_top_candidates_);
      if (locked) publishOdom(msg->header.stamp, out.pose, out.score);
      return;
    }

    const bool global =
        global_every_n_ > 0 && scan_count_ % global_every_n_ == 0;
    const bool ok = matcher_.Match(xs, ys, pose_, global, &out);
    ++scan_count_;
    if (!ok) {
      ROS_WARN_THROTTLE(1.0, "Fast correlative match below min_score.");
    } else {
      pose_ = out.pose;
      has_pose_ = true;
    }

    publishCandidates(msg->header.stamp, out.cand, publish_top_candidates_);
    if (ok) publishOdom(msg->header.stamp, out.pose, out.score);
  }

  void publishOdom(const ros::Time& stamp,
                   const cartographer_parallel::Pose2& pose,
                   const float score) {
    nav_msgs::Odometry odom;
    odom.header.stamp = stamp;
    odom.header.frame_id = map_frame_;
    odom.child_frame_id = base_frame_;
    odom.pose.pose.position.x = pose.x;
    odom.pose.pose.position.y = pose.y;
    odom.pose.pose.orientation = YawToQuat(pose.yaw);
    odom.pose.covariance[0] = std::max(1e-4, 1.0 - score);
    odom.pose.covariance[7] = std::max(1e-4, 1.0 - score);
    odom.pose.covariance[35] = std::max(1e-4, 1.0 - score);
    odom_pub_.publish(odom);
  }

  void publishCandidates(const ros::Time& stamp,
                         const std::vector<cartographer_parallel::CandOut>& cand,
                         const int limit) {
    geometry_msgs::PoseArray poses;
    poses.header.stamp = stamp;
    poses.header.frame_id = map_frame_;
    visualization_msgs::MarkerArray markers;

    const int n = std::min(limit, static_cast<int>(cand.size()));
    poses.poses.reserve(n);
    markers.markers.reserve(n);
    for (int i = 0; i < n; ++i) {
      poses.poses.push_back(ToPose(cand[i]));

      visualization_msgs::Marker marker;
      marker.header = poses.header;
      marker.ns = "fast_correlative_candidates";
      marker.id = i;
      marker.type = visualization_msgs::Marker::SPHERE;
      marker.action = visualization_msgs::Marker::ADD;
      marker.pose = poses.poses.back();
      marker.scale.x = 0.12;
      marker.scale.y = 0.12;
      marker.scale.z = 0.12;
      marker.color.r = 1.0f - cand[i].score;
      marker.color.g = cand[i].score;
      marker.color.b = 0.0f;
      marker.color.a = 0.8f;
      marker.lifetime = ros::Duration(0.5);
      markers.markers.push_back(marker);
    }
    candidate_pub_.publish(poses);
    marker_pub_.publish(markers);
    publishCandidateText(stamp, cand, n);
  }

  void publishCandidateText(const ros::Time& stamp,
                            const std::vector<cartographer_parallel::CandOut>& cand,
                            const int limit) {
    if (!candidate_text_pub_) return;
    std_msgs::String text;
    std::ostringstream out;
    out << "scan_time=" << stamp << " candidates=" << std::min(limit, static_cast<int>(cand.size())) << "\n";
    out << "id,x,y,yaw,score\n";
    const int n = std::min(limit, static_cast<int>(cand.size()));
    for (int i = 0; i < n; ++i) {
      out << i << "," << std::fixed << std::setprecision(6) << cand[i].x << ","
          << cand[i].y << "," << cand[i].yaw << ","
          << std::setprecision(4) << cand[i].score << "\n";
    }
    text.data = out.str();
    candidate_text_pub_.publish(text);
  }

  cartographer_parallel::FastMatcher matcher_;
  cartographer_parallel::FastMatcher initial_matcher_;
  cartographer_parallel::FastMatcher initial_refine_matcher_;
  cartographer_parallel::Pose2 pose_;
  cartographer_parallel::Pose2 initial_pose_;
  bool has_pose_ = false;
  bool global_first_match_ = true;
  int global_every_n_ = 0;
  int publish_top_candidates_ = 100;
  int initial_publish_top_candidates_ = 500;
  float initial_min_score_ = 0.90f;
  int scan_count_ = 0;
  std::string map_frame_;
  std::string base_frame_;

  ros::Subscriber scan_sub_;
  ros::Publisher map_pub_;
  ros::Publisher odom_pub_;
  ros::Publisher candidate_pub_;
  ros::Publisher marker_pub_;
  ros::Publisher candidate_text_pub_;
};

}  // namespace cartographer_parallel_ros

int main(int argc, char** argv) {
  ros::init(argc, argv, "fast_correlative_node");
  cartographer_parallel_ros::FastCorrelativeNode node;
  ros::spin();
  return 0;
}
