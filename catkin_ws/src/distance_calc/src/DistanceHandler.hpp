//Class for handeling distances between objects and robot
#ifndef DISTANCE_HANDLER_HPP
#define DISTANCE_HANDLER_HPP

#include <ros/ros.h>
// PCL specific includes
#include <iostream>
#include <sensor_msgs/image_encodings.h>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <image_transport/image_transport.h>
#include <vector>
#include <std_msgs/Float64MultiArray.h>
#include <visualization_msgs/Marker.h>
#include <tf/transform_listener.h>



struct ObjectData{
	float minDistance;
	pcl::PointXYZ minPoint;
	int closestJoint;
	int inside;
	int outside;
	pcl::PointXYZ meanPoint;
	bool visible;
	clustering::pointArray cloud;
}; //index

struct SecurityDistances{
	float redDistance;
	float yellowDistance;
	float greenDistance;
	float trackingDistance;
};

struct ObjectDataList{
	std::vector<ObjectData> list;
	int closestObject; //index
	clustering::clusterArray clouds;
};

class DistanceHandler{
private:
	ros::Subscriber calibrationSubscriber, robotSubscriber, clusteringSubscriber;
	ros::Publisher linePublisher, pointCloudPublisher;
	clustering::clusterArray rawClusters;
	float insideRobotParameter;
	visualization_msgs::Marker line;
	ObjectDataList objects;
	ObjectDataList robot;
	std::vector<cv::Mat_ <float> > robotJoint;
	std::vector<float> sqrLengthBetweenJoints;
	std::vector<float> radiusOfCylinders;
	std::string jointNames[7] ;
	pcl::PointXYZ minPoint;
	int numberOfClusters;
	pcl::PointCloud<pcl::PointXYZ>::Ptr publishedPointCloud;
	SecurityDistances safetyZones;	
	    	tf::TransformListener listener;	

public:
	DistanceHandler(ros::NodeHandle& nh) :
		publishedPointCloud(new pcl::PointCloud<pcl::PointXYZ>())   {
		//calibrationSubscriber = nh.subscribe("calibration_data", 1, &DistanceHandler::calibrationCallback, this);
		//TODO add robotSubsriber
		clusteringSubscriber = nh.subscribe("cluster_vectors", 1,  &DistanceHandler::distanceCallback, this);
		linePublisher = nh.advertise<visualization_msgs::Marker>("closest_line", 0);
		pointCloudPublisher = nh.advertise<sensor_msgs::PointCloud2>("objects",0);
		objects.closestObject = -1;
		//only for testing
		cv::Mat test;
		test = (cv::Mat_<float>(3,1) <<0.0f,0.0f,2.0f);
		robotJoint.push_back(test);
		test =(cv::Mat_<float>(3,1) <<0.00f,0.0f,3.0f);
		robotJoint.push_back(test);
		//test =(cv::Mat_<float>(3,1) <<-1.00f,0.0f,100.0f);
		//robotJoint.push_back(test);
		sqrLengthBetweenJoints.push_back(1.0f);
		radiusOfCylinders.push_back(0.01f);
		insideRobotParameter = 0.1;
		
		//initializing the closest line.
		line.header.frame_id = "/camera_depth_optical_frame";
		line.header.stamp=ros::Time();
		line.id = 0;
		line.type = visualization_msgs::Marker::LINE_STRIP;
		line.action = visualization_msgs::Marker::ADD;

		//initialize security distances
		safetyZones.redDistance = 0.25f;
		safetyZones.yellowDistance = 0.5f;
		safetyZones.greenDistance = 0.75f;
		safetyZones.trackingDistance = 0.5f;

		//defining jointNames
		jointNames[0]="/link_s";
		jointNames[1]="/link_l";
		jointNames[2]="/link_e";
		jointNames[3]="/link_u";
		jointNames[4]="/link_r";
		jointNames[5]="/link_b";
		jointNames[6]="/link_t";
	}

	///Functions regarding calculation of distance
	void distanceCallback(clustering::clusterArray msg){ //This function is what's doing all the work.
		rawClusters = msg;
		//if(!updateRobotCoordinates())
		//	return; // skip this message
		newCloudsHandler(msg);
		setClosestObject();
		publishLine();
		publishPointCloud(objects.clouds);
		displayCurrentStatus();
	}

	void publishPointCloud(clustering::clusterArray& clusters){
		std::vector<pcl::PointXYZ> tempObjectVector;
		publishedPointCloud->clear();
		publishedPointCloud->header.frame_id= "/camera_depth_optical_frame";
		ROS_INFO("Creating object point cloud from %d clusters\n", clusters.ca.size());
		for(int i=0; i<clusters.ca.size();++i){
			for (int k= 0; k<clusters.ca[i].pa.size();++k){
				tempObjectVector.push_back(pcl::PointXYZ(clusters.ca[i].pa[k].x,clusters.ca[i].pa[k].y,clusters.ca[i].pa[k].z));
			}
		}
		publishedPointCloud->insert(publishedPointCloud->begin(),tempObjectVector.begin(),tempObjectVector.end());
		ROS_INFO("Publishing point cloud of size %dx%d to objects", publishedPointCloud->width, publishedPointCloud->height);
		pointCloudPublisher.publish(publishedPointCloud);
	}

	void publishLine(){
		if(objects.closestObject!= -1){
			cv::Mat joint = robotJoint[objects.list[objects.closestObject].closestJoint];
			pcl::PointXYZ objectPoint = objects.list[objects.closestObject].minPoint;
			float distance = objects.list[objects.closestObject].minDistance;			
			if (distance <=safetyZones.greenDistance){
				geometry_msgs::Point p;
				p.x=joint.at<float>(0,0);
				p.y=joint.at<float>(1,0);
				p.z= joint.at<float>(2,0);
				line.points.push_back(p);
				p.x=objectPoint.x;
				p.y=objectPoint.y;
				p.z=objectPoint.z;
				line.points.push_back(p);
				line.scale.x=0.1;				
				if (distance <= safetyZones.redDistance){
				line.color.a=1.0;
				line.color.g=0.0;
				line.color.b=0.0;
				line.color.r=1.0;
				} else if (distance <= safetyZones.yellowDistance){
				line.color.a=1.0;
				line.color.g=1.0;
				line.color.b=0.0;
				line.color.r=1.0;
				} else{
				line.color.a=1.0;
				line.color.g=1.0;
				line.color.b=0.0;
				line.color.r=0.0;
				}
			} 
			linePublisher.publish(line);
			line.points.clear();
		}
	}


	float compareToRobot(float x, float y, float z, cv::Mat& joint){
		float distance = powf(joint.at<float>(0,0)-x, 2)+powf(joint.at<float>(1,0)-y, 2)+powf(joint.at<float>(2,0)-z, 2);
		return distance;
	}

	// This is a big function.
	// Process new point cloud. Make matches with previous and checks what belongs to the robot.
	// Also calculates the distance between each point cloud, that doesn't belong to robot, and
	// closest joint.
	//It removes the clouds that belongs to the robot.
	void newCloudsHandler(clustering::clusterArray& rawClusterCloud){
		objects.clouds.ca.clear();
		// Sets every object/cloud from previous frame to invisible.
		for (int i = 0; i < objects.list.size(); i++){
			objects.list[i].visible = false;
		}
		//objects.list.clear();

		objects.closestObject = -1;
		robot.list.clear();

		for(int i=0; i<rawClusterCloud.ca.size(); i++)
		{
			ObjectData data;
			data.cloud.pa = rawClusterCloud.ca[i].pa;
			data.visible = true;
			data.minDistance = 1000.0f;
			data.closestJoint = -1;
			++numberOfClusters;
			for(int j=0; j<rawClusterCloud.ca[i].pa.size(); j++)
			{
				pointInsideRobot(rawClusterCloud.ca[i].pa[j], data);
				data.meanPoint.x += rawClusterCloud.ca[i].pa[j].x;
				data.meanPoint.y += rawClusterCloud.ca[i].pa[j].y;
				data.meanPoint.z += rawClusterCloud.ca[i].pa[j].z;
			}
			// Calculate the mean point for each cluster
			data.meanPoint.x = data.meanPoint.x / rawClusterCloud.ca[i].pa.size();
			data.meanPoint.y = data.meanPoint.y / rawClusterCloud.ca[i].pa.size();
			data.meanPoint.z = data.meanPoint.z / rawClusterCloud.ca[i].pa.size();
			// Finding the closest matching cloud from previous frame(similar mean value)
			float nearestCloud = 0.5f; //This is the largest distance that a cloud could move between two frames and be recognized as the same.
			float cloudDistance;
			int matchingCloud;
			bool cloudFound = false;
			for (int k = 0; k < objects.list.size(); k++){
				cloudDistance = powf(objects.list[k].meanPoint.x - data.meanPoint.x, 2) +
								powf(objects.list[k].meanPoint.y - data.meanPoint.y, 2) +
								powf(objects.list[k].meanPoint.z - data.meanPoint.z, 2);
				if (cloudDistance < nearestCloud){
					matchingCloud = k;
					nearestCloud = cloudDistance;
					cloudFound = true;
				}
			}
			// Update list of object
			if(cloudFound){
				objects.list[matchingCloud] = data;

				std::cout << "Cloud is tracked! \n" << std::endl;
			}

			// if statement checks if the object belongs to the robot or not
			if((data.inside+data.outside != 0)&&!(data.inside/(data.inside+data.outside) > insideRobotParameter)){
				//objects.clouds.ca.push_back(rawClusterCloud.ca[i]);
				if(!cloudFound){
					objects.list.push_back(data);
					std::cout << "New cloud! \n" << std::endl;
				}
			} else{// This is a robot object
					robot.clouds.ca.push_back(rawClusterCloud.ca[i]);
					robot.list.push_back(data);
			}

		}

		// Check if any of the invisible objects could have disappeared out of the frame.
		for (int i = 0; i < objects.list.size(); i++){
			if (objects.list[i].visible == false && objects.list[i].minDistance > safetyZones.trackingDistance){
				std::cout << "Remove object from list" << std::endl;
				objects.list.erase (objects.list.begin() + i);// Erases the object list at position i.
			}
		}
		for(int i = 0; i < objects.list.size(); i++){
			objects.clouds.ca.push_back(objects.list[i].cloud);
		}
	}

	void pointInsideRobot(clustering::point inputPoint, ObjectData& data){
		for(int i=0; i<robotJoint.size()-1; i++){
			pointInsideCylinder(robotJoint[i],robotJoint[i+1],sqrLengthBetweenJoints[i],radiusOfCylinders[i],inputPoint,data, i);
		}
	}

	//Algorithm copied from http://www.flipcode.com/archives/Fast_Point-In-Cylinder_Test.shtml
	void pointInsideCylinder( cv::Mat& pt1, cv::Mat& pt2, float lengthsq, float radius_sq,
			const clustering::point& testpt, ObjectData& data, int jointIndex)
	{
		float dx, dy, dz;	// vector d  from line segment point 1 to point 2
		float pdx, pdy, pdz;	// vector pd from point 1 to test point
		float dot, dsq;

		dx = pt2.at<float>(0,0) - pt1.at<float>(0,0);	// translate so pt1 is origin.  Make vector from
		dy = pt2.at<float>(1,0) - pt1.at<float>(1,0);     // pt1 to pt2.  Need for this is easily eliminated
		dz = pt2.at<float>(2,0) - pt1.at<float>(2,0);

		pdx = testpt.x - pt1.at<float>(0,0);		// vector from pt1 to test point.
		pdy = testpt.y - pt1.at<float>(1,0);
		pdz = testpt.z - pt1.at<float>(2,0);


		dot = pdx * dx + pdy * dy + pdz * dz;

		float dist = compareToRobot(testpt.x, testpt.y, testpt.z, pt1); //Will only calculate distance to the six first joints
		if (dist < data.minDistance)
		{
			data.minDistance = dist;
			data.minPoint.x = testpt.x;
			data.minPoint.y = testpt.y;
			data.minPoint.z = testpt.z;
			data.closestJoint= jointIndex;
		}

		if( dot < 0.0f || dot > lengthsq )
		{
			++data.outside;
		}
		else
		{
			dsq = (pdx*pdx + pdy*pdy + pdz*pdz) - dot*dot/lengthsq;

			if( dsq > radius_sq )
			{
				++data.inside;
			}
			else
			{
				++data.outside;		// return true if inside cylinder
			}
		}
	}

	void setClosestObject(){
		int minDistance = 20000.0f;
		for (int i = 0;i<objects.list.size();++i){
			if (objects.list[i].minDistance < minDistance){
				minDistance = objects.list[i].minDistance;
				objects.closestObject= i;
			}
		}
	}

	bool updateRobotCoordinates(){
		robotJoint.clear();
		sqrLengthBetweenJoints.clear();
		radiusOfCylinders.clear();
		tf::StampedTransform transform;
		cv::Mat oldJoint, joint;

		try {
			for(int i=0;i<7;++i){
				listener.lookupTransform("/camera_depth_optical_frame", jointNames[i],
				ros::Time(0), transform);	
				float x =transform.getOrigin().x();
				float y =transform.getOrigin().y();
				float z =transform.getOrigin().z();

				joint =(cv::Mat_<float>(3,1) <<x,y,z);
				robotJoint.push_back(joint);
				if(i > 0){
					double sqrLength = pow(cv::norm(joint-oldJoint),2); // TODO: Check type
					sqrLengthBetweenJoints.push_back(sqrLength);
					radiusOfCylinders.push_back(0.04f);
				}
				oldJoint = joint;
			}
			ROS_INFO("Succesful robot position update\n");
			return true;
		}
		catch(tf::TransformException ex) {
			ROS_INFO("Failed with getting transform: %s\n", ex.what());
			return false;	
		}
				
	}

	void displayCurrentStatus(){
		std::cout << "There are " << objects.list.size() << " objects visible\n";
		std::cout << "There are " << robot.list.size() << " clouds considered robot\n";
		if (objects.list.size() != 0){
			std::cout << "The closest object is " << objects.list[objects.closestObject].minDistance << " m away from joint number ";
			std::cout  << objects.list[objects.closestObject].closestJoint << "\n";
		}
	}
};


#endif