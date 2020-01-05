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

#include <iostream>
#include <memory>
#include <sstream>
#include "latke.h"
#include "BlockingQueue.h"
#include <math.h>
#include <chrono>
#include <cassert>
#include <thread>
#include "ArchFactory.h"

using namespace ltk;

template<typename M> struct JobInfo {
	JobInfo(DeviceOCL *dev, std::unique_ptr<M> *hostToDev,
			std::unique_ptr<M> *devToHost, JobInfo *previous) :
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

	JobInfo *prev;
};

enum pattern_t{
    RGGB = 0,
    GRBG = 1,
    GBRG = 2,
    BGGR = 3
};

BlockingQueue<JobInfo<DualBufferOCL>*> mappedHostToDeviceQueue;
BlockingQueue<JobInfo<DualBufferOCL>*> mappedDeviceToHostQueue;

void CL_CALLBACK HostToDeviceMappedCallback(cl_event event,
		cl_int cmd_exec_status, void *user_data) {

	assert(user_data);

	auto info = (JobInfo<DualBufferOCL>*) user_data;

	// push mapped image into queue
	mappedHostToDeviceQueue.push(info);
}

void CL_CALLBACK DeviceToHostMappedCallback(cl_event event,
		cl_int cmd_exec_status, void *user_data) {

	assert(user_data);
	// handle processed data
	auto info = (JobInfo<DualBufferOCL>*) user_data;

	// push mapped image into queue
	mappedDeviceToHostQueue.push(info);
}

int main() {
	const uint32_t numBuffers =64;
	uint32_t bps_out = 3;
	uint32_t bufferWidth = 3840;
	uint32_t bufferPitch = bufferWidth;
	uint32_t bufferHeight = 2160;
	uint32_t frameSize = bufferPitch * bufferHeight;
	uint32_t bufferPitchOut = bufferWidth * bps_out;

	// 1. create device manager
	auto deviceManager = std::make_unique<DeviceManagerOCL>(true);
	auto rc = deviceManager->init(0, true);
	if (rc != DeviceSuccess) {
		std::cout << "Failed to initialize OpenCL device";
		exit(-1);
	}

	auto dev = deviceManager->getDevice(0);

	auto arch = ArchFactory::getArchitecture(dev->deviceInfo->venderId);

	const int numImages = 4;
	const int numBatches = numBuffers / numImages;

	std::unique_ptr<DualBufferOCL> hostToDevice[numImages];
	std::unique_ptr<DualBufferOCL> deviceToHost[numImages];
	std::unique_ptr<QueueOCL> kernelQueue[numImages];
	JobInfo<DualBufferOCL> *currentJobInfo[numImages];
	JobInfo<DualBufferOCL> *prevJobInfo[numImages];

	const int tile_rows = 5;
	const int tile_columns = 32;
	std::stringstream buildOptions;
	buildOptions << " -I ./ ";
	buildOptions << " -D TILE_ROWS=" << tile_rows;
	buildOptions << " -D TILE_COLS=" << tile_columns;
	switch (arch->getVendorId()){
	case vendorIdAMD:
		buildOptions << " -D AMD_GPU_ARCH";
		break;
	case vendorIdNVD:
		buildOptions << " -D NVIDIA_ARCH";
		break;
	}
	//buildOptions << " -D DEBUG";

	KernelInitInfoBase initInfoBase(dev, buildOptions.str(), "",
	BUILD_BINARY_IN_MEMORY);
	KernelInitInfo initInfo(initInfoBase, "debayer.cl", "debayer",
			"malvar_he_cutler_demosaic");
	std::unique_ptr<KernelOCL> kernel = std::make_unique<KernelOCL>(initInfo);

	for (int i = 0; i < numImages; ++i) {
		hostToDevice[i] = std::make_unique<DualBufferOCL>(dev, frameSize,true);
		deviceToHost[i] = std::make_unique<DualBufferOCL>(dev, frameSize*3 + 1, false);
		kernelQueue[i] = std::make_unique<QueueOCL>(dev);
		currentJobInfo[i] = nullptr;
		prevJobInfo[i] = nullptr;
	}

	// queue all kernel runs
	for (int j = 0; j < numBatches; j++) {
		for (int i = 0; i < numImages; ++i) {
			bool lastBatch = j == numBatches - 1;
			auto prev = currentJobInfo[i];
			currentJobInfo[i] = new JobInfo<DualBufferOCL>(dev, &hostToDevice[i],
					&deviceToHost[i], prevJobInfo[i]);
			prevJobInfo[i] = prev;

			// map
			// (wait for previous kernel to complete)
			cl_event hostToDeviceMapped;
			if (!hostToDevice[i]->map(prev ? 1 : 0,
					prev ? &prev->kernelCompleted : nullptr,
					&hostToDeviceMapped, false)) {
				return -1;
			}
			// set callback, which will add this image
			// info to host-side queue of mapped buffers
			auto error_code = clSetEventCallback(hostToDeviceMapped,
			CL_COMPLETE, &HostToDeviceMappedCallback, currentJobInfo[i]);
			if (DeviceSuccess != error_code) {
				Util::LogError("Error: clSetEventCallback returned %s.\n",
						Util::TranslateOpenCLError(error_code));
				return -1;
			}
			Util::ReleaseEvent(hostToDeviceMapped);

			// unmap
			if (!hostToDevice[i]->unmap(1,
					&currentJobInfo[i]->hostToDevice->triggerMemUnmap,
					&currentJobInfo[i]->hostToDevice->memUnmapped)) {
				return -1;
			}

			int bayer_pattern = RGGB;
			kernel->pushArg<cl_uint>(&bufferHeight);
			kernel->pushArg<cl_uint>(&bufferWidth);
			kernel->pushArg<cl_mem>(hostToDevice[i]->getDeviceMem());
			kernel->pushArg<cl_uint>(&bufferPitch);
			kernel->pushArg<cl_mem>(deviceToHost[i]->getDeviceMem());
			kernel->pushArg<cl_uint>(&bufferPitchOut);
			kernel->pushArg<cl_int>(&bayer_pattern);

			EnqueueInfoOCL info(kernelQueue[i].get());
			info.dimension = 2;
			info.local_work_size[0] = tile_columns;
			info.local_work_size[1] = tile_rows;
			info.global_work_size[0] = (size_t) std::ceil(
					bufferWidth / (double) tile_columns)
					* info.local_work_size[0];
			info.global_work_size[1] = (size_t) std::ceil(
					bufferHeight / (double) tile_rows)
					* info.local_work_size[1];
			info.needsCompletionEvent = true;
			info.pushWaitEvent(currentJobInfo[i]->hostToDevice->memUnmapped);
			// wait for unmapping of previous deviceToHost
			if (prev) {
				info.pushWaitEvent(prev->hostToDevice->memUnmapped);
			}
			try {
				kernel->enqueue(info);
			} catch (std::exception &ex) {
				// todo: handle exception
			}
			currentJobInfo[i]->kernelCompleted = info.completionEvent;

			// map
			cl_event deviceToHostMapped;
			if (!deviceToHost[i]->map(1, &currentJobInfo[i]->kernelCompleted,
					&deviceToHostMapped, false)) {
				return -1;
			}
			// set callback on mapping
			error_code = clSetEventCallback(deviceToHostMapped,
			CL_COMPLETE, &DeviceToHostMappedCallback, currentJobInfo[i]);
			if (DeviceSuccess != error_code) {
				Util::LogError("Error: clSetEventCallback returned %s.\n",
						Util::TranslateOpenCLError(error_code));
				return -1;
			}
			Util::ReleaseEvent(deviceToHostMapped);

			// unmap (except last batch)
			if (!lastBatch) {
				if (!deviceToHost[i]->unmap(1,
						&currentJobInfo[i]->deviceToHost->triggerMemUnmap,
						&currentJobInfo[i]->deviceToHost->memUnmapped)) {
					return -1;
				}
			}
		}
	}

	auto start = std::chrono::high_resolution_clock::now();

	// wait for images from queue, fill them, and trigger unmap event
	std::thread pushImages([]() {
		JobInfo<DualBufferOCL> *info = nullptr;
		int count = 0;
		while (mappedHostToDeviceQueue.waitAndPop(info)) {
			/*
			 * fill unprocessed image memory
			 */
			// trigger unmap, allowing current kernel to proceed
			Util::SetEventComplete(info->hostToDevice->triggerMemUnmap);
			count++;
			if (count == numBuffers)
				break;
		}
	});

	// wait for processed images from queue, handle them,
	// and trigger unmap event
	std::thread pullImages([]() {
		JobInfo<DualBufferOCL> *info = nullptr;
		int count = 0;
		while (mappedDeviceToHostQueue.waitAndPop(info)) {

			/*
			 *
			 * handle processed image memory
			 *
			 */

			// trigger unmap, allowing next kernel to proceed
			Util::SetEventComplete(info->deviceToHost->triggerMemUnmap);

			// cleanup
			delete info->prev;
			info->prev = nullptr;
			count++;
			if (count == numBuffers)
				break;
		}
	});

	pushImages.join();
	pullImages.join();

	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = finish - start;

	// cleanup
	for (int i = 0; i < numImages; ++i) {
		deviceToHost[i]->unmap(0, nullptr, nullptr);
		delete currentJobInfo[i]->prev;
		delete currentJobInfo[i];
	}
	delete arch;
	fprintf(stdout, "time per image = %f ms\n",
			(elapsed.count() * 1000) / (double) numBuffers);
	return 0;
}
