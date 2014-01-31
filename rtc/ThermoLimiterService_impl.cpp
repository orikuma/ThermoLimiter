// -*- mode: c++; indent-tabs-mode: t; tab-width: 4; c-basic-offset: 4; -*-
#include <iostream>
#include "ThermoLimiterService_impl.h"
#include "ThermoLimiter.h"

ThermoLimiterService_impl::ThermoLimiterService_impl() : m_thermo_limiter(NULL)
{
}

ThermoLimiterService_impl::~ThermoLimiterService_impl()
{
}

bool ThermoLimiterService_impl::isMaxTemperatureError(CORBA::Long jointId)
{
	return m_thermo_limiter->isMaxTemperatureError(jointId);
}

double ThermoLimiterService_impl::getMaxToruqe(CORBA::Long jointId)
{
	return m_thermo_limiter->getMaxToruqe(jointId);
}

void ThermoLimiterService_impl::thermo_limiter(ThermoLimiter *i_thermo_limiter)
{
	m_thermo_limiter = i_thermo_limiter;
} 

