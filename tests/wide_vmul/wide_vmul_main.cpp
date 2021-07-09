#include <memory>
#include <sstream>
#include <string>
#include <chrono>

#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_TARGET_OPENCL_VERSION 120
#define CL_HPP_MINIMUM_OPENCL_VERSION 120

#include <CL/cl2.hpp>
#include <CL/cl_ext_xilinx.h>
#include "latke.h"
using namespace ltk;

std::string kernelName = "wide_vmul";
const uint32_t num_concurrent_kernels = 4;

// this is equivalent to single HD RGBA frame (floating point)
const uint32_t fullBufferSize = (1080 * 1920 * 4);
const uint32_t bufferSizePerKernel = fullBufferSize/num_concurrent_kernels;

int enqueue_buf(cl::CommandQueue &q, cl::Kernel &krnl, cl::Event *event, cl::Buffer a, cl::Buffer b, cl::Buffer c)
{
    // Get the size of the buffer
    cl::Event k_event, m_event;
    std::vector<cl::Event> krnl_events;

    static std::vector<cl::Event> tx_events, rx_events;

    std::vector<cl::Memory> c_vec;
    size_t size;
    size = a.getInfo<CL_MEM_SIZE>();

    std::vector<cl::Memory> in_vec;
    in_vec.push_back(a);
    in_vec.push_back(b);
    q.enqueueMigrateMemObjects(in_vec, 0, &tx_events, &m_event);
    krnl_events.push_back(m_event);
    tx_events.push_back(m_event);
    if (tx_events.size() > 1) {
        tx_events[0] = tx_events[1];
        tx_events.pop_back();
    }

    krnl.setArg(0, a);
    krnl.setArg(1, b);
    krnl.setArg(2, c);
    krnl.setArg(3, (uint32_t)(size));

    q.enqueueTask(krnl, &krnl_events, &k_event);
    krnl_events.push_back(k_event);
    if (rx_events.size() == 1) {
        krnl_events.push_back(rx_events[0]);
        rx_events.pop_back();
    }
    c_vec.push_back(c);
    q.enqueueMigrateMemObjects(c_vec, CL_MIGRATE_MEM_OBJECT_HOST, &krnl_events, event);
    rx_events.push_back(*event);

    return 0;
}

struct KernelObjects {
	KernelObjects() : a(nullptr),b(nullptr),c(nullptr)
	{}
    cl::Buffer a_buf;
    cl::Buffer b_buf;
    cl::Buffer c_buf;
    float *a, *b, *c;

	cl_mem_ext_ptr_t in_bank_ext;
	cl::Kernel krnl;
	cl::Event kernel_event;
};


int main(int argc, char *argv[])
{
	if (argc < 2){
		printf("too few arguments");
		exit(0);
	}

	const char* kernelBinary = argv[1];

  	// 1. create device manager
    const int platformId = 0;
    const eDeviceType deviceType = ACCELERATOR;
    const int deviceNum = 0;

  	auto deviceManager = std::make_shared<DeviceManagerOCL>(true);
    cl_command_queue_properties queue_props = CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE;
  	auto rc = deviceManager->init(platformId, deviceType, deviceNum, true, queue_props);
  	if (rc != DeviceSuccess) {
  		std::cerr << "Failed to initialize OpenCL device";
  		return -1;
  	}

	auto dev = deviceManager->getDevice(deviceNum);
	auto arch = ArchFactory::getArchitecture(dev->deviceInfo->venderId);
	if (!arch){
	    std::cerr << "Unsupported OpenCL vendor ID " << dev->deviceInfo->venderId;
	    return -1;
	}
    cl::Context ctx = cl::Context(dev->context, true);
    cl::CommandQueue q = cl::CommandQueue(dev->queue, true);

    std::cout << std::endl << std::endl;
    std::cout << "Running kernel test with XRT-allocated contiguous buffers" << std::endl;
    std::cout << "and wide vmul (16 values/clock)" << std::endl;

	std::stringstream buildOptions;
	buildOptions << " -I ./ ";
	KernelInitInfoBase initInfoBase(dev, buildOptions.str(), "",LOAD_BINARY);
	KernelInitInfo initInfo(initInfoBase, "", kernelBinary,"");
	cl_program program = 0;
	try {
		program = KernelOCL::generateProgram(initInfo);
		if (!program){
			std::cerr << "Failed to generate program";
			return -1;
		}
	} catch (std::exception &ex){
		std::cerr << "Failed to generate program. Exiting";
		return -1;
	}


    // Map our user-allocated buffers as OpenCL buffers using a shared
    // host pointer
    KernelObjects objects[num_concurrent_kernels];
    for (uint32_t i = 0; i < num_concurrent_kernels; ++i) {
    	auto obj = objects + i;
		try {

	    	std::stringstream ss;
	    	ss << kernelName << ":{" << kernelName << "_" << (i + 1) << "}";
	    	initInfo.kernelName = ss.str();

	    	KernelOCL *kernel = nullptr;
	    	try {
	    		kernel = new KernelOCL(initInfo,program);
	    	} catch (std::runtime_error &re) {
	    		std::cerr << "Unable to build kernel. Exiting" << std::endl;
	    		return -1;
	    	}

	        obj->krnl = cl::Kernel(kernel->getKernel(), true);

	    	obj->in_bank_ext.flags = i | XCL_MEM_TOPOLOGY;
			obj->in_bank_ext.obj   = NULL;
			obj->in_bank_ext.param = 0;

			obj->a_buf = cl::Buffer(ctx,
							 static_cast<cl_mem_flags>(CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY | CL_MEM_EXT_PTR_XILINX),
							 bufferSizePerKernel * sizeof(float),
							 &obj->in_bank_ext,
							 NULL);
			obj->b_buf = cl::Buffer(ctx,
							 static_cast<cl_mem_flags>(CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY | CL_MEM_EXT_PTR_XILINX),
							 bufferSizePerKernel * sizeof(float),
							 &obj->in_bank_ext,
							 NULL);
			obj->c_buf = cl::Buffer(ctx,
							 static_cast<cl_mem_flags>(CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY | CL_MEM_EXT_PTR_XILINX),
							 bufferSizePerKernel * sizeof(float),
							 &obj->in_bank_ext,
							 NULL);

			// Although we'll change these later, we'll set the buffers as kernel
			// arguments prior to mapping so that XRT can resolve the physical memory
			// in which they need to be allocated
			obj->krnl.setArg(0, obj->a_buf);
			obj->krnl.setArg(1, obj->b_buf);
			obj->krnl.setArg(2, obj->c_buf);

			obj->a = (float *)q.enqueueMapBuffer(obj->a_buf,
														 CL_TRUE,
														 CL_MAP_WRITE_INVALIDATE_REGION,
														 0,
														 bufferSizePerKernel * sizeof(float));
			obj->b = (float *)q.enqueueMapBuffer(obj->b_buf,
														 CL_TRUE,
														 CL_MAP_WRITE_INVALIDATE_REGION,
														 0,
														 bufferSizePerKernel * sizeof(float));
			for (int i = 0; i < bufferSizePerKernel; i++) {
				obj->a[i] = i;
				obj->b[i] = 2 * i;
			}
		}
		catch (cl::Error &e) {
			std::cout << "ERROR: " << e.what() << std::endl;
			return EXIT_FAILURE;
		}
    }

	auto start = std::chrono::high_resolution_clock::now();
	std::array<cl::Event,  num_concurrent_kernels > globalKernelEvents;

    for (uint32_t i = 0; i < num_concurrent_kernels; ++i) {
    	auto obj = objects + i;
		try {
			// Send the buffers down to the Alveo card
			q.enqueueUnmapMemObject(obj->a_buf, obj->a);
			q.enqueueUnmapMemObject(obj->b_buf, obj->b);

			// enqueue kernel
			enqueue_buf(q, obj->krnl, &obj->kernel_event, obj->a_buf, obj->b_buf, obj->c_buf);

		}
		catch (cl::Error &e) {
			std::cout << "ERROR: " << e.what() << std::endl;
			return EXIT_FAILURE;
		}
    }

    for (uint32_t i = 0; i < num_concurrent_kernels; ++i) {
    	auto obj = objects + i;
   		 globalKernelEvents[i] = obj->kernel_event.get();
    }

	clWaitForEvents(globalKernelEvents.size(), (const cl_event *)&globalKernelEvents);

    for (uint32_t i = 0; i < num_concurrent_kernels; ++i) {
    	auto obj = objects + i;

    	// 1. map output
    	obj->c = (float *)q.enqueueMapBuffer(obj->c_buf,
													 CL_TRUE,
													 CL_MAP_READ,
													 0,
													 bufferSizePerKernel * sizeof(float));
		// 2. process output

		// 3. unmap output
		q.enqueueUnmapMemObject(obj->c_buf, obj->c);
    }
	auto finish = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = finish - start;
	std::cout << "Execution time " <<  (elapsed.count() * 1000.0) << " ms for FP buffer of size " << fullBufferSize << std::endl;

    q.finish();

    if (program)
    	clReleaseProgram(program);
}
