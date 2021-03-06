/*
 * timesync_server.cpp
 *
 *  Created on: Jun 23, 2015
 *      Author: ffontana
 */

#include "ros/ros.h"
#include "timesync_tester/TimeMsg.h"
#include "timesync_tester/ResultMsg.h"

#include <thread>
#include <memory>
#include <atomic>

class TimeSyncServer
{
public:
  TimeSyncServer();
  ~TimeSyncServer();
  void msgCallback(const timesync_tester::TimeMsg::ConstPtr &msg);

  ros::NodeHandle nh_;
  ros::NodeHandle pnh_;


  ros::Publisher ping_pub_;
  ros::Subscriber pong_sub_;
  ros::Publisher debug_pub_;


  std::list<timesync_tester::TimeMsg> msg_buffer_;

  std::unique_ptr<std::thread> spin_thread_;

  std::atomic_bool thread_running_;
  int number_of_measurements_;

  void spinThread();
  void recordData();
  void evalData();

  double calculateMean(const std::list<double> &input);
  double calculateVariane(const std::list<double> &input);

};

TimeSyncServer::TimeSyncServer() :
    nh_(), thread_running_(false), pnh_("~")
{
  ping_pub_ = nh_.advertise<timesync_tester::TimeMsg>("ping", 1);
  pong_sub_ = nh_.subscribe("pong", 1, &TimeSyncServer::msgCallback, this, ros::TransportHints().tcpNoDelay());
  debug_pub_ = nh_.advertise<timesync_tester::ResultMsg>("results", 1);
  thread_running_ = true;
  spin_thread_ = std::unique_ptr < std::thread > (new std::thread(&TimeSyncServer::spinThread, this));
  pnh_.param<int>("number_of_measurements", number_of_measurements_, 10);

}

TimeSyncServer::~TimeSyncServer()
{
  if (thread_running_ && spin_thread_)
  {
    thread_running_ = false;
    spin_thread_->join();
  }
}

double TimeSyncServer::calculateMean(const std::list<double> &input)
{
  if (input.size() == 0)
    return 0.0;

  double sum = 0;

  for (auto it = input.begin(); it != input.end(); it++)
  {
    sum += *it;
  }
  return (sum / (double)input.size());
}

double TimeSyncServer::calculateVariane(const std::list<double> &input)
{
  if (input.size() == 0)
    return 0.0;

  double mean = calculateMean(input);
  double temp = 0;

  for (auto it = input.begin(); it != input.end(); it++)
  {
    temp += (*it - mean) * (*it - mean);
  }
  return temp / ((double)input.size()-1.0);
}

void TimeSyncServer::spinThread()
{
  ros::spin();
}

void TimeSyncServer::msgCallback(const timesync_tester::TimeMsg::ConstPtr &msg)
{
  ros::Time recv_stamp = ros::Time::now();
  timesync_tester::TimeMsg local_msg(*msg);
  local_msg.received_stamp =recv_stamp;
  msg_buffer_.push_back(local_msg);
  timesync_tester::ResultMsg result_msg;
  result_msg.seqence_number = msg->seqence_number;

  ros::Duration pong_duration = local_msg.received_stamp - msg->outgoing_stamp;

  ros::Time estimated_receive_time = local_msg.received_stamp - ros::Duration(pong_duration.toSec() / 2.0);

  std::cout << "local_msg.outgoing_stamp: " << local_msg.outgoing_stamp << std::endl;
  std::cout << "local_msg.received_stamp: " << local_msg.received_stamp << std::endl;
  std::cout << "local_msg.pong_stamp:     " << local_msg.pong_stamp << std::endl;
  std::cout << "estimated_receive_time:   " << estimated_receive_time << std::endl;

  result_msg.offset = (msg->pong_stamp - estimated_receive_time).toSec() * 1000.0;
  result_msg.ping_pong_time = pong_duration.toSec() * 1000.0;

  printf("pingpong:\t%.5fms\n", result_msg.ping_pong_time);
  printf("offset:\t\t%.5fms\n", result_msg.offset);


    debug_pub_.publish(result_msg);

}

void TimeSyncServer::recordData()
{
  ros::Duration(1.0).sleep();

  int seqence_number;

//  while (seqence_number < number_of_measurements_ && ros::ok())
  while (true && ros::ok())
  {
    timesync_tester::TimeMsg msg;
    msg.seqence_number = seqence_number++;
    msg.outgoing_stamp = ros::Time::now();
    ping_pub_.publish(msg);
    ros::Duration(1.0).sleep();
  }
}

void TimeSyncServer::evalData()
{
  ROS_INFO("received %d messages", (int )msg_buffer_.size());

  std::list<double> ping_pong_times;
  std::list<double> slave_offsets;

  for (auto it = msg_buffer_.begin(); it != msg_buffer_.end(); it++)
  {
    double ping_pong_time = (it->received_stamp - it->outgoing_stamp).toSec();
    double estimated_receive_time = (it->received_stamp.toSec() + it->outgoing_stamp.toSec()) / 2.0;
    double slave_offset = it->pong_stamp.toSec() - estimated_receive_time;
    printf("%d [ %.2fms, %.2fms ]\n", it->seqence_number, ping_pong_time * 1000.0, slave_offset * 1000.0);
    ping_pong_times.push_back(ping_pong_time);
    slave_offsets.push_back(slave_offset);
  }
  printf("ping times [ %.2f ms, %e ms ]\n", calculateMean(ping_pong_times) * 1000.0, calculateVariane(ping_pong_times) * 1000.0);
  printf("slave offset [ %.2f ms, %e ms ]\n", calculateMean(slave_offsets) * 1000.0, calculateVariane(slave_offsets) * 1000.0);
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "timesync_server");
  TimeSyncServer server;
  server.recordData();
  server.evalData();

  return 0;
}

