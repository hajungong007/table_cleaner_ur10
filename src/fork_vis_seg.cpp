#include <ros/ros.h>
#include <ros/callback_queue.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/Image.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <std_msgs/String.h>
#include <boost/bind.hpp>
// PCL specific includes
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/conversions.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/console/parse.h>
#include <pcl/common/transforms.h>
#include <pcl/filters/passthrough.h>
#include <pcl/sample_consensus/ransac.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/sample_consensus/sac_model_normal_plane.h>
#include <pcl/sample_consensus/sac_model_plane.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/ModelCoefficients.h>
#include <pcl/features/integral_image_normal.h>
#include <pcl/io/pcd_io.h>
#include <pcl/surface/concave_hull.h>
#include <pcl_ros/transforms.h>
#include <pcl_ros/point_cloud.h>
#include "std_msgs/MultiArrayLayout.h"
#include "std_msgs/MultiArrayDimension.h"
#include "std_msgs/Int32MultiArray.h"
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/PointCloud2.h>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <iostream>
#include <cmath> 

using namespace sensor_msgs;
using namespace message_filters;
using namespace pcl;
using namespace cv;


ros::Publisher pcl_pub;
ros::Publisher turn_pub;


/// Global variables
RNG rng(12345);
double min_range_;
double max_range_;
int CONTOUR_LIMIT = 80;

int mask_coeff[4];


// Global calibration values from Kinect (depth)
float fx_rgb = 542.874146;
float fy_rgb = 546.720581;
float cx_rgb = 320.693510;
float cy_rgb = 264.239638;
float fx_d = 605.119568;
float fy_d = 605.215271;
float cx_d = 298.887004;
float cy_d = 264.589174;


void callback(const sensor_msgs::ImageConstPtr& mask_sub, const sensor_msgs::ImageConstPtr& img_sub, const sensor_msgs::ImageConstPtr& depth_sub)
{

  cv_bridge::CvImagePtr mask_ptr;
  cv_bridge::CvImagePtr img_ptr;
  cv_bridge::CvImagePtr depth_ptr;

  try
  {
    mask_ptr = cv_bridge::toCvCopy(mask_sub, sensor_msgs::image_encodings::MONO8);
    img_ptr = cv_bridge::toCvCopy(img_sub, sensor_msgs::image_encodings::BGR8);
    depth_ptr = cv_bridge::toCvCopy(depth_sub, "32FC1");
  }
  catch (cv_bridge::Exception& e)
  {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    return;
  }

  Mat mask_src, img_src;
  mask_src = mask_ptr->image;
  img_src = img_ptr->image;


 // Extract ROI
    Mat roi, roi_img;
    roi_img = img_src( Rect(mask_coeff[0], mask_coeff[1],mask_coeff[2], mask_coeff[3]) );
    //roi = clustered( Rect(mask_coeff[0], mask_coeff[1],mask_coeff[2], mask_coeff[3]) );


// **************** Kmeans color clustering *********************
   
    
    Mat samples(roi_img.rows * roi_img.cols, 3, CV_32F);
    for( int y = 0; y < roi_img.rows; y++ )
      for( int x = 0; x < roi_img.cols; x++ )
        for( int z = 0; z < 3; z++)
          samples.at<float>(y + x*roi_img.rows, z) = roi_img.at<Vec3b>(y,x)[z];


    int clusterCount = 2;
    Mat labels;
    int attempts = 3;
    Mat centers;
    kmeans(samples, clusterCount, labels, TermCriteria(CV_TERMCRIT_ITER|CV_TERMCRIT_EPS, 3, 1), attempts, KMEANS_PP_CENTERS, centers );

    Mat clustered( roi_img.size(), roi_img.type() );
    for( int y = 0; y < roi_img.rows; y++ )
      for( int x = 0; x < roi_img.cols; x++ )
      { 
        int cluster_idx = labels.at<int>(y + x*roi_img.rows,0);

        clustered.at<Vec3b>(y,x)[0] = centers.at<float>(cluster_idx, 0);
        clustered.at<Vec3b>(y,x)[1] = centers.at<float>(cluster_idx, 1);
        clustered.at<Vec3b>(y,x)[2] = centers.at<float>(cluster_idx, 2);
        /*
	if (cluster_idx==1)
        {     
          bwImage.at<uchar>(y,x) = 0;
        }else
        {
          bwImage.at<uchar>(y,x) = 255;
        }
        */
      }

  // Convert Roi to binary
    cv::Mat bwImage(roi_img.size(), CV_8U);
    cvtColor(clustered,bwImage,CV_RGB2GRAY);
    cv::threshold(bwImage, bwImage, 128, 255, CV_THRESH_BINARY | CV_THRESH_OTSU);
 

/*
  // Apply canny edge detector
    Canny( bwImage, bwImage, 100, 300, 3 );
    cv::Point vertices[4];

    // Find contours
    vector<vector<Point> > contours;
    vector<Vec4i> hierarchy;
    findContours(bwImage, contours, hierarchy, RETR_TREE, CV_CHAIN_APPROX_SIMPLE, Point (0, 0) );

    // Threshold contour size
    for (vector<vector<Point> >::iterator contours_it = contours.begin(); contours_it!=contours.end(); )
    {
      if (contours_it->size()<CONTOUR_LIMIT)
          contours_it=contours.erase(contours_it);
      else
          ++contours_it;
    }

    //Black_roi
    Mat black_roi = Mat::zeros(bwImage.size(), CV_8U);
    
    // Draw contour
    for( size_t i = 0; i< contours.size(); i++ )
      {
      drawContours( black_roi, contours, i, cv::Scalar(255,255,255), -1, 8, hierarchy, 0, Point() );     
      }  
*/

 
// Put bwImage back into full matrix 
int erosion_size=5;
Mat element_erode = getStructuringElement( MORPH_ELLIPSE,
                                       Size( 2*erosion_size + 1, 2*erosion_size+1 ),
                                       Point( erosion_size, erosion_size ) );
int dilate_size=2;
Mat element_dilate = getStructuringElement( MORPH_ELLIPSE,
                                       Size( 2*dilate_size + 1, 2*dilate_size+1 ),
                                       Point( dilate_size, dilate_size ) );
 erode( bwImage, bwImage,element_erode);
 dilate( bwImage, bwImage,element_dilate);

  Mat final_contours = Mat::zeros(480, 640, CV_8UC1);
  bwImage.copyTo(final_contours(Rect(mask_coeff[0], mask_coeff[1],mask_coeff[2], mask_coeff[3])));

/*
 // Update GUI Window
    cv::imshow("ROI image", roi_img);
    cv::imshow("Clustered", clustered);
    cv::imshow("bwImage", bwImage);
    cv::imshow("final_contours", final_contours);
    cv::waitKey(1);
*/

   // ***********************************************************************
   // ************************** DEPTH IMAGE ********************************
   // ***********************************************************************

    Mat depth_src, masked_src;
    depth_src = depth_ptr->image;
    
    //Run mask on depth map
    depth_src.copyTo(masked_src, final_contours);
    
    Mat depthImage;  
    depthImage = masked_src;

 /*    
    // Update GUI Window
    cv::imshow("Depth viewer", masked_src);
    cv::waitKey(1);
*/

    // Generate Pointcloud 
    pcl::PointCloud<pcl::PointXYZ>::Ptr outputPointcloud (new pcl::PointCloud <pcl::PointXYZ>); 
    float rgbFocalInvertedX = 1/fx_rgb;	// 1/fx
    float rgbFocalInvertedY = 1/fy_rgb;	// 1/fy

    pcl::PointXYZ newPoint;
    for (int i=0;i<depthImage.rows;i++)
    {
       for (int j=0;j<depthImage.cols;j++)
       {
  	  float depthValue = depthImage.at<float>(i,j);
          if (depthValue != 0)                // if depthValue is not NaN
	  {
            // Find 3D position respect to rgb frame:
	    newPoint.z = depthValue/1000;
	    newPoint.x = (j - cx_rgb) * newPoint.z * rgbFocalInvertedX;
	    newPoint.y = (i - cy_rgb) * newPoint.z * rgbFocalInvertedY;
	    outputPointcloud->push_back(newPoint);
	  }
       }
    }
  
/*
    // Visualize pointcloud
    viewer.addCoordinateSystem();
    viewer.addPointCloud (outputPointcloud, "scene_cloud");
    viewer.spinOnce();
    viewer.removePointCloud("scene_cloud");
*/

    // Publish the data
    outputPointcloud->header.frame_id = "camera_rgb_optical_frame";
    ros::Time time_st = ros::Time::now ();
    outputPointcloud->header.stamp = time_st.toNSec()/1e3;
    pcl_pub.publish (outputPointcloud);

}


void mask_callback (const std_msgs::Int32MultiArray::ConstPtr& array)
{

   int i = 0;
   // print all the remaining numbers
   for(std::vector<int>::const_iterator it = array->data.begin(); it != array->data.end(); ++it)
   {
     mask_coeff[i]=*it;
     i++;
   }

return;
}


int main(int argc, char** argv)
{
  ros::init(argc, argv, "fork_vis_seg");

/*
  cv::namedWindow("ROI image");
  cv::namedWindow("Clustered");
  cv::namedWindow("bwImage");
  cv::namedWindow("final_bw");
*/
  
  ros::NodeHandle nh;

  ros::Subscriber mask_coeff = nh.subscribe("/fork/mask_coeff", 1, mask_callback);

  message_filters::Subscriber<sensor_msgs::Image> mask_sub(nh, "/cropped/fork_mask", 1);
  message_filters::Subscriber<sensor_msgs::Image> img_sub(nh, "/camera/rgb/image_rect_color", 1);
  message_filters::Subscriber<sensor_msgs::Image> depth_sub(nh, "camera/depth_registered/sw_registered/image_rect_raw", 1);
//message_filters::Subscriber<sensor_msgs::Image> depth_sub(nh, "/fork/fork_rect_depth", 1);


  pcl_pub = nh.advertise<sensor_msgs::PointCloud2> ("/fork/vis_seg/pcl", 1);
  

  typedef sync_policies::ApproximateTime<sensor_msgs::Image, sensor_msgs::Image, sensor_msgs::Image> MySyncPolicy;
  // ApproximateTime takes a queue size as its constructor argument, hence MySyncPolicy(10)
  Synchronizer<MySyncPolicy> sync(MySyncPolicy(7), mask_sub, img_sub, depth_sub);
  sync.registerCallback(boost::bind(&callback, _1, _2, _3));


  ros::spin ();
  return 0;

}
