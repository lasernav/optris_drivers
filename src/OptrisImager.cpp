#include "OptrisImager.h"

namespace optris_drivers
{

OptrisImager::OptrisImager(evo::IRDevice* dev, evo::IRDeviceParams params)
{
  _imager.init(&params, dev->getFrequency(), dev->getWidth(), dev->getHeight(), dev->controlledViaHID());
  _imager.setClient(this);

  _bufferSize = dev->getRawBufferSize();
  _bufferRaw = new unsigned char[dev->getRawBufferSize()];

  ros::NodeHandle n;

  ros::NodeHandle nh("~");
  //emissivity settings
  if (nh.hasParam("emissivity"))
  {
    float emissivity = 1.0;
    float transmissivity = 0.0;
    float ambientTemperature = -9999.0;
    nh.param("emissivity", emissivity, emissivity);
    nh.param("transmissivity", transmissivity, transmissivity);
    nh.param("ambientTemperature", ambientTemperature, ambientTemperature);
    ROS_INFO("Radiation param: e=%f t=%f Tamb=%f", emissivity, transmissivity, ambientTemperature);
    _imager.setRadiationParameters(emissivity, transmissivity, ambientTemperature);
  }

  if (nh.hasParam("flip"))
  {
    nh.param("flip", _flip, false);
    ROS_INFO("Filp image");
  } else {
    _flip = false;
  }

  _raw_pub =  n.advertise<std_msgs::ByteMultiArray>("raw", 1);
  _raw_data.data.resize(dev->getRawBufferSize());

  image_transport::ImageTransport it(n);

  _thermal_pub = it.advertise("thermal_image", 1);
  _thermal_image.header.frame_id = "thermal_image";
  _thermal_image.height          = _imager.getHeight();
  _thermal_image.width           = _imager.getWidth();
  _thermal_image.encoding        = "mono16";
  _thermal_image.step            = _thermal_image.width * 2;
  _thermal_image.data.resize(_thermal_image.height * _thermal_image.step);


  if(_imager.hasBispectralTechnology())
  {
    _visible_pub = it.advertise("visible_image", 1);
    _visible_image.header.frame_id = "visible_image";
    _visible_image.height          = _imager.getVisibleHeight();
    _visible_image.width           = _imager.getVisibleWidth();
    _visible_image.encoding        = "yuv422";
    _visible_image.step            = _visible_image.width * 2;
    _visible_image.data.resize(_visible_image.height * _visible_image.step);
  }

  // advertise the camera internal timer
  _timer_pub= n.advertise<sensor_msgs::TimeReference>("optris_timer", 1 );
  _device_timer.header.frame_id=_thermal_image.header.frame_id;

  _sAuto  = n.advertiseService("auto_flag",  &OptrisImager::onAutoFlag, this);
  _sForce = n.advertiseService("force_flag", &OptrisImager::onForceFlag, this);
  _sRadParam  = n.advertiseService("radiation_parameters", &OptrisImager::onSetRadiationParameters, this);
  _sTemp  = n.advertiseService("temperature_range", &OptrisImager::onSetTemperatureRange, this);

  // advertise all of the camera's temperatures in a single custom message
  _temp_pub = n.advertise<Temperature> ("internal_temperature", 1);
  _internal_temperature.header.frame_id=_thermal_image.header.frame_id;

  _flag_pub = n.advertise<Flag>("flag_state", 1);

  _img_cnt = 0;

  _dev = dev;
}

OptrisImager::~OptrisImager()
{
  delete [] _bufferRaw;
}

void OptrisImager::run()
{
  _dev->startStreaming();

  ros::NodeHandle n;
  ros::Timer timer = n.createTimer(ros::Duration(1.0/_imager.getMaxFramerate()), &OptrisImager::onTimer, this);
  ros::spin();

  _dev->stopStreaming();
}

void OptrisImager::onTimer(const ros::TimerEvent& event)
{
  int retval = _dev->getFrame(_bufferRaw);
  if(retval==evo::IRIMAGER_SUCCESS)
  {
    std::copy(_bufferRaw, _bufferRaw + _bufferSize, _raw_data.data.begin());
    _raw_pub.publish(_raw_data);

    _imager.process(_bufferRaw);
  }
  if(retval==evo::IRIMAGER_DISCONNECTED)
  {
    ros::shutdown();
  }
}

void OptrisImager::onThermalFrame(unsigned short* image, unsigned int w, unsigned int h, evo::IRFrameMetadata meta, void* arg)
{
  unsigned short * dest = (unsigned short *) &_thermal_image.data[0];
  if (_flip) {
    for (unsigned int r = 0; r < h; r++) {
      for (unsigned int c = 0; c < w; c++) {
        memcpy(dest + w * (h - 1 - r) + (w - 1 - c), image + w * r + c, sizeof(*image));
      }
    }
  } else {
    memcpy(dest, image, w * h * sizeof(*image));
  }

  _thermal_image.header.seq = ++_img_cnt;
  _thermal_image.header.stamp = ros::Time::now();
  _thermal_pub.publish(_thermal_image);

  _device_timer.header.seq = _thermal_image.header.seq;
  _device_timer.header.stamp = _thermal_image.header.stamp;
  _device_timer.time_ref.fromNSec(meta.timestamp);

  _internal_temperature.header.seq       = _thermal_image.header.seq;
  _internal_temperature.header.stamp     = _thermal_image.header.stamp;

  _internal_temperature.temperature_flag = _imager.getTempFlag();
  _internal_temperature.temperature_box  = _imager.getTempBox();
  _internal_temperature.temperature_chip = _imager.getTempChip();

  _internal_temperature.header.frame_id  = _thermal_image.header.frame_id;

  _timer_pub.publish(_device_timer);
  _temp_pub.publish(_internal_temperature);
}

void OptrisImager::onVisibleFrame(unsigned char* image, unsigned int w, unsigned int h, evo::IRFrameMetadata meta, void* arg)
{
  if(_visible_pub.getNumSubscribers()==0) return;

  unsigned short * src = (unsigned short *) image;
  unsigned short * dest = (unsigned short *) &_visible_image.data[0];
  if (_flip) {
    for (unsigned int r = 0; r < h; r++) {
      for (unsigned int c = 0; c < w; c++) {
        memcpy(dest + w * (h - 1 - r) + (w - 1 - c), src +  (w * r + c), sizeof(*src));
      }
    }
  } else {
    memcpy(dest, src, w * h * sizeof(*src));
  }


  _visible_image.header.seq   = _img_cnt;
  _visible_image.header.stamp = ros::Time::now();
  _visible_pub.publish(_visible_image);
}

void OptrisImager::onFlagStateChange(evo::EnumFlagState flagstate, void* arg)
{
  optris_drivers::Flag flag;
  flag.flag_state      = flagstate;
  flag.header.frame_id = _thermal_image.header.frame_id;
  flag.header.stamp    = _thermal_image.header.stamp;
  _flag_pub.publish(flag);
}

void OptrisImager::onProcessExit(void* arg)
{

}

bool OptrisImager::onAutoFlag(AutoFlag::Request &req, AutoFlag::Response &res)
{
  _imager.setAutoFlag(req.autoFlag);
  res.isAutoFlagActive = _imager.getAutoFlag();
  return true;
}

bool OptrisImager::onForceFlag(std_srvs::Empty::Request& req, std_srvs::Empty::Response& res)
{
  _imager.forceFlagEvent();
  return true;
}

bool OptrisImager::onSetRadiationParameters(RadiationParameters::Request &req, RadiationParameters::Response &res)
{
  setRadiationParameters(req.emissivity, req.transmissivity, req.ambientTemperature);
  res.success = true;

  return true;
}

void OptrisImager::setRadiationParameters(double emissivity, double transmissivity, double ambientTemperature) {
  _imager.setRadiationParameters(emissivity, transmissivity, ambientTemperature);
}

bool OptrisImager::onSetTemperatureRange(TemperatureRange::Request &req, TemperatureRange::Response &res)
{
  bool validParam = _imager.setTempRange(req.temperatureRangeMin, req.temperatureRangeMax);

  if(validParam)
  {
    _imager.forceFlagEvent(1000.f);
  }

  res.success = validParam;

  return true;
}

}
