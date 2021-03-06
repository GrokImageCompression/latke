/*
 * Copyright 2016-2020 Grok Image Compression Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#ifdef _WIN32
#include "windirent.h"
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <dirent.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif /* _WIN32 */

#include <iostream>
#include <sstream>
#include "latke.h"
#include "BlockingQueue.h"
#include <math.h>
#include <chrono>
#include <cassert>
#include "ArchFactory.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include <string>
#include "ThreadPool.h"
#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"
using namespace TCLAP;

using namespace ltk;

template<typename M> struct JobInfo {
	JobInfo(DeviceOCL *dev, std::shared_ptr<M> hostToDev,
			std::shared_ptr<M> devToHost, JobInfo *previous) :
			hostToDevice(new MemMapEvents<M>(dev, hostToDev)), kernelCompleted(
					0), deviceToHost(new MemMapEvents<M>(dev, devToHost)), prev(
					previous) {
	}
	~JobInfo() {
		delete hostToDevice;
		Util::ReleaseEvent(kernelCompleted);
		delete deviceToHost;
	}

	MemMapEvents<M> *hostToDevice;
	cl_event kernelCompleted;
	MemMapEvents<M> *deviceToHost;
	std::string fileName;

	JobInfo *prev;
};

typedef void (CL_CALLBACK *pfn_event_notify)(cl_event event,
		cl_int event_command_exec_status, void *user_data);

class BufferAllocater {
public:
	BufferAllocater(DeviceOCL *dev, size_t dimX, size_t dimY, size_t bps,
			uint32_t data_type, cl_command_queue_properties queue_props) :
			m_dev(dev),
			m_dimX(dimX),
			m_dimY(dimY),
			m_bps(bps),
			m_queue_props(queue_props)
  {
		(void) data_type;
	}
	std::unique_ptr<DualBufferOCL> allocate(bool hostToDevice) {
		return std::make_unique<DualBufferOCL>(m_dev, m_dimX * m_dimY * m_bps,
				hostToDevice ? HostToDeviceBuffer : DeviceToHostBuffer, m_queue_props);
	}
private:
	DeviceOCL *m_dev;
	size_t m_dimX;
	size_t m_dimY;
	size_t m_bps;
	cl_command_queue_properties m_queue_props;
};

class ImageAllocater {
public:
	ImageAllocater(DeviceOCL *dev, size_t dimX, size_t dimY, size_t bps,
			uint32_t data_type, cl_command_queue_properties queue_props) :
			m_dev(dev),
			m_dimX(dimX),
			m_dimY(dimY),
			m_bps(bps),
			m_data_type(data_type),
			m_queue_props(queue_props) {
	}
	std::unique_ptr<DualImageOCL> allocate(bool hostToDevice) {
		return std::make_unique<DualImageOCL>(m_dev, m_dimX, m_dimY,
				(m_bps == 1 ? CL_R : CL_RGBA), m_data_type, hostToDevice, m_queue_props);
	}
private:
	DeviceOCL *m_dev;
	size_t m_dimX;
	size_t m_dimY;
	size_t m_bps;
	uint32_t m_data_type;
	cl_command_queue_properties m_queue_props;
};

