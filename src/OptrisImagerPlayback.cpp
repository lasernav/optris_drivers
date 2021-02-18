#include "OptrisImagerPlayback.h"

namespace optris_drivers
{

void OptrisImagerPlayback::runSubscriber()
{
    ros::NodeHandle n;

    _raw_sub = n.subscribe("raw", 1, &OptrisImagerPlayback::rawFrameCallback, this);

    ros::spin();
}

void OptrisImagerPlayback::rawFrameCallback(const std_msgs::ByteMultiArray::ConstPtr& msg)
{
    _imager.process((unsigned char *) msg->data.data());
}


}
