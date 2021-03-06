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
#include "latke_config.h"
#ifdef OPENCL_FOUND
#include "platform.h"
#include <string>
#include "QueueOCL.h"
#include "UtilOCL.h"
#include "EnqueueInfoOCL.h"


namespace ltk {

const uint32_t LOAD_BINARY = 0;
const uint32_t BUILD_BINARY_IN_MEMORY =	1;
const uint32_t BUILD_BINARY_OFFLINE	= 2;
const uint32_t BUILD_BINARY_OFFLINE_ALL_DEVICES	= 4;


struct KernelInitInfoBase {
	KernelInitInfoBase() : device(nullptr),
							buildOptions(""),
							directory(""),
							binaryBuildMethod(LOAD_BINARY)
	{}
	KernelInitInfoBase(DeviceOCL *dev, std::string bldOptions, std::string directory,
			uint32_t binaryBuildMethod) :
			device(dev), buildOptions(bldOptions), directory(directory), binaryBuildMethod(
					binaryBuildMethod) {
	}
	KernelInitInfoBase(const KernelInitInfoBase &other) :
			device(other.device), buildOptions(other.buildOptions), directory(
					other.directory), binaryBuildMethod(other.binaryBuildMethod) {
	}
	DeviceOCL *device;
	std::string buildOptions;
	std::string directory;
	uint32_t binaryBuildMethod;
};

struct KernelInitInfo: KernelInitInfoBase {
	KernelInitInfo() : KernelInitInfoBase(),
						programName(""),
						binaryName(""),
						kernelName("")
	{}
	KernelInitInfo(KernelInitInfoBase initInfo, std::string progName,
			std::string binaryName, std::string knlName) :
			KernelInitInfoBase(initInfo), programName(progName), binaryName(
					binaryName), kernelName(knlName) {
	}
	std::string programName;
	std::string binaryName;
	std::string kernelName;
};

class KernelOCL {
public:
	KernelOCL(KernelInitInfo initInfo, cl_program program);
	KernelOCL(KernelInitInfo initInfo);
	virtual ~KernelOCL(void);

	static cl_program generateProgram(KernelInitInfo init);

	cl_kernel getKernel() {
		return myKernel;
	}
	cl_device_id getDevice() {
		return device;
	}
	void enqueue(EnqueueInfoOCL &info);
	static void generateBinary(KernelInitInfo init);

	template<typename T> void pushArg(T *val) {
		auto error_code = clSetKernelArg(myKernel, argCount++, sizeof(T), val);
		if (DeviceSuccess != error_code) {
			Util::LogError("Error: setKernelArgs returned %s.\n",
					Util::TranslateOpenCLError(error_code));
			throw std::exception();
		}
	}
protected:
	static void generateBinaryName(buildProgramData &data);
	static buildProgramData getProgramData(KernelInitInfo init);
	static std::string getBuildOptions(KernelInitInfo init);
	KernelInitInfo initInfo;
	cl_kernel myKernel;
	cl_ulong localMemorySize;
	cl_device_id device;
	cl_context context;
	uint32_t argCount;
	cl_program program;
};
}
#endif
