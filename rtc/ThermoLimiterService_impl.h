// -*- mode: c++; indent-tabs-mode: t; tab-width: 4; c-basic-offset: 4; -*-
#ifndef __THERMO_LIMITER_SERVICE_H__
#define __THERMO_LIMITER_SERVICE_H__

#include "ThermoLimiterService.hh"

class ThermoLimiter;

class ThermoLimiterService_impl
	: public virtual POA_OpenHRP::ThermoLimiterService,
	  public virtual PortableServer::RefCountServantBase
{
public:
	/**
	   \brief constructor
	*/
	ThermoLimiterService_impl();

	/**
	   \brief destructor
	*/
	virtual ~ThermoLimiterService_impl();

	bool isMaxTemperatureError(CORBA::Long jointId);
	double getMaxToruqe(CORBA::Long jointId);
	void thermo_limiter(ThermoLimiter *i_thermo_limiter);

private:
	ThermoLimiter *m_thermo_limiter;
};

#endif
