#ifndef _OPTRISIMAGERPLAYBACK_H_
#define _OPTRISIMAGERPLAYBACK_H_

#include "OptrisImager.h"

namespace optris_drivers
{

/**
 * @class OptrisImagerPlayback
 * @brief Node management class
 * @author Matteo Murtas (Laser Navigation)
 */
class OptrisImagerPlayback : public OptrisImager
{
public:

    OptrisImagerPlayback(evo::IRDevice* dev, evo::IRDeviceParams params) :
        OptrisImager(dev, params) {}

    void runSubscriber();

    void rawFrameCallback(const std_msgs::ByteMultiArray::ConstPtr& msg);

private:
    ros::Subscriber _raw_sub;
};

}

#endif // _OPTRISIMAGERPLAYBACK_H_
