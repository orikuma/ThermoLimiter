// -*- C++ -*-
/*!
 * @file  ThermoLimiter.cpp
 * @brief null component
 * $Date$
 *
 * $Id$
 */

#include "ThermoLimiter.h"
#include "../SoftErrorLimiter/beep.h"
#include <rtm/CorbaNaming.h>
#include <hrpModel/ModelLoaderUtil.h>
#include <hrpUtil/MatrixSolvers.h>

#define DEBUGP ((m_debugLevel==1 && loop%200==0) || m_debugLevel > 1 )
#define DQ_MAX 1.0

// Module specification
// <rtc-template block="module_spec">
static const char* thermolimiter_spec[] =
  {
    "implementation_id", "ThermoLimiter",
    "type_name",         "ThermoLimiter",
    "description",       "null component",
    "version",           "1.0",
    "vendor",            "AIST",
    "category",          "example",
    "activity_type",     "DataFlowComponent",
    "max_instance",      "10",
    "language",          "C++",
    "lang_type",         "compile",
    // Configuration variables
    "conf.default.string", "test",
    "conf.default.intvec", "1,2,3",
    "conf.default.double", "1.234",

    ""
  };
// </rtc-template>

ThermoLimiter::ThermoLimiter(RTC::Manager* manager)
  : RTC::DataFlowComponentBase(manager),
    // <rtc-template block="initializer">
    m_tempInIn("tempIn", m_tempIn),
    m_tauInIn("tauIn", m_tauIn),
    m_qCurrentInIn("qCurrentIn", m_qCurrentIn),
    m_tauMaxOutOut("tauMax", m_tauMaxOut),
    m_ThermoLimiterServicePort("ThermoLimiterService"),
    m_debugLevel(1)
{
  m_service0.thermo_limiter(this);
}

ThermoLimiter::~ThermoLimiter()
{
}



RTC::ReturnCode_t ThermoLimiter::onInitialize()
{
  std::cout << m_profile.instance_name << ": onInitialize()" << std::endl;
  // <rtc-template block="bind_config">
  // Bind variables and configuration variable
  bindParameter("debugLevel", m_debugLevel, "1");
  
  // </rtc-template>

  // Registration: InPort/OutPort/Service
  // <rtc-template block="registration">
  // Set InPort buffers
  addInPort("tempIn", m_tempInIn);
  addInPort("tauIn", m_tauInIn);
  addInPort("qCurrentIn", m_qCurrentInIn);

  // Set OutPort buffer
  addOutPort("tauMax", m_tauMaxOutOut);
  
  // Set service provider to Ports
  m_ThermoLimiterServicePort.registerProvider("service0", "ThermoLimiterService", m_service0);
  
  // Set service consumers to Ports
  addPort(m_ThermoLimiterServicePort);
  
  // Set CORBA Service Ports
  
  // </rtc-template>

  RTC::Properties& prop = getProperties();
  coil::stringTo(m_dt, prop["dt"].c_str());

  m_robot = hrp::BodyPtr(new hrp::Body());

  RTC::Manager& rtcManager = RTC::Manager::instance();
  std::string nameServer = rtcManager.getConfig()["corba.nameservers"];
  int comPos = nameServer.find(",");
  if (comPos < 0){
    comPos = nameServer.length();
  }
  nameServer = nameServer.substr(0, comPos);
  RTC::CorbaNaming naming(rtcManager.getORB(), nameServer.c_str());
  if (!loadBodyFromModelLoader(m_robot, prop["model"].c_str(),
                               CosNaming::NamingContext::_duplicate(naming.getRootContext())
        )){
    std::cerr << "failed to load model[" << prop["model"] << "]"
              << std::endl;
  }

  // make thermo limiter params
  m_thermoLimiterParams.resize(m_robot->numJoints());
  
  // set limit of motor temperature
  coil::vstring motorTemperatureLimitFromConf = coil::split(prop["motor_temperature_limit"], ",");
  if (motorTemperatureLimitFromConf.size() != m_robot->numJoints()) {
    std::cerr <<  "[WARN]: size of motor_temperature_limit is " << motorTemperatureLimitFromConf.size() << ", not equal to " << m_robot->numJoints() << std::endl;
    for (int i = 0; i < m_robot->numJoints(); i++) {
      m_thermoLimiterParams[i].maxTemperature = 80.0;
      m_thermoLimiterParams[i].tauMax = m_robot->joint(i)->climit;
      m_thermoLimiterParams[i].temperatureErrorFlag = false;
    }
  } else {
    for (int i = 0; i < m_robot->numJoints(); i++) {
      coil::stringTo(m_thermoLimiterParams[i].maxTemperature, motorTemperatureLimitFromConf[i].c_str());
      m_thermoLimiterParams[i].tauMax = m_robot->joint(i)->climit;
      m_thermoLimiterParams[i].temperatureErrorFlag = false;
    }
  }
  if (m_debugLevel > 0) {
    std::cerr <<  "motor_temperature_limit: ";
    for(std::vector<ThermoLimiterParam>::iterator it = m_thermoLimiterParams.begin(); it != m_thermoLimiterParams.end(); ++it){
      std::cerr << (*it).maxTemperature << " ";
    }
    std::cerr << std::endl;
  }

  // set temperature of environment
  double ambientTemp = 25.0;
  if (prop["ambient_tmp"] != "") {
    coil::stringTo(ambientTemp, prop["ambient_tmp"].c_str());
  }
  std::cerr <<  m_profile.instance_name << ": ambient temperature: " << ambientTemp << std::endl;

  // set dt [sec] for thermo limitation 
  m_term = 120;
  if (prop["thermo_limiter_term"] != "") {
    coil::stringTo(m_term, prop["thermo_limiter_term"].c_str());
  }
  std::cerr <<  m_profile.instance_name << ": thermo_limiter_term: " << m_term << std::endl;
  
  // set limit of motor heat parameters
  coil::vstring motorHeatParamsFromConf = coil::split(prop["motor_heat_params"], ",");
  if (motorHeatParamsFromConf.size() != 2 * m_robot->numJoints()) {
    std::cerr <<  "[WARN]: size of motor_heat_param is " << motorHeatParamsFromConf.size() << ", not equal to 2 * " << m_robot->numJoints() << std::endl;
    for (int i = 0; i < m_robot->numJoints(); i++) {
      m_thermoLimiterParams[i].defaultParams();
      m_thermoLimiterParams[i].param.temperature = ambientTemp;
    }

  } else {
    for (int i = 0; i < m_robot->numJoints(); i++) {
      m_thermoLimiterParams[i].param.temperature = ambientTemp;
      coil::stringTo(m_thermoLimiterParams[i].param.currentCoeffs, motorHeatParamsFromConf[2 * i].c_str());
      coil::stringTo(m_thermoLimiterParams[i].param.thermoCoeffs, motorHeatParamsFromConf[2 * i + 1].c_str());
    }
  }
  
  if (m_debugLevel > 0) {
    std::cerr <<  "motor_heat_param: ";
    for(std::vector<ThermoLimiterParam>::iterator it = m_thermoLimiterParams.begin(); it != m_thermoLimiterParams.end(); ++it){
      std::cerr << (*it).param.temperature << "," << (*it).param.currentCoeffs << "," << (*it).param.thermoCoeffs << ", ";
    }
    std::cerr << std::endl;
  }
  
  // make torque controller
  // set limit of motor heat parameters
  coil::vstring torqueControllerParamsFromConf = coil::split(prop["torque_controller_params"], ",");
  m_motorTwoDofControllers.resize(m_robot->numJoints());
  if (torqueControllerParamsFromConf.size() != 2 * m_robot->numJoints()) {
    std::cerr <<  "[WARN]: size of torque_controller_params is " << torqueControllerParamsFromConf.size() << ", not equal to 2 * " << m_robot->numJoints() << std::endl;
    for (std::vector<TwoDofController>::iterator it = m_motorTwoDofControllers.begin(); it != m_motorTwoDofControllers.end() ; ++it) {
      (*it).setup(400.0, 0.04, m_dt); // set default params
      // (*it).setup(400.0, 1.0, m_dt);
    }
  } else {
    double tdcParamK, tdcParamT;
    for (int i = 0; i < m_robot->numJoints(); i++) {
      coil::stringTo(tdcParamK, torqueControllerParamsFromConf[2 * i].c_str());
      coil::stringTo(tdcParamT, torqueControllerParamsFromConf[2 * i + 1].c_str());
      m_motorTwoDofControllers[i].setup(tdcParamK, tdcParamT, m_dt);
    }
  }

  // allocate memory for outPorts
  m_tauMaxOut.data.length(m_robot->numJoints());
  
  return RTC::RTC_OK;
}



/*
RTC::ReturnCode_t ThermoLimiter::onFinalize()
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t ThermoLimiter::onStartup(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t ThermoLimiter::onShutdown(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

RTC::ReturnCode_t ThermoLimiter::onActivated(RTC::UniqueId ec_id)
{
  std::cout << m_profile.instance_name<< ": onActivated(" << ec_id << ")" << std::endl;
  return RTC::RTC_OK;
}

RTC::ReturnCode_t ThermoLimiter::onDeactivated(RTC::UniqueId ec_id)
{
  std::cout << m_profile.instance_name<< ": onDeactivated(" << ec_id << ")" << std::endl;
  return RTC::RTC_OK;
}

RTC::ReturnCode_t ThermoLimiter::onExecute(RTC::UniqueId ec_id)
{
  // std::cout << m_profile.instance_name<< ": onExecute(" << ec_id << "), data = " << m_data.data << std::endl;
  static long long loop = 0;
  loop ++;

  coil::TimeValue coiltm(coil::gettimeofday());
  RTC::Time tm;
  tm.sec = coiltm.sec();
  tm.nsec = coiltm.usec()*1000;
  bool isTempError = false;

  // update port
  if (m_tempInIn.isNew()) {
    m_tempInIn.read();
  }
  if (m_tauInIn.isNew()) {
    m_tauInIn.read();
  }
  if (m_qCurrentInIn.isNew()) {
    m_qCurrentInIn.read();
  }

  // calculate tauMax
  isTempError = limitTemperature();
  // call beep
  if (isTempError) {
    start_beep(3136);
  } else {
    stop_beep();
  }
  // output restricted tauMax
  for (int i = 0; i < m_robot->numJoints(); i++) {
    m_tauMaxOut.data[i] = m_thermoLimiterParams[i].tauMax;
  }
  m_tauMaxOut.tm = tm;
  m_tauMaxOutOut.write();
  return RTC::RTC_OK;
}

/*
RTC::ReturnCode_t ThermoLimiter::onAborting(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t ThermoLimiter::onError(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t ThermoLimiter::onReset(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t ThermoLimiter::onStateUpdate(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

/*
RTC::ReturnCode_t ThermoLimiter::onRateChanged(RTC::UniqueId ec_id)
{
  return RTC::RTC_OK;
}
*/

bool ThermoLimiter::isMaxTemperatureError(int jointId)
{
  if (jointId < m_robot->numJoints()) {
    return m_thermoLimiterParams[jointId].temperatureErrorFlag;
  } else {
    std::cerr << "Target jointId is over numJoints " << m_robot->numJoints() << std::endl;
    return false;
  }
}

double ThermoLimiter::getMaxToruqe(int jointId)
{
  if (jointId < m_robot->numJoints()) {
    return m_thermoLimiterParams[jointId].tauMax;
  } else {
    std::cerr << "Target jointId is over numJoints " << m_robot->numJoints() << std::endl;
    return 0.0;
  }
}


bool ThermoLimiter::limitTemperature(void)
{
  static long loop = 0;
  loop++;
  
  int numJoints = m_robot->numJoints();
  bool isTempError = false;
  double temp;
  hrp::dvector squareTauMax(numJoints);

  if (DEBUGP) {
    std::cerr << "[" << m_profile.instance_name << "]" << std::endl;
  }
  
  if ( m_tempIn.data.length() ==  m_robot->numJoints() ) {
    if (DEBUGP) {
      std::cerr << "temperature: ";
      for (int i = 0; i < numJoints; i++) {
        std::cerr << " " << m_tempIn.data[i];
      }
      std::cerr << std::endl;
      std::cerr << "tauIn: ";
      for (int i = 0; i < numJoints; i++) {
        std::cerr << " " << m_tauIn.data[i];
      }
      std::cerr << std::endl;
    }

    for (int i = 0; i < numJoints; i++) {
      temp = m_tempIn.data[i];
      // limit temperature
      squareTauMax[i]
        = (((m_thermoLimiterParams[i].maxTemperature - temp) / m_term)
           + m_thermoLimiterParams[i].param.thermoCoeffs * (temp - m_thermoLimiterParams[i].param.temperature))
        / m_thermoLimiterParams[i].param.currentCoeffs;

      // determine tauMax
      if (squareTauMax[i] < 0) {
        std::cerr << "[WARN] tauMax ** 2 = " << squareTauMax[i] << " < 0 in Joint " << i << std::endl;
        m_thermoLimiterParams[i].tauMax = m_robot->joint(i)->climit;
        m_thermoLimiterParams[i].temperatureErrorFlag = true;
        isTempError = true;
      } else {
        m_thermoLimiterParams[i].tauMax = std::sqrt(squareTauMax[i]);
        if (std::pow(m_tauIn.data[i], 2) > squareTauMax[i]) {
          std::cerr << "[WARN] tauMax over in Joint " << i << ": ||" << m_tauIn.data[i] << "|| > " << m_thermoLimiterParams[i].tauMax << std::endl;
          m_thermoLimiterParams[i].temperatureErrorFlag = true;
          isTempError = true;
        } else {
          m_thermoLimiterParams[i].temperatureErrorFlag = false;
        }
      }
    }
    
    if (DEBUGP) {
      std::cerr << std::endl;
      std::cerr << "tauMax: ";
      for (int i = 0; i < m_robot->numJoints(); i++) {
        std::cerr << m_thermoLimiterParams[i].tauMax << " ";
      }
      std::cerr << std::endl;
    }
  }
  return isTempError;
}

extern "C"
{

  void ThermoLimiterInit(RTC::Manager* manager)
  {
    RTC::Properties profile(thermolimiter_spec);
    manager->registerFactory(profile,
                             RTC::Create<ThermoLimiter>,
                             RTC::Delete<ThermoLimiter>);
  }

};


