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

/**********************************************************************
 Copyright ©2013 Advanced Micro Devices, Inc. All rights reserved.
 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 •   Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 •   Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ********************************************************************/

#include "latke_config.h"
#ifdef OPENCL_FOUND
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <limits.h>
#include <unistd.h>
#endif

#include "UtilOCL.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdarg.h>
#include <cstring>
#include <memory>

namespace ltk {

/**
 * getOpencLErrorCodeStr
 * global function to get corresponding string for a error code
 * @param input the error code
 * @return const char* the string for the error code
 */
const char* getOpenCLErrorCodeStr(std::string input) {
    return "unknown error code";
}

/**
 * error
 * constant function, Prints error messages
 * @param errorMsg std::string message
 */
void error(std::string errorMsg) {
    std::cout << "Error: " << errorMsg << std::endl;
}

/**
 * getPath
 * @return path of the current directory
 */
std::string getPath() {
#ifdef _WIN32
	char buffer[MAX_PATH];
#ifdef UNICODE
	if (!GetModuleFileName(NULL, (LPWCH)buffer, sizeof(buffer)))
	{
		throw std::string("GetModuleFileName() failed!");
	}
#else
	if (!GetModuleFileName(NULL, buffer, sizeof(buffer)))
	{
		throw std::string("GetModuleFileName() failed!");
	}
#endif
	std::string str(buffer);
	/* '\' == 92 */
	int last = (int)str.find_last_of((char)92);
#else
    char buffer[PATH_MAX + 1];
    ssize_t len;
    if ((len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1)) == -1) {
        throw std::string("readlink() failed!");
    }
    buffer[len] = '\0';
    std::string str(buffer);
    /* '/' == 47 */
    int last = (int) str.find_last_of((char) 47);
#endif
    return str.substr(0, last + 1);
}

/**
 * Opens the CL program file
 * @return true if success else false
 */
bool KernelFile::open(const char *fileName) {
    size_t size;
    // Open file stream
    std::fstream f(fileName, (std::fstream::in | std::fstream::binary));
    // Check if we have opened file stream
    if (f.is_open()) {
        size_t sizeFile;
        // Find the stream size
        f.seekg(0, std::fstream::end);
        size = sizeFile = (size_t) f.tellg();
        f.seekg(0, std::fstream::beg);
        std::unique_ptr<char[]> str(new char[size + 1]);
        if (!str) {
            f.close();
            return false;
        }
        // Read file
        f.read(str.get(), sizeFile);
        f.close();
        str[size] = '\0';
        source_ = str.get();
        return true;
    }
    return false;
}

/**
 * writeBinaryToFile
 * @param fileName Name of the file
 * @param binary char binary array
 * @param numBytes number of bytes
 * @return true if success else false
 */
int KernelFile::writeBinaryToFile(const char *fileName, const char *binary,
        size_t numBytes) {
    FILE *output = fopen(fileName, "wb");
    if (!output) {
        return FAILURE;
    }
    fwrite(binary, sizeof(char), numBytes, output);
    fclose(output);
    return SUCCESS;
}

/**
 * readBinaryToFile
 * @param fileName name of file
 * @return true if success else false
 */
int KernelFile::readBinaryFromFile(const char *fileName) {
    size_t size = 0, val;
    FILE *input = fopen(fileName, "rb");
    if (!input) {
        return FAILURE;
    }
    fseek(input, 0L, SEEK_END);
    size = ftell(input);
    rewind(input);
    std::unique_ptr<char[]> binary(new char[size]);
    if (binary == NULL) {
        fclose(input);
        return FAILURE;
    }
    val = fread(binary.get(), sizeof(char), size, input);
    fclose(input);
    source_.assign(binary.get(), size);
    return SUCCESS;
}

/**
 * Replaces Newline with spaces
 */
void KernelFile::replaceNewlineWithSpaces() {
    size_t pos = source_.find_first_of('\n', 0);
    while (pos != -1) {
        source_.replace(pos, 1, " ");
        pos = source_.find_first_of('\n', pos + 1);
    }
    pos = source_.find_first_of('\r', 0);
    while (pos != -1) {
        source_.replace(pos, 1, " ");
        pos = source_.find_first_of('\r', pos + 1);
    }
}

/**
 * source
 * Returns a pointer to the string object with the source code
 */
const std::string& KernelFile::source() const {
    return source_;
}

/**
 * getDefaultPlatform
 * selects default platform
 * @param numPlatforms Number of Available platforms
 * @param platforms an array of cl_platform_id
 * @param platform cl_platform_id
 * @param dType cl_device_type
 * @return 0 if success else nonzero
 */
bool getDefaultPlatform(cl_uint numPlatforms, cl_platform_id *platforms,
        cl_platform_id &platform, cl_device_type dType, bool verbose) {
    int status;
    char platformName[100];
    bool defaultPlatform = false;

    //First find AMD as default platform
    for (unsigned i = 0; i < numPlatforms; ++i) {
        status = clGetPlatformInfo(platforms[i],
        CL_PLATFORM_VENDOR, sizeof(platformName), platformName,
        NULL);
        CHECK_OPENCL_ERROR(status, "clGetPlatformInfo failed.");
        platform = platforms[i];

        if (!strcmp(platformName, "Advanced Micro Devices, Inc.")) {
            cl_context_properties cps[3] = {
            CL_CONTEXT_PLATFORM, (cl_context_properties) platform, 0 };
            cl_context context = clCreateContextFromType(cps, dType,
            NULL,
            NULL, &status);
            if (context)
                clReleaseContext(context);
            if (status == CL_DEVICE_NOT_FOUND) {
                defaultPlatform = false;
            } else {
                defaultPlatform = true;
                break;
            }
        }
    }

    //if there is no device of AMD platform, find
    //any first platform having this device
    if (!defaultPlatform) {
        for (unsigned i = 0; i < numPlatforms; ++i) {
            status = clGetPlatformInfo(platforms[i],
            CL_PLATFORM_VENDOR, sizeof(platformName), platformName,
            NULL);
            CHECK_OPENCL_ERROR(status, "clGetPlatformInfo failed.");
            platform = platforms[i];

            cl_context_properties cps[3] = {
            CL_CONTEXT_PLATFORM, (cl_context_properties) platform, 0 };
            cl_context context = clCreateContextFromType(cps, dType,
            NULL,
            NULL, &status);
            if (context)
                clReleaseContext(context);
            if (status == CL_DEVICE_NOT_FOUND) {
                defaultPlatform = false;
            } else {
                defaultPlatform = true;
                break;
            }
        }
    }

    if (verbose)
        std::cout << "Platform found : " << platformName << "\n";
    return defaultPlatform;
}
int getPlatformL(cl_platform_id &platform, int platformId,
        bool platformIdEnabled, cl_device_type dType, bool verbose) {
    cl_uint numPlatforms;
    cl_int status = clGetPlatformIDs(0, NULL, &numPlatforms);
    CHECK_OPENCL_ERROR(status, "clGetPlatformIDs failed.");
    if (0 < numPlatforms) {
        std::unique_ptr<cl_platform_id[]> platforms(
                new cl_platform_id[numPlatforms]);
        status = clGetPlatformIDs(numPlatforms, platforms.get(), NULL);
        CHECK_OPENCL_ERROR(status, "clGetPlatformIDs failed.");
        if (platformIdEnabled) {
            platform = platforms[platformId];
        } else {
            bool platformFound = false;

            switch (dType) {
				case CL_DEVICE_TYPE_GPU:
				case CL_DEVICE_TYPE_ACCELERATOR:
					//first find platform having GPU
					platformFound = getDefaultPlatform(numPlatforms,
							platforms.get(), platform, dType, verbose);
					if (platformFound)
						break;
					//fall-through
				default:
					//if there is no GPU/ACCELERATOR found,
					//then find platform having CPU
					if (!platformFound) {
						platformFound = getDefaultPlatform(numPlatforms,
								platforms.get(), platform, dType, verbose);
					}
					break;
           }
        }
    }
    if (NULL == platform) {
        error("NULL platform found so Exiting Application.");
        return FAILURE;
    }
    return SUCCESS;
}

int getDevices(cl_context &context, cl_device_id **devices,
        size_t *deviceCount) {
    /* First, get the size of device list data */
    size_t deviceListSize = 0;
    int status = 0;
    *deviceCount = 0;
    status = clGetContextInfo(context,
    CL_CONTEXT_DEVICES, 0,
    NULL, &deviceListSize);
    CHECK_OPENCL_ERROR(status, "clGetContextInfo failed.");
    *deviceCount = (int) (deviceListSize / sizeof(cl_device_id));
    /**
     * Now allocate memory for device list based on the size we got earlier
     * Note that this memory is allocated to a pointer which is a argument
     * so it must not be deleted inside this function. The Sample implementer
     * has to delete the devices pointer in the host code at clean up
     */
    (*devices) = new cl_device_id[deviceListSize];
    CHECK_ALLOCATION((*devices), "Failed to allocate memory (devices).");
    /* Now, get the device list data */
    status = clGetContextInfo(context,
    CL_CONTEXT_DEVICES, deviceListSize, (*devices),
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetGetContextInfo failed.");
    return SUCCESS;
}
/**
 * display devices
 * displays the devices in a platform
 * @param platform cl_platform_id
 * @param deviceType deviceType
 * @return 0 if success else nonzero
 */
int displayDevices(cl_platform_id platform, cl_device_type deviceType) {
    cl_int status;
    // Get platform name
    char platformVendor[1024];
    status = clGetPlatformInfo(platform, CL_PLATFORM_VENDOR,
            sizeof(platformVendor), platformVendor, NULL);
    CHECK_OPENCL_ERROR(status, "clGetPlatformInfo failed");
    std::cout << "Selected Platform Vendor : " << platformVendor << std::endl;
    // Get number of devices available
    cl_uint deviceCount = 0;
    status = clGetDeviceIDs(platform, deviceType, 0, NULL, &deviceCount);
    CHECK_OPENCL_ERROR(status, "clGetDeviceIDs failed");
    std::unique_ptr<cl_device_id[]> deviceIds(new cl_device_id[deviceCount]);
    CHECK_ALLOCATION(deviceIds, "Failed to allocate memory(deviceIds)");
    // Get device ids
    status = clGetDeviceIDs(platform, deviceType, deviceCount, deviceIds.get(),
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceIDs failed");
    // Print device index and device names
    for (cl_uint i = 0; i < deviceCount; ++i) {
        char deviceName[1024];
        status = clGetDeviceInfo(deviceIds[i], CL_DEVICE_NAME,
                sizeof(deviceName), deviceName, NULL);
        CHECK_OPENCL_ERROR(status, "clGetDeviceInfo failed");
        std::cout << "Device " << i << " : " << deviceName << std::endl
                << "Device ID : " << deviceIds[i] << std::endl;
    }
    return SUCCESS;
}

/**
 * buildOpenCLProgram
 * builds the opencl program
 * @param program program object
 * @param context cl_context object
 * @param buildData buildProgramData Object
 * @return 0 if success else nonzero
 */
int buildOpenCLProgram(cl_program &program, const cl_context &context,
        const buildProgramData &buildData) {
    // we only output debug info to console if we are building from source
    bool verbose = buildData.binaryName.empty();
    if (verbose)
        std::cout << std::endl;
    cl_int status = CL_SUCCESS;
    KernelFile kernelFile;
    auto programPath = buildData.programPath;
    // first try loading binary
    if (!buildData.binaryName.empty()) {
        // try short path
        auto binaryPath = buildData.binaryName;
        auto successfulRead = true;
        if (kernelFile.readBinaryFromFile(binaryPath.c_str())) {
            // short path failed; try full path
            if (programPath != ""){
				binaryPath = programPath + binaryPath;
				if (kernelFile.readBinaryFromFile(binaryPath.c_str())) {
	            	successfulRead = false;
				}
            } else {
            	successfulRead = false;
            }
        }
        if (!successfulRead){
			std::cout << "Failed to load binary file : " << binaryPath
					<< std::endl;
			return FAILURE;
        }

        const char *binary = kernelFile.source().c_str();
        size_t binarySize = kernelFile.source().size();
        program = clCreateProgramWithBinary(context, 1, &buildData.device,
                (const size_t*) &binarySize, (const unsigned char**) &binary,
                NULL, &status);
        CHECK_OPENCL_ERROR(status, "clCreateProgramWithBinary failed.");
    }
//otherwise, build from source
    else {
        std::cout << "Creating program " << buildData.programName
                << " from source" << std::endl;
        programPath += buildData.programName;
        if (!kernelFile.open(programPath.c_str())) {
            std::cout << "Failed to load kernel file: " << programPath
                    << std::endl;
            return FAILURE;
        }
        const char *source = kernelFile.source().c_str();
        size_t sourceSize[] = { strlen(source) };
        program = clCreateProgramWithSource(context, 1, &source, sourceSize,
                &status);
        CHECK_OPENCL_ERROR(status, "clCreateProgramWithSource failed.");
    }
    if (verbose)
        std::cout << "Building program " << buildData.programName << std::endl;
    std::string flagsStr = buildData.flagsStr;
// Get additional options
    if (buildData.flagsFileName.size() != 0) {
        KernelFile flagsFile;
        std::string flagsPath = getPath();
        flagsPath += buildData.flagsFileName;
        if (!flagsFile.open(flagsPath.c_str())) {
            std::cout << "Failed to load flags file: " << flagsPath
                    << std::endl;
            return FAILURE;
        }
        flagsFile.replaceNewlineWithSpaces();
        const char *flags = flagsFile.source().c_str();
        flagsStr.append(flags);
    }
    if (verbose)
        std::cout << "Build Options are : " << flagsStr.c_str() << std::endl;
    /* create a cl program executable for specified device*/
    status = clBuildProgram(program, 1, &buildData.device, flagsStr.c_str(),
    NULL, NULL);
    if (status != CL_SUCCESS) {
        if (status == CL_BUILD_PROGRAM_FAILURE) {
            cl_int logStatus;
            size_t buildLogSize = 0;
            logStatus = clGetProgramBuildInfo(program, buildData.device,
            CL_PROGRAM_BUILD_LOG, buildLogSize, nullptr, &buildLogSize);
            CHECK_OPENCL_ERROR(logStatus, "clGetProgramBuildInfo failed.");
            std::unique_ptr<char[]> buildLog(new char[buildLogSize]);
            CHECK_ALLOCATION(buildLog,
                    "Failed to allocate host memory. (buildLog)");
            memset(buildLog.get(), 0, buildLogSize);
            logStatus = clGetProgramBuildInfo(program, buildData.device,
            CL_PROGRAM_BUILD_LOG, buildLogSize, buildLog.get(),
            NULL);
            if (checkVal(logStatus, CL_SUCCESS,
                    "clGetProgramBuildInfo failed.")) {
                return FAILURE;
            }
            std::cout << " \n\t\t\tBUILD LOG\n";
            std::cout << " ************************************************\n";
            std::cout << buildLog.get() << std::endl;
            std::cout << " ************************************************\n";
        }
        CHECK_OPENCL_ERROR(status, "clBuildProgram failed.");
    }
    if (verbose) {
        size_t log_size = 0;
        cl_int err_status = clGetProgramBuildInfo(program, buildData.device,
        CL_PROGRAM_BUILD_LOG, 0,
        NULL, &log_size);
        CHECK_OPENCL_ERROR(err_status, "clGetProgramBuildInfo failed.");

        if (log_size > 1) {
            std::unique_ptr<char[]> build_log(new char[log_size]);
            size_t actual_log_size;
            build_log[0] = 0;
            err_status = clGetProgramBuildInfo(program, buildData.device,
            CL_PROGRAM_BUILD_LOG, log_size, build_log.get(), &actual_log_size);
            CHECK_OPENCL_ERROR(err_status, "clGetProgramBuildInfo failed.");
            printf("Build Log: \n%s", build_log.get());
        }
    }
    return SUCCESS;
}

/**
 * generateBinaryImage
 * generate Binary for a kernel
 * @param binaryData bifdata object
 * @return 0 if success else nonzero
 */
int generateBinaryImage(const bifData &binaryData) {
    int rc = SUCCESS;
    std::cout << "Generating binary image for " << binaryData.binaryName
            << std::endl;
    std::unique_ptr<cl_device_id[]> devices;
    std::unique_ptr<char*[]> binaries;
    std::unique_ptr<size_t[]> binarySizes;
    size_t numDevices = 0;
    cl_uint numPlatforms = 0;
    cl_platform_id platform = NULL;
    std::string flagsStr;
    KernelFile kernelFile;
    std::string kernelPath;
    const char *source = nullptr;
    cl_program program = 0;
    cl_context context = 0;

    cl_int status = clGetPlatformIDs(0, NULL, &numPlatforms);
    CHECK_OPENCL_ERROR_CLEANUP(status, "clGetPlatformIDs failed.");
    if (0 < numPlatforms) {
        std::unique_ptr<cl_platform_id[]> platforms(
                new cl_platform_id[numPlatforms]);
        status = clGetPlatformIDs(numPlatforms, platforms.get(), NULL);
        CHECK_OPENCL_ERROR_CLEANUP(status, "clGetPlatformIDs failed.");
        char platformName[100];
        for (unsigned i = 0; i < numPlatforms; ++i) {
            status = clGetPlatformInfo(platforms[i],
            CL_PLATFORM_VENDOR, sizeof(platformName), platformName,
            NULL);
            CHECK_OPENCL_ERROR_CLEANUP(status, "clGetPlatformInfo failed.");
            platform = platforms[i];
            if (!strcmp(platformName, "Advanced Micro Devices, Inc.")) {
                break;
            }
        }
        std::cout << "Platform found : " << platformName << std::endl;
    }
    if (NULL == platform) {
        std::cout << "NULL platform found so Exiting Application.";
        rc = FAILURE;
        goto CLEANUP;
    }
    /*
     * If we could find our platform, use it. Otherwise use just available platform.
     */
    {
        cl_context_properties cps[5] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties) platform,
        CL_CONTEXT_OFFLINE_DEVICES_AMD, (cl_context_properties) 1, 0 };
        context = clCreateContextFromType(cps,
        CL_DEVICE_TYPE_ALL,
        NULL,
        NULL, &status);
    }
    CHECK_OPENCL_ERROR_CLEANUP(status, "clCreateContextFromType failed.");
    /* create a CL program using the kernel source */
    kernelPath = binaryData.programPath + binaryData.programFileName;
    if (!kernelFile.open(kernelPath.c_str())) {
        std::cout << "Failed to load kernel file : " << kernelPath << std::endl;
        rc = FAILURE;
        goto CLEANUP;
    }
    source = kernelFile.source().c_str();
    {
        size_t sourceSize[] = { strlen(source) };
        program = clCreateProgramWithSource(context, 1, &source, sourceSize,
                &status);
    }
    CHECK_OPENCL_ERROR_CLEANUP(status, "clCreateProgramWithSource failed.");
    flagsStr = std::string(binaryData.flagsStr.c_str());
// tell compiler not to include source and intermediate representations
    flagsStr += " -fno-bin-llvmir -fno-bin-amdil -fno-bin-source";
// Get additional options
    if (binaryData.flagsFileName.size() != 0) {
        KernelFile flagsFile;
        std::string flagsPath = getPath();
        flagsPath.append(binaryData.flagsFileName.c_str());
        if (!flagsFile.open(flagsPath.c_str())) {
            std::cout << "Failed to load flags file: " << flagsPath
                    << std::endl;
            rc = FAILURE;
            goto CLEANUP;
        }
        flagsFile.replaceNewlineWithSpaces();
        flagsStr.append(flagsFile.source().c_str());
    }
    if (flagsStr.size() != 0) {
        std::cout << "Build Options are : " << flagsStr.c_str() << std::endl;
    }
    /* create a cl program executable for all the devices specified */
    status = clBuildProgram(program, (cl_uint) binaryData.numDevices,
            binaryData.devices, flagsStr.c_str(),
            NULL,
            NULL);
    CHECK_OPENCL_ERROR_CLEANUP(status, "clBuildProgram failed.");

    status = clGetProgramInfo(program,
    CL_PROGRAM_NUM_DEVICES, sizeof(numDevices), &numDevices,
    NULL);
    CHECK_OPENCL_ERROR_CLEANUP(status,
            "clGetProgramInfo(CL_PROGRAM_NUM_DEVICES) failed.");
    std::cout << "Number of devices found : " << numDevices << std::endl;
    devices = std::unique_ptr<cl_device_id[]>(new cl_device_id[numDevices]);
    binaries = std::unique_ptr<char*[]>(new char*[numDevices]);
    binarySizes = std::unique_ptr<size_t[]>(new size_t[numDevices]);

    /* grab the handles to all of the devices in the program. */
    status = clGetProgramInfo(program,
    CL_PROGRAM_DEVICES, sizeof(cl_device_id) * numDevices, devices.get(),
    NULL);
    CHECK_OPENCL_ERROR_CLEANUP(status,
            "clGetProgramInfo(CL_PROGRAM_DEVICES) failed.");

    /* figure out the sizes of each of the binaries. */
    status = clGetProgramInfo(program,
    CL_PROGRAM_BINARY_SIZES, sizeof(size_t) * numDevices, binarySizes.get(),
    NULL);
    CHECK_OPENCL_ERROR_CLEANUP(status,
            "clGetProgramInfo(CL_PROGRAM_BINARY_SIZES) failed.");

    /* copy over all of the generated binaries. */
    for (size_t i = 0; i < numDevices; i++) {
        if (binarySizes[i] != 0) {
            binaries[i] = new char[binarySizes[i]];
        } else {
            binaries[i] = NULL;
        }
    }

    status = clGetProgramInfo(program,
    CL_PROGRAM_BINARIES, sizeof(char*) * numDevices, binaries.get(),
    NULL);
    CHECK_OPENCL_ERROR_CLEANUP(status,
            "clGetProgramInfo(CL_PROGRAM_BINARIES) failed.");

    /* dump out each binary into its own separate file. */
    for (size_t i = 0; i < numDevices; i++) {
        char deviceName[1024];
        status = clGetDeviceInfo(devices[i],
        CL_DEVICE_NAME, sizeof(deviceName), deviceName,
        NULL);
        CHECK_OPENCL_ERROR_CLEANUP(status,
                "clGetDeviceInfo(CL_DEVICE_NAME) failed.");
        if (binarySizes[i] != 0) {
            // remove white space
            std::string deviceNameString(deviceName);
            deviceNameString.erase(
                    remove_if(deviceNameString.begin(), deviceNameString.end(),
                            ::isspace), deviceNameString.end());

            auto fileName = binaryData.binaryName + "." + deviceNameString;
            std::cout << "Generated binary kernel " << fileName
                    << " for device " << deviceName << std::endl;
            KernelFile BinaryFile;
            if (BinaryFile.writeBinaryToFile(fileName.c_str(), binaries[i],
                    binarySizes[i])) {
                std::cout << "Failed to load kernel file : " << fileName
                        << std::endl;
                rc = FAILURE;
                goto CLEANUP;
            }
        } else {
            printf("%s binary kernel(%s) : %s\n", deviceName,
                    binaryData.binaryName.c_str(),
                    "Skipping as there is no binary data to write!");
        }
    }
// Release all resources and memory
    for (size_t i = 0; i < numDevices; i++) {
        if (binaries[i])
            delete[] binaries[i];
    }
    CLEANUP: if (program) {
        status = clReleaseProgram(program);
        CHECK_OPENCL_ERROR_CLEANUP(status, "clReleaseProgram failed.");
    }
    if (context) {
        status = clReleaseContext(context);
        CHECK_OPENCL_ERROR_CLEANUP(status, "clReleaseContext failed.");
    }
    return rc;
}

DeviceInfo::DeviceInfo() {
    dType = CL_DEVICE_TYPE_GPU;
    venderId = 0;
    maxComputeUnits = 0;
    maxWorkItemDims = 0;
    maxWorkItemSizes = NULL;
    maxWorkGroupSize = 0;
    preferredCharVecWidth = 0;
    preferredShortVecWidth = 0;
    preferredIntVecWidth = 0;
    preferredLongVecWidth = 0;
    preferredFloatVecWidth = 0;
    preferredDoubleVecWidth = 0;
    preferredHalfVecWidth = 0;
    nativeCharVecWidth = 0;
    nativeShortVecWidth = 0;
    nativeIntVecWidth = 0;
    nativeLongVecWidth = 0;
    nativeFloatVecWidth = 0;
    nativeDoubleVecWidth = 0;
    nativeHalfVecWidth = 0;
    maxClockFrequency = 0;
    addressBits = 0;
    maxMemAllocSize = 0;
    imageSupport = CL_FALSE;
    maxReadImageArgs = 0;
    maxWriteImageArgs = 0;
    image2dMaxWidth = 0;
    image2dMaxHeight = 0;
    image3dMaxWidth = 0;
    image3dMaxHeight = 0;
    image3dMaxDepth = 0;
    maxSamplers = 0;
    maxParameterSize = 0;
    memBaseAddressAlign = 0;
    minDataTypeAlignSize = 0;
    singleFpConfig = CL_FP_ROUND_TO_NEAREST | CL_FP_INF_NAN;
    doubleFpConfig = CL_FP_FMA |
    CL_FP_ROUND_TO_NEAREST |
    CL_FP_ROUND_TO_ZERO |
    CL_FP_ROUND_TO_INF |
    CL_FP_INF_NAN |
    CL_FP_DENORM;
    globleMemCacheType = CL_NONE;
    globalMemCachelineSize = CL_NONE;
    globalMemCacheSize = 0;
    globalMemSize = 0;
    maxConstBufSize = 0;
    maxConstArgs = 0;
    localMemType = CL_LOCAL;
    localMemSize = 0;
    errCorrectionSupport = CL_FALSE;
    hostUnifiedMem = CL_FALSE;
    timerResolution = 0;
    endianLittle = CL_FALSE;
    available = CL_FALSE;
    compilerAvailable = CL_FALSE;
    execCapabilities = CL_EXEC_KERNEL;
    queueProperties = 0;
    platform = 0;
    name = NULL;
    vendorName = NULL;
    driverVersion = NULL;
    profileType = NULL;
    deviceVersion = NULL;
    openclCVersion = NULL;
    extensions = NULL;
#ifdef CL_VERSION_2_0
    maxQueueSize = 0;
    memset(&svmcaps, 0, sizeof svmcaps);
#endif
}
;

DeviceInfo::~DeviceInfo() {
    delete[] maxWorkItemSizes;
    delete[] name;
    delete[] vendorName;
    delete[] driverVersion;
    delete[] profileType;
    delete[] deviceVersion;
    delete[] openclCVersion;
    delete[] extensions;
}
;

/**
 * setKernelWorkGroupInfo
 * Set all information for a given device id
 * @param deviceId deviceID
 * @return 0 if success else nonzero
 */
int DeviceInfo::setDeviceInfo(cl_device_id deviceId) {
    cl_int status = CL_SUCCESS;
//Get device type
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_TYPE, sizeof(cl_device_type), &dType,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_TYPE) failed");
//Get vender ID
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_VENDOR_ID, sizeof(cl_uint), &venderId,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_VENDOR_ID) failed");
//Get max compute units
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &maxComputeUnits,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_COMPUTE_UNITS) failed");
//Get max work item dimensions
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(cl_uint), &maxWorkItemDims,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS) failed");
//Get max work item sizes
    delete[] maxWorkItemSizes;
    maxWorkItemSizes = new size_t[maxWorkItemDims];
    CHECK_ALLOCATION(maxWorkItemSizes,
            "Failed to allocate memory(maxWorkItemSizes)");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_WORK_ITEM_SIZES, maxWorkItemDims * sizeof(size_t),
            maxWorkItemSizes,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS) failed");
// Maximum work group size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &maxWorkGroupSize,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE) failed");
// Preferred vector sizes of all data types
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR, sizeof(cl_uint),
            &preferredCharVecWidth,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT, sizeof(cl_uint),
            &preferredShortVecWidth,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT, sizeof(cl_uint),
            &preferredIntVecWidth,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG, sizeof(cl_uint),
            &preferredLongVecWidth,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT, sizeof(cl_uint),
            &preferredFloatVecWidth,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE, sizeof(cl_uint),
            &preferredDoubleVecWidth,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF, sizeof(cl_uint),
            &preferredHalfVecWidth,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF) failed");
// Clock frequency
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(cl_uint), &maxClockFrequency,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_CLOCK_FREQUENCY) failed");
// Address bits
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_ADDRESS_BITS, sizeof(cl_uint), &addressBits,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_ADDRESS_BITS) failed");
// Maximum memory alloc size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(cl_ulong), &maxMemAllocSize,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_MEM_ALLOC_SIZE) failed");
// Image support
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_IMAGE_SUPPORT, sizeof(cl_bool), &imageSupport,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_IMAGE_SUPPORT) failed");
// Maximum read image arguments
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_READ_IMAGE_ARGS, sizeof(cl_uint), &maxReadImageArgs,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_READ_IMAGE_ARGS) failed");
// Maximum write image arguments
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_WRITE_IMAGE_ARGS, sizeof(cl_uint), &maxWriteImageArgs,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_WRITE_IMAGE_ARGS) failed");
// 2D image and 3D dimensions
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_IMAGE2D_MAX_WIDTH, sizeof(size_t), &image2dMaxWidth,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_IMAGE2D_MAX_WIDTH) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_IMAGE2D_MAX_HEIGHT, sizeof(size_t), &image2dMaxHeight,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_IMAGE2D_MAX_HEIGHT) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_IMAGE3D_MAX_WIDTH, sizeof(size_t), &image3dMaxWidth,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_IMAGE3D_MAX_WIDTH) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_IMAGE3D_MAX_HEIGHT, sizeof(size_t), &image3dMaxHeight,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_IMAGE3D_MAX_HEIGHT) failed");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_IMAGE3D_MAX_DEPTH, sizeof(size_t), &image3dMaxDepth,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_IMAGE3D_MAX_DEPTH) failed");
// Maximum samplers
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_SAMPLERS, sizeof(cl_uint), &maxSamplers,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_MAX_SAMPLERS) failed");
// Maximum parameter size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_PARAMETER_SIZE, sizeof(size_t), &maxParameterSize,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_PARAMETER_SIZE) failed");
// Memory base address align
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MEM_BASE_ADDR_ALIGN, sizeof(cl_uint), &memBaseAddressAlign,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MEM_BASE_ADDR_ALIGN) failed");
// Minimum data type align size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE, sizeof(cl_uint), &minDataTypeAlignSize,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE) failed");
// Single precision floating point configuration
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_SINGLE_FP_CONFIG, sizeof(cl_device_fp_config), &singleFpConfig,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_SINGLE_FP_CONFIG) failed");
// Double precision floating point configuration
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_DOUBLE_FP_CONFIG, sizeof(cl_device_fp_config), &doubleFpConfig,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_DOUBLE_FP_CONFIG) failed");
// Global memory cache type
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_GLOBAL_MEM_CACHE_TYPE, sizeof(cl_device_mem_cache_type),
            &globleMemCacheType,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_GLOBAL_MEM_CACHE_TYPE) failed");
// Global memory cache line size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE, sizeof(cl_uint),
            &globalMemCachelineSize,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE) failed");
// Global memory cache size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, sizeof(cl_ulong), &globalMemCacheSize,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_GLOBAL_MEM_CACHE_SIZE) failed");
// Global memory size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(cl_ulong), &globalMemSize,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_GLOBAL_MEM_SIZE) failed");
// Maximum constant buffer size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, sizeof(cl_ulong), &maxConstBufSize,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE) failed");
// Maximum constant arguments
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_MAX_CONSTANT_ARGS, sizeof(cl_uint), &maxConstArgs,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_MAX_CONSTANT_ARGS) failed");
// Local memory type
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_LOCAL_MEM_TYPE, sizeof(cl_device_local_mem_type), &localMemType,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_LOCAL_MEM_TYPE) failed");
// Local memory size
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_LOCAL_MEM_SIZE, sizeof(cl_ulong), &localMemSize,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_LOCAL_MEM_SIZE) failed");
// Error correction support
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_ERROR_CORRECTION_SUPPORT, sizeof(cl_bool), &errCorrectionSupport,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_ERROR_CORRECTION_SUPPORT) failed");
// Profiling timer resolution
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PROFILING_TIMER_RESOLUTION, sizeof(size_t), &timerResolution,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_PROFILING_TIMER_RESOLUTION) failed");
// Endian little
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_ENDIAN_LITTLE, sizeof(cl_bool), &endianLittle,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_ENDIAN_LITTLE) failed");
// Device available
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_AVAILABLE, sizeof(cl_bool), &available,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_AVAILABLE) failed");
// Device compiler available
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_COMPILER_AVAILABLE, sizeof(cl_bool), &compilerAvailable,
    NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_COMPILER_AVAILABLE) failed");
// Device execution capabilities
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_EXECUTION_CAPABILITIES, sizeof(cl_device_exec_capabilities),
            &execCapabilities,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_EXECUTION_CAPABILITIES) failed");
// Device queue properities
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_QUEUE_PROPERTIES, sizeof(cl_command_queue_properties),
            &queueProperties,
            NULL);
    CHECK_OPENCL_ERROR(status,
            "clGetDeviceInfo(CL_DEVICE_QUEUE_PROPERTIES) failed");
// Platform
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PLATFORM, sizeof(cl_platform_id), &platform,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_PLATFORM) failed");
// Device name
    size_t tempSize = 0;
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_NAME, 0,
    NULL, &tempSize);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_NAME) failed");
    delete[] name;
    name = new char[tempSize];
    CHECK_ALLOCATION(name, "Failed to allocate memory(name)");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_NAME, sizeof(char) * tempSize, name,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_NAME) failed");
// Vender name
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_VENDOR, 0,
    NULL, &tempSize);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_VENDOR) failed");
    delete[] vendorName;
    vendorName = new char[tempSize];
    CHECK_ALLOCATION(vendorName, "Failed to allocate memory(venderName)");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_VENDOR, sizeof(char) * tempSize, vendorName,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_VENDOR) failed");
// Driver name
    status = clGetDeviceInfo(deviceId,
    CL_DRIVER_VERSION, 0,
    NULL, &tempSize);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DRIVER_VERSION) failed");
    delete[] driverVersion;
    driverVersion = new char[tempSize];
    CHECK_ALLOCATION(driverVersion, "Failed to allocate memory(driverVersion)");
    status = clGetDeviceInfo(deviceId,
    CL_DRIVER_VERSION, sizeof(char) * tempSize, driverVersion,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DRIVER_VERSION) failed");
// Device profile
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PROFILE, 0,
    NULL, &tempSize);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_PROFILE) failed");
    delete[] profileType;
    profileType = new char[tempSize];
    CHECK_ALLOCATION(profileType, "Failed to allocate memory(profileType)");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_PROFILE, sizeof(char) * tempSize, profileType,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_PROFILE) failed");
// Device version
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_VERSION, 0,
    NULL, &tempSize);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_VERSION) failed");
    delete[] deviceVersion;
    deviceVersion = new char[tempSize];
    CHECK_ALLOCATION(deviceVersion, "Failed to allocate memory(deviceVersion)");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_VERSION, sizeof(char) * tempSize, deviceVersion,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_VERSION) failed");
// Device extensions
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_EXTENSIONS, 0,
    NULL, &tempSize);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_EXTENSIONS) failed");
    delete[] extensions;
    extensions = new char[tempSize];
    CHECK_ALLOCATION(extensions, "Failed to allocate memory(extensions)");
    status = clGetDeviceInfo(deviceId,
    CL_DEVICE_EXTENSIONS, sizeof(char) * tempSize, extensions,
    NULL);
    CHECK_OPENCL_ERROR(status, "clGetDeviceInfo(CL_DEVICE_EXTENSIONS) failed");
// Device parameters of OpenCL 1.1 Specification
#ifdef CL_VERSION_1_1
    std::string deviceVerStr(deviceVersion);
    size_t vStart = deviceVerStr.find(" ", 0);
    size_t vEnd = deviceVerStr.find(" ", vStart + 1);
    std::string vStrVal = deviceVerStr.substr(vStart + 1, vEnd - vStart - 1);
    if (vStrVal.compare("1.0") > 0) {
        // Native vector sizes of all data types
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR, sizeof(cl_uint),
                &nativeCharVecWidth,
                NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR) failed");
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT, sizeof(cl_uint),
                &nativeShortVecWidth,
                NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT) failed");
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_NATIVE_VECTOR_WIDTH_INT, sizeof(cl_uint), &nativeIntVecWidth,
        NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_NATIVE_VECTOR_WIDTH_INT) failed");
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG, sizeof(cl_uint),
                &nativeLongVecWidth,
                NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG) failed");
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT, sizeof(cl_uint),
                &nativeFloatVecWidth,
                NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT) failed");
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE, sizeof(cl_uint),
                &nativeDoubleVecWidth,
                NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE) failed");
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF, sizeof(cl_uint),
                &nativeHalfVecWidth,
                NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF) failed");
        // Host unified memory
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(cl_bool), &hostUnifiedMem,
        NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_HOST_UNIFIED_MEMORY) failed");
        // Device OpenCL C version
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_OPENCL_C_VERSION, 0,
        NULL, &tempSize);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_OPENCL_C_VERSION) failed");
        delete[] openclCVersion;
        openclCVersion = new char[tempSize];
        CHECK_ALLOCATION(openclCVersion,
                "Failed to allocate memory(openclCVersion)");
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_OPENCL_C_VERSION, sizeof(char) * tempSize, openclCVersion,
        NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_OPENCL_C_VERSION) failed");
    }
#endif
#ifdef CL_VERSION_2_0
    if (checkOpenCL2_XCompatibility()) {
        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_SVM_CAPABILITIES, sizeof(cl_device_svm_capabilities),
                &svmcaps,
                NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_SVM_CAPABILITIES) failed");

        status = clGetDeviceInfo(deviceId,
        CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE, sizeof(cl_uint), &maxQueueSize,
        NULL);
        CHECK_OPENCL_ERROR(status,
                "clGetDeviceInfo(CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE) failed");
    }
#endif
    return SUCCESS;
}

/**
 * detectSVM
 * Check if the device supports Shared virtual memory(SVM)
 * @return bool
 */
bool DeviceInfo::detectSVM() {
    bool svmSupport = false;

#ifdef CL_VERSION_2_0
    if (this->svmcaps
            & (CL_DEVICE_SVM_COARSE_GRAIN_BUFFER
                    | CL_DEVICE_SVM_FINE_GRAIN_BUFFER
                    | CL_DEVICE_SVM_FINE_GRAIN_SYSTEM | CL_DEVICE_SVM_ATOMICS)) {
        svmSupport = true;
    }
#endif

    return svmSupport;
}

/**
 * detectOpenCL2_xCompatibility
 * Check if the device supports OpenCL 2.x
 * @return @bool
 */
bool DeviceInfo::checkOpenCL2_XCompatibility() {
    bool isOpenCL2_XSupported = false;

    int majorRev, minorRev;
    if (sscanf(this->deviceVersion, "OpenCL %d.%d", &majorRev, &minorRev)
            == 2) {
        if (majorRev >= 2) {
            isOpenCL2_XSupported = true;
        }
    }

    return isOpenCL2_XSupported;
}

void Util::LogInfo(const char *str, ...) {
    if (str) {
        va_list args;
        va_start(args, str);

        vfprintf(stdout, str, args);

        va_end(args);
    }
}

void Util::LogError(const char *str, ...) {
    if (str) {
        va_list args;
        va_start(args, str);

        vfprintf(stderr, str, args);

        va_end(args);
    }
}

// Obtains a list of OpenCL platforms available
// numPlatforms returns the number of platforms available
// Note: A memory allocation is done for platforms
// The caller should be responsible to deallocate it
cl_int Util::GetPlatformIds(cl_platform_id **platforms, cl_uint *numPlatforms) {
    cl_int errorCode = CL_SUCCESS;

    *platforms = NULL;
    *numPlatforms = 0;

// Get (in numPlatforms) the number of OpenCL platforms available
// No platform ID will be return, since platforms is NULL
    errorCode = clGetPlatformIDs(0, NULL, numPlatforms);
    if (errorCode != CL_SUCCESS) {
        Util::LogError(
                "Error: clGetplatform_ids() to get num platforms returned %s.\n",
                Util::TranslateOpenCLError(errorCode));
        return errorCode;
    } else if (*numPlatforms == 0) {
        Util::LogError("Error: No platforms found!\n");
        return CL_INVALID_PLATFORM;
    }

    *platforms = new cl_platform_id[*numPlatforms];
    if (*platforms == NULL) {
        Util::LogError("Error: Couldn't allocate memory for platforms!\n");
        return CL_OUT_OF_HOST_MEMORY;
    }

// Now, obtains a list of numPlatforms OpenCL platforms available
// The list of platforms available will be returned in platforms
    errorCode = clGetPlatformIDs(*numPlatforms, *platforms, NULL);
    if (errorCode != CL_SUCCESS) {
        Util::LogError(
                "Error: clGetplatform_ids() to get platforms returned %s.\n",
                Util::TranslateOpenCLError(errorCode));
        return errorCode;
    }

    return CL_SUCCESS;
}

// Translating the input flags to an OpenCL device type
cl_device_type Util::TranslateDeviceType(bool preferCpu, bool preferGpu,
        bool preferShared) {
    cl_device_type deviceType = CL_DEVICE_TYPE_ALL;

// Looking for both CPU and GPU devices is like selecting CL_DEVICE_TYPE_ALL
    if (preferCpu && preferGpu) {
        preferCpu = false;
        preferGpu = false;
        deviceType = CL_DEVICE_TYPE_ALL;
    } else if (preferShared) {
        deviceType = CL_DEVICE_TYPE_ALL;
    } else if (preferCpu) {
        deviceType = CL_DEVICE_TYPE_CPU;
    } else if (preferGpu) {
        deviceType = CL_DEVICE_TYPE_GPU;
    }

    return deviceType;
}

// Check whether platform is an OpenCL vendor platform
cl_int Util::CheckPreferredVendorMatch(cl_platform_id platform,
        const char *preferredVendor, bool *match) {
    size_t stringLength = 0;
    cl_int errorCode = CL_SUCCESS;

    *match = false;

// In order to read the platform vendor id, we first read the platform's vendor string length (param_value is NULL).
// The value returned in stringLength
    errorCode = clGetPlatformInfo(platform, CL_PLATFORM_VENDOR, 0, NULL,
            &stringLength);
    if (errorCode != CL_SUCCESS) {
        Util::LogError(
                "Error: clGetPlatformInfo() to get CL_PLATFORM_VENDOR length returned '%s'.\n",
                Util::TranslateOpenCLError(errorCode));
        return errorCode;
    }

// Now. that we know the platform's vendor string length, we can allocate space for it and read it
    char *str = new char[stringLength];
    if (str == NULL) {
        Util::LogError(
                "Error: Couldn't allocate memory for CL_PLATFORM_VENDOR string.\n");
        return CL_OUT_OF_HOST_MEMORY;
    }

// Read the platform's vendor string
// The read value returned in str
    errorCode = clGetPlatformInfo(platform, CL_PLATFORM_VENDOR, sizeof(str),
            str,
            NULL);
    if (errorCode != CL_SUCCESS) {
        Util::LogError(
                "Error: clGetplatform_ids() to get CL_PLATFORM_VENDOR returned %s.\n",
                Util::TranslateOpenCLError(errorCode));
    } else if (strcmp(str, preferredVendor) == 0) {
        // The checked platform is the one we're looking for
        *match = true;
    }

    delete[] str;

    return errorCode;
}

// Find and return a "preferredVendor" OpenCL platform
// In case that preferredVendor is NULL, the ID of the first discovered platform will be returned
cl_platform_id Util::FindPlatformId(const char *preferredVendor, bool preferCpu,
        bool preferGpu, bool preferShared) {
    cl_platform_id platform = NULL;
    cl_platform_id *platforms = NULL;
    cl_uint numPlatforms = 0;
    cl_int errorCode = CL_SUCCESS;

// Obtains a list of OpenCL platforms available
// Note: there is a memory allocation inside GetPlatformIds()
    errorCode = GetPlatformIds(&platforms, &numPlatforms);
    if ((platforms == NULL) || (numPlatforms == 0)) {
        Util::LogError(
                "Error: clGetplatform_ids() to get platforms returned %s.\n",
                Util::TranslateOpenCLError(errorCode));
    } else {
        // Check if one of the available platform matches the preffered requirements
        cl_uint maxDevices = 0;
        cl_device_type deviceType = TranslateDeviceType(preferCpu, preferGpu,
                preferShared);

        for (cl_uint i = 0; i < numPlatforms; i++) {
            bool match = true;
            cl_uint numDevices = 0;

            // Obtains the number of deviceType devices available on platform
            // When the function failed we expect numDevices to be zero.
            // We ignore the function return value since a nonzero error code
            // could happen if this platform doesn't support the specified device type.
            clGetDeviceIDs(platforms[i], deviceType, 0, NULL, &numDevices);

            // In case the platform includes preferred deviceType continue to check it
            if (numDevices != 0) {
                if (preferredVendor != NULL) {
                    // In case we're looking for a specific vendor
                    errorCode = CheckPreferredVendorMatch(platforms[i],
                            preferredVendor, &match);
                }

                // We don't care which OpenCL platform we found
                // So, we'll check it for the preferred device(s)
                else if (preferShared) {
                    // In case of preferShared (shared context) -
                    // the first platform with match devices will be selected
                    match = false;
                    if (numDevices > maxDevices) {
                        maxDevices = numDevices;
                        match = true;
                    }
                }

                if (match) {
                    platform = platforms[i];
                }
            }
        }
    }

    delete[] platforms;

// If we couldn't find a platform that matched the specified preferences
// but we were otherwise successful, try to find any platform.
    if ((platform == NULL) && (errorCode == CL_SUCCESS)
            && ((preferredVendor != NULL) || (preferCpu == true)
                    || (preferGpu == true) || (preferShared == true))) {
        platform = FindPlatformId();
    }

    return platform;
}

// Create a context with the preferred devices
cl_context Util::CreateContext(cl_platform_id platformId, bool preferCpu,
        bool preferGpu, bool preferShared) {
    cl_context context = NULL;

// If both devices are preferred, we'll try both of them separately
    if (preferCpu && preferGpu) {
        preferCpu = false;
        preferGpu = false;
    }

    if (preferShared) {
        LogInfo("Info: Trying to create a shared context (preferred)...\n");
        context = CreateSharedContext(platformId);
    } else if (preferCpu || preferGpu) {
        if (preferCpu) {
            LogInfo("Info: Trying to create a CPU context (preferred)...\n");
            context = CreateCPUContext(platformId);
        } else if (preferGpu) {
            LogInfo("Info: Trying to create a GPU context (preferred)...\n");
            context = CreateGPUContext(platformId);
        }
    } else {
        if (context == NULL) {
            LogInfo("Info: Trying to create a GPU context...\n");
            context = CreateGPUContext(platformId);
        }
        if (context == NULL) {
            LogInfo("Info: Trying to create a CPU context...\n");
            context = CreateCPUContext(platformId);
        }
    }

    return context;
}

// Create a CPU based context
cl_context Util::CreateCPUContext(cl_platform_id platformId) {
    cl_int errorCode = CL_SUCCESS;

    cl_context_properties contextProperties[] = { CL_CONTEXT_PLATFORM,
            (cl_context_properties) platformId, 0 };

// Create context with all the CPU devices in the platforms.
// The creation is synchronized (pfn_notify is NULL) and NULL user_data
    cl_context context = clCreateContextFromType(contextProperties,
    CL_DEVICE_TYPE_CPU, NULL, NULL, &errorCode);
    if ((errorCode != CL_SUCCESS) || (context == NULL)) {
        Util::LogError(
                "Couldn't create a CPU context, clCreateContextFromType() returned '%s'.\n",
                Util::TranslateOpenCLError(errorCode));
    }

    return context;
}

// Create a GPU based context
cl_context Util::CreateGPUContext(cl_platform_id platformId) {
    cl_int errorCode = CL_SUCCESS;

    cl_context_properties contextProperties[] = { CL_CONTEXT_PLATFORM,
            (cl_context_properties) platformId, 0 };

// Create context with all the GPU devices in the platforms.
// The creation is synchronized (pfn_notify is NULL) and NULL user_data
    cl_context context = clCreateContextFromType(contextProperties,
    CL_DEVICE_TYPE_GPU, NULL, NULL, &errorCode);
    if ((errorCode != CL_SUCCESS) || (context == NULL)) {
        Util::LogError(
                "Couldn't create a GPU context, clCreateContextFromType() returned '%s'.\n",
                Util::TranslateOpenCLError(errorCode));
    }

    return context;
}

// Create a shared context with both CPU and GPU devices
cl_context Util::CreateSharedContext(cl_platform_id platformId) {
    cl_int errorCode = CL_SUCCESS;

    cl_context_properties contextProperties[] = { CL_CONTEXT_PLATFORM,
            (cl_context_properties) platformId, 0 };

// Create context with all the devices in the platforms.
// The creation is synchronized (pfn_notify is NULL) and NULL user_data
    cl_context context = clCreateContextFromType(contextProperties,
    CL_DEVICE_TYPE_ALL, NULL, NULL, &errorCode);
    if ((errorCode != CL_SUCCESS) || (context == NULL)) {
        Util::LogError(
                "Couldn't create a shared context, clCreateContextFromType() returned '%s'.\n",
                Util::TranslateOpenCLError(errorCode));
    }

    return context;
}

// Translate OpenCL numeric error code (errorCode) to a meaningful error string
const char* Util::TranslateOpenCLError(cl_int errorCode) {
    switch (errorCode) {
    case CL_SUCCESS:
        return "CL_SUCCESS";
    case CL_DEVICE_NOT_FOUND:
        return "CL_DEVICE_NOT_FOUND";
    case CL_DEVICE_NOT_AVAILABLE:
        return "CL_DEVICE_NOT_AVAILABLE";
    case CL_COMPILER_NOT_AVAILABLE:
        return "CL_COMPILER_NOT_AVAILABLE";
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:
        return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case CL_OUT_OF_RESOURCES:
        return "CL_OUT_OF_RESOURCES";
    case CL_OUT_OF_HOST_MEMORY:
        return "CL_OUT_OF_HOST_MEMORY";
    case CL_PROFILING_INFO_NOT_AVAILABLE:
        return "CL_PROFILING_INFO_NOT_AVAILABLE";
    case CL_MEM_COPY_OVERLAP:
        return "CL_MEM_COPY_OVERLAP";
    case CL_IMAGE_FORMAT_MISMATCH:
        return "CL_IMAGE_FORMAT_MISMATCH";
    case CL_IMAGE_FORMAT_NOT_SUPPORTED:
        return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
    case CL_BUILD_PROGRAM_FAILURE:
        return "CL_BUILD_PROGRAM_FAILURE";
    case CL_MAP_FAILURE:
        return "CL_MAP_FAILURE";
    case CL_INVALID_VALUE:
        return "CL_INVALID_VALUE";
    case CL_INVALID_DEVICE_TYPE:
        return "CL_INVALID_DEVICE_TYPE";
    case CL_INVALID_PLATFORM:
        return "CL_INVALID_PLATFORM";
    case CL_INVALID_DEVICE:
        return "CL_INVALID_DEVICE";
    case CL_INVALID_CONTEXT:
        return "CL_INVALID_CONTEXT";
    case CL_INVALID_QUEUE_PROPERTIES:
        return "CL_INVALID_QUEUE_PROPERTIES";
    case CL_INVALID_COMMAND_QUEUE:
        return "CL_INVALID_COMMAND_QUEUE";
    case CL_INVALID_HOST_PTR:
        return "CL_INVALID_HOST_PTR";
    case CL_INVALID_MEM_OBJECT:
        return "CL_INVALID_MEM_OBJECT";
    case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:
        return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
    case CL_INVALID_IMAGE_SIZE:
        return "CL_INVALID_IMAGE_SIZE";
    case CL_INVALID_SAMPLER:
        return "CL_INVALID_SAMPLER";
    case CL_INVALID_BINARY:
        return "CL_INVALID_BINARY";
    case CL_INVALID_BUILD_OPTIONS:
        return "CL_INVALID_BUILD_OPTIONS";
    case CL_INVALID_PROGRAM:
        return "CL_INVALID_PROGRAM";
    case CL_INVALID_PROGRAM_EXECUTABLE:
        return "CL_INVALID_PROGRAM_EXECUTABLE";
    case CL_INVALID_KERNEL_NAME:
        return "CL_INVALID_KERNEL_NAME";
    case CL_INVALID_KERNEL_DEFINITION:
        return "CL_INVALID_KERNEL_DEFINITION";
    case CL_INVALID_KERNEL:
        return "CL_INVALID_KERNEL";
    case CL_INVALID_ARG_INDEX:
        return "CL_INVALID_ARG_INDEX";
    case CL_INVALID_ARG_VALUE:
        return "CL_INVALID_ARG_VALUE";
    case CL_INVALID_ARG_SIZE:
        return "CL_INVALID_ARG_SIZE";
    case CL_INVALID_KERNEL_ARGS:
        return "CL_INVALID_KERNEL_ARGS";
    case CL_INVALID_WORK_DIMENSION:
        return "CL_INVALID_WORK_DIMENSION";
    case CL_INVALID_WORK_GROUP_SIZE:
        return "CL_INVALID_WORK_GROUP_SIZE";
    case CL_INVALID_WORK_ITEM_SIZE:
        return "CL_INVALID_WORK_ITEM_SIZE";
    case CL_INVALID_GLOBAL_OFFSET:
        return "CL_INVALID_GLOBAL_OFFSET";
    case CL_INVALID_EVENT_WAIT_LIST:
        return "CL_INVALID_EVENT_WAIT_LIST";
    case CL_INVALID_EVENT:
        return "CL_INVALID_EVENT";
    case CL_INVALID_OPERATION:
        return "CL_INVALID_OPERATION";
    case CL_INVALID_GL_OBJECT:
        return "CL_INVALID_GL_OBJECT";
    case CL_INVALID_BUFFER_SIZE:
        return "CL_INVALID_BUFFER_SIZE";
    case CL_INVALID_MIP_LEVEL:
        return "CL_INVALID_MIP_LEVEL";
    default:
        break;
    }

    Util::LogError("Unknown Error %08X\n", errorCode);
    return "*** Unknown Error ***";
}
cl_int Util::ReleaseMemory(cl_mem memory) {
    cl_int error_code = CL_SUCCESS;
    if (memory) {
        error_code = clReleaseMemObject(memory);
        if (CL_SUCCESS != error_code) {
            Util::LogError(
                    "Error: clReleaseMemObject (CL_QUEUE_CONTEXT) returned %s.\n",
                    Util::TranslateOpenCLError(error_code));
        }
    }
    return error_code;

}

cl_event Util::CreateUserEvent(cl_context ctxt) {
    cl_int errorCode;
    cl_event evt = clCreateUserEvent(ctxt, &errorCode);
// TODO: throw exception on error
    if (CL_SUCCESS != errorCode) {
        Util::LogError(
                "Error: clCreateUserEvent (CL_QUEUE_CONTEXT) returned %s.\n",
                Util::TranslateOpenCLError(errorCode));
        return 0;
    }
    return evt;
}

cl_event Util::RetainEvent(cl_event evt) {
    if (evt) {
        cl_int errorCode = clRetainEvent(evt);
        if (CL_SUCCESS != errorCode) {
            Util::LogError(
                    "Error: clRetainEvent (CL_QUEUE_CONTEXT) returned %s.\n",
                    Util::TranslateOpenCLError(errorCode));
        }
        return evt;
    }
    return 0;
}

void Util::ReleaseEvent(cl_event evt) {
    if (evt) {
        cl_int errorCode = clReleaseEvent(evt);
        if (CL_SUCCESS != errorCode) {
            Util::LogError(
                    "Error: clReleaseEvent (CL_QUEUE_CONTEXT) returned %s.\n",
                    Util::TranslateOpenCLError(errorCode));
        }
    }
}

void Util::SetEventComplete(cl_event evt) {
    if (evt) {
        cl_int errorCode = clSetUserEventStatus(evt, CL_COMPLETE);
        if (CL_SUCCESS != errorCode) {
            Util::LogError(
                    "Error: clSetUserEventStatus (CL_QUEUE_CONTEXT) returned %s.\n",
                    Util::TranslateOpenCLError(errorCode));
        }
    }
}

cl_int Util::mapImage(cl_command_queue queue, cl_mem img, bool synchronous,
        cl_map_flags flags, size_t width, size_t height, cl_uint numWaitEvents,
        const cl_event *waitEvents, cl_event *completionEvent,
        void **mappedPtr) {
    if (!mappedPtr)
        return -1;

    cl_int error_code = CL_SUCCESS;
    size_t image_dimensions[3] = { width, height, 1 };
    size_t image_origin[3] = { 0, 0, 0 };
    size_t image_row_pitch;

    *mappedPtr = clEnqueueMapImage(queue, img, synchronous, flags, image_origin,
            image_dimensions, &image_row_pitch,
            NULL, numWaitEvents, waitEvents, completionEvent, &error_code);
    if (CL_SUCCESS != error_code) {
        Util::LogError("Error: clEnqueueMapImage return %s.\n",
                Util::TranslateOpenCLError(error_code));
    }
    return error_code;
}

cl_int Util::mapBuffer(cl_command_queue queue, cl_mem buffer, bool synchronous,
        cl_map_flags flags, size_t size, cl_uint numWaitEvents,
        const cl_event *waitEvents, cl_event *completionEvent,
        void **mappedPtr) {
    if (!mappedPtr)
        return -1;

    cl_int error_code = CL_SUCCESS;
    *mappedPtr = clEnqueueMapBuffer(queue, buffer, synchronous, flags, 0, size,
            numWaitEvents, waitEvents, completionEvent, &error_code);
    if (CL_SUCCESS != error_code) {
        Util::LogError("Error: clEnqueueMapBuffer return %s.\n",
                Util::TranslateOpenCLError(error_code));
    }
    return error_code;
}

cl_int Util::unmapMemory(cl_command_queue queue, cl_uint numWaitEvents,
        const cl_event *waitEvents, cl_event *completionEvent, cl_mem memory,
        void *mappedPtr) {
    if (!mappedPtr)
        return -1;

    cl_int error_code = clEnqueueUnmapMemObject(queue, memory, mappedPtr,
            numWaitEvents, waitEvents, completionEvent);
    if (CL_SUCCESS != error_code) {
        Util::LogError("Error: clEnqueueUnmapMemObject return %s.\n",
                Util::TranslateOpenCLError(error_code));

    }
    return error_code;

}

cl_int Util::getRefCount(cl_event evt) {
    cl_int refCount = -1;
    cl_int error_code = clGetEventInfo(evt, CL_EVENT_REFERENCE_COUNT,
            sizeof(refCount), &refCount, NULL);
    if (CL_SUCCESS != error_code) {
        Util::LogError("Error: clGetEventInfo return %s.\n",
                Util::TranslateOpenCLError(error_code));
    }
    return refCount;
}
}
#endif
