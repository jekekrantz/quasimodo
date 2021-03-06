#include "ros/ros.h"
#include <dynamic_object_retrieval/dynamic_retrieval.h>
#include <dynamic_object_retrieval/visualize.h>
#include <object_3d_benchmark/benchmark_retrieval.h>
#include <object_3d_benchmark/benchmark_visualization.h>
#include "quasimodo_msgs/query_cloud.h"
#include "quasimodo_msgs/visualize_query.h"
#include "quasimodo_msgs/retrieval_query_result.h"
#include <pcl_ros/point_cloud.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <tf_conversions/tf_eigen.h>
#include <eigen_conversions/eigen_msg.h>

using namespace std;

using PointT = pcl::PointXYZRGB;
using CloudT = pcl::PointCloud<PointT>;
using HistT = pcl::Histogram<250>;

class visualization_server {
public:
    ros::NodeHandle n;
    ros::ServiceServer service;
    ros::Publisher image_pub;
    ros::Subscriber sub;
    string image_output;
    string topic_input;

    visualization_server(const std::string& name)
    {
        ros::NodeHandle pn("~");
        pn.param<std::string>("image_output", image_output, std::string("visualization_image"));
        pn.param<std::string>("topic_input", topic_input, std::string("/retrieval_result"));

        image_pub = n.advertise<sensor_msgs::Image>(image_output, 1);

        service = n.advertiseService(name, &visualization_server::service_callback, this);

        sub = n.subscribe(topic_input, 1, &visualization_server::callback, this);
    }

    //sensor_msgs/PointCloud2 cloud
    //sensor_msgs/Image image
    //sensor_msgs/Image depth
    //sensor_msgs/Image mask
    //sensor_msgs/CameraInfo camera
    //int32 number_query


    //sensor_msgs/Image image
    //sensor_msgs/CameraInfo camera
    //geometry_msgs/Transform room_transform
    sensor_msgs::Image vis_img_from_msgs(const quasimodo_msgs::retrieval_query& query,
                                         const quasimodo_msgs::retrieval_result& result)
    {
        cv_bridge::CvImagePtr cv_ptr;
        try {
            cv_ptr = cv_bridge::toCvCopy(query.image, sensor_msgs::image_encodings::BGR8);
        }
        catch (cv_bridge::Exception& e) {
            ROS_ERROR("cv_bridge exception: %s", e.what());
            exit(-1);
        }

        cv_bridge::CvImagePtr cv_mask_ptr;
        try {
            cv_mask_ptr = cv_bridge::toCvCopy(query.mask, sensor_msgs::image_encodings::MONO8);
        }
        catch (cv_bridge::Exception& e) {
            ROS_ERROR("cv_bridge exception: %s", e.what());
            exit(-1);
        }

        cv::Mat inverted_mask;
        cv::bitwise_not(cv_mask_ptr->image, inverted_mask);
        cv_ptr->image.setTo(cv::Scalar(255, 255, 255), inverted_mask);

        Eigen::Affine3d e;
        tf::transformMsgToEigen(query.room_transform, e);
        Eigen::Matrix4f T = e.matrix().cast<float>();
        T.col(3) << 0.0f, 0.0f, 0.0f, 1.0f;

        vector<CloudT::Ptr> retrieved_clouds;
        for (const sensor_msgs::PointCloud2& cloud : result.retrieved_clouds) {
            retrieved_clouds.push_back(CloudT::Ptr(new CloudT));
            pcl::fromROSMsg(cloud, *retrieved_clouds.back());
        }

        string query_label = "Query Image";
        vector<string> dummy_labels;
        for (int i = 0; i < result.retrieved_clouds.size(); ++i) {
            dummy_labels.push_back(string("result") + to_string(i));
        }
        cv::Mat visualization = benchmark_retrieval::make_visualization_image(cv_ptr->image, query_label, retrieved_clouds, dummy_labels, T);

        cv_bridge::CvImagePtr cv_pub_ptr(new cv_bridge::CvImage);
        cv_pub_ptr->image = visualization;
        cv_pub_ptr->encoding = "bgr8";

        return *cv_pub_ptr->toImageMsg();
    }

    bool service_callback(quasimodo_msgs::visualize_query::Request& req,
                          quasimodo_msgs::visualize_query::Response& res)
    {
        res.image = vis_img_from_msgs(req.query, req.result);

        image_pub.publish(res.image);

        return true;
    }

    void callback(const quasimodo_msgs::retrieval_query_result& res)
    {
        sensor_msgs::Image image = vis_img_from_msgs(res.query, res.result);

        image_pub.publish(image);
    }
};

int main(int argc, char** argv)
{
    ros::init(argc, argv, "quasimodo_visualization_service");

    visualization_server vs(ros::this_node::getName());

    ros::spin();

    return 0;
}
